/*
 * BlueALSA - io.c
 * Copyright (c) 2016-2018 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "io.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/timerfd.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sco.h>

#include <sbc/sbc.h>
#if ENABLE_AAC
# include <fdk-aac/aacdecoder_lib.h>
# include <fdk-aac/aacenc_lib.h>
# define AACENCODER_LIB_VERSION LIB_VERSION( \
		AACENCODER_LIB_VL0, AACENCODER_LIB_VL1, AACENCODER_LIB_VL2)
#endif
#if ENABLE_APTX
# include <openaptx.h>
#endif
#if ENABLE_LDAC
# include <ldacBT.h>
# include <ldacBT_abr.h>
#endif

#include "a2dp-codecs.h"
#include "a2dp-rtp.h"
#include "bluealsa.h"
#include "transport.h"
#include "utils.h"
#include "shared/defs.h"
#include "shared/ffb.h"
#include "shared/log.h"
#include "shared/rt.h"


/**
 * Scale PCM signal according to the transport audio properties. */
static void io_thread_scale_pcm(const struct ba_transport *t, int16_t *buffer,
		size_t samples, int channels) {

	/* Get a snapshot of audio properties. Please note, that mutex is not
	 * required here, because we are not modifying these variables. */
	uint8_t ch1_volume = t->a2dp.ch1_volume;
	uint8_t ch2_volume = t->a2dp.ch2_volume;

	double ch1_scale = 0;
	double ch2_scale = 0;

	if (!t->a2dp.ch1_muted)
		ch1_scale = pow(10, (-64 + 64.0 * ch1_volume / 127) / 20);
	if (!t->a2dp.ch2_muted)
		ch2_scale = pow(10, (-64 + 64.0 * ch2_volume / 127) / 20);

	snd_pcm_scale_s16le(buffer, samples, channels, ch1_scale, ch2_scale);
}

/**
 * Read PCM signal from the transport PCM FIFO. */
static ssize_t io_thread_read_pcm(struct ba_pcm *pcm, int16_t *buffer, size_t samples) {

	ssize_t ret;

	/* If the passed file descriptor is invalid (e.g. -1) is means, that other
	 * thread (the controller) has closed the connection. If the connection was
	 * closed during this call, we will still read correct data, because Linux
	 * kernel does not decrement file descriptor reference counter until the
	 * read returns. */
	while ((ret = read(pcm->fd, buffer, samples * sizeof(int16_t))) == -1 && errno == EINTR)
		continue;

	if (ret > 0)
		return ret / sizeof(int16_t);

	if (ret == 0)
		debug("FIFO endpoint has been closed: %d", pcm->fd);
	if (errno == EBADF)
		ret = 0;
	if (ret == 0)
		transport_release_pcm(pcm);

	return ret;
}

/**
 * Write PCM signal to the transport PCM FIFO. */
static ssize_t io_thread_write_pcm(struct ba_pcm *pcm, const int16_t *buffer, size_t samples) {

	const uint8_t *head = (uint8_t *)buffer;
	size_t len = samples * sizeof(int16_t);
	ssize_t ret;

	do {
		if ((ret = write(pcm->fd, head, len)) == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EPIPE) {
				/* This errno value will be received only, when the SIGPIPE
				 * signal is caught, blocked or ignored. */
				debug("FIFO endpoint has been closed: %d", pcm->fd);
				transport_release_pcm(pcm);
				return 0;
			}
			return ret;
		}
		head += ret;
		len -= ret;
	} while (len != 0);

	/* It is guaranteed, that this function will write data atomically. */
	return samples;
}

/**
 * Write data to the BT SEQPACKET socket. */
static ssize_t io_thread_write_bt(const struct ba_transport *t,
		const uint8_t *buffer, size_t len, int *coutq) {

	struct pollfd pfd = { t->bt_fd, POLLOUT, 0 };
	ssize_t ret;

	if (ioctl(pfd.fd, TIOCOUTQ, coutq) == -1)
		warn("Couldn't get BT queued bytes: %s", strerror(errno));
	else
		*coutq = abs(t->a2dp.bt_fd_coutq_init - *coutq);

retry:
	if ((ret = write(pfd.fd, buffer, len)) == -1)
		switch (errno) {
		case EINTR:
			goto retry;
		case EAGAIN:
			poll(&pfd, 1, -1);
			/* set coutq to some arbitrary big value */
			*coutq = 1024 * 16;
			goto retry;
		}

	return ret;
}

/**
 * Initialize RTP headers.
 *
 * @param s The memory area where the RTP headers will be initialized.
 * @param hdr The address where the pointer to the RTP header will be stored.
 * @param mhdr The address where the pointer to the RTP media payload header
 *   will be stored. This parameter might be NULL in order to omit RTP media
 *   payload header.
 * @return This function returns the address of the RTP payload region. */
static uint8_t *io_thread_init_rtp(void *s, rtp_header_t **hdr, rtp_media_header_t **mhdr) {

	rtp_header_t *header = *hdr = (rtp_header_t *)s;
	memset(header, 0, RTP_HEADER_LEN);
	header->paytype = 96;
	header->version = 2;
	header->seq_number = random();
	header->timestamp = random();

	uint8_t *data = (uint8_t *)&header->csrc[header->cc];

	if (mhdr != NULL) {
		memset(data, 0, sizeof(rtp_media_header_t));
		*mhdr = (rtp_media_header_t *)data;
		data += sizeof(rtp_media_header_t);
	}

	return data;
}

void *io_thread_a2dp_sink_sbc(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;

	/* Cancellation should be possible only in the carefully selected place
	 * in order to prevent memory leaks and resources not being released. */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	/* Lock transport during initialization stage. This lock will ensure,
	 * that no one will modify critical section until thread state can be
	 * known - initialization has failed or succeeded. */
	bool locked = !transport_pthread_cleanup_lock(t);

	if (t->bt_fd == -1) {
		error("Invalid BT socket: %d", t->bt_fd);
		goto fail_init;
	}

	/* Check for invalid (e.g. not set) reading MTU. If buffer allocation does
	 * not return NULL (allocating zero bytes might return NULL), we will read
	 * zero bytes from the BT socket, which will be wrongly identified as a
	 * "connection closed" action. */
	if (t->mtu_read <= 0) {
		error("Invalid reading MTU: %zu", t->mtu_read);
		goto fail_init;
	}

	sbc_t sbc;

	if ((errno = -sbc_init_a2dp(&sbc, 0, t->a2dp.cconfig, t->a2dp.cconfig_size)) != 0) {
		error("Couldn't initialize SBC codec: %s", strerror(errno));
		goto fail_init;
	}

	const unsigned int channels = transport_get_channels(t);

	ffb_uint8_t bt = { 0 };
	ffb_int16_t pcm = { 0 };
	pthread_cleanup_push(PTHREAD_CLEANUP(sbc_finish), &sbc);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_int16_free), &pcm);

	if (ffb_init(&pcm, sbc_get_codesize(&sbc)) == NULL ||
			ffb_init(&bt, t->mtu_read) == NULL) {
		error("Couldn't create data buffers: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	/* Lock transport during thread cancellation. This handler shall be at
	 * the top of the cleanup stack - lastly pushed. */
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup_lock), t);

	uint16_t seq_number = -1;

	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		{ -1, POLLIN, 0 },
	};

	transport_pthread_cleanup_unlock(t);
	locked = false;

	debug("Starting IO loop: %s (%s)",
			bluetooth_profile_to_string(t->profile),
			bluetooth_a2dp_codec_to_string(t->codec));
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		ssize_t len;

		/* add BT socket to the poll if transport is active */
		pfds[1].fd = t->state == TRANSPORT_ACTIVE ? t->bt_fd : -1;

		if (poll(pfds, ARRAYSIZE(pfds), -1) == -1) {
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		if (pfds[0].revents & POLLIN) {
			/* dispatch incoming event */
			enum ba_transport_signal sig = -1;
			if (read(pfds[0].fd, &sig, sizeof(sig)) != sizeof(sig))
				warn("Couldn't read signal: %s", strerror(errno));
			continue;
		}

		if ((len = read(pfds[1].fd, bt.tail, ffb_len_in(&bt))) == -1) {
			debug("BT read error: %s", strerror(errno));
			continue;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* it seems that zero is never returned... */
		if (len == 0) {
			debug("BT socket has been closed: %d", pfds[1].fd);
			/* Prevent sending the release request to the BlueZ. If the socket has
			 * been closed, it means that BlueZ has already closed the connection. */
			close(pfds[1].fd);
			t->bt_fd = -1;
			goto fail;
		}

		if (t->a2dp.pcm.fd == -1) {
			seq_number = -1;
			continue;
		}

		const rtp_header_t *rtp_header = (rtp_header_t *)bt.data;
		const rtp_media_header_t *rtp_media_header = (rtp_media_header_t *)&rtp_header->csrc[rtp_header->cc];
		const uint8_t *rtp_payload = (uint8_t *)(rtp_media_header + 1);
		size_t rtp_payload_len = len - ((void *)rtp_payload - (void *)rtp_header);

#if ENABLE_PAYLOADCHECK
		if (rtp_header->paytype < 96) {
			warn("Unsupported RTP payload type: %u", rtp_header->paytype);
			continue;
		}
#endif

		uint16_t _seq_number = ntohs(rtp_header->seq_number);
		if (++seq_number != _seq_number) {
			if (seq_number != 0)
				warn("Missing RTP packet: %u != %u", _seq_number, seq_number);
			seq_number = _seq_number;
		}

		/* decode retrieved SBC frames */
		size_t frames = rtp_media_header->frame_count;
		while (frames--) {

			ssize_t len;
			size_t decoded;

			if ((len = sbc_decode(&sbc, rtp_payload, rtp_payload_len,
							pcm.data, ffb_blen_in(&pcm), &decoded)) < 0) {
				error("SBC decoding error: %s", strerror(-len));
				break;
			}

			rtp_payload += len;
			rtp_payload_len -= len;

			const size_t samples = decoded / sizeof(int16_t);
			io_thread_scale_pcm(t, pcm.data, samples, channels);
			if (io_thread_write_pcm(&t->a2dp.pcm, pcm.data, samples) == -1)
				error("FIFO write error: %s", strerror(errno));

		}

	}

fail:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_pop(!locked);
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
fail_init:
	pthread_cleanup_pop(1);
	return NULL;
}

void *io_thread_a2dp_source_sbc(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	bool locked = !transport_pthread_cleanup_lock(t);

	sbc_t sbc;

	if ((errno = -sbc_init_a2dp(&sbc, 0, t->a2dp.cconfig, t->a2dp.cconfig_size)) != 0) {
		error("Couldn't initialize SBC codec: %s", strerror(errno));
		goto fail_init;
	}

	ffb_uint8_t bt = { 0 };
	ffb_int16_t pcm = { 0 };
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_int16_free), &pcm);
	pthread_cleanup_push(PTHREAD_CLEANUP(sbc_finish), &sbc);

	const size_t sbc_pcm_samples = sbc_get_codesize(&sbc) / sizeof(int16_t);
	const size_t sbc_frame_len = sbc_get_frame_length(&sbc);
	const unsigned int channels = transport_get_channels(t);
	const unsigned int samplerate = transport_get_sampling(t);

	/* Writing MTU should be big enough to contain RTP header, SBC payload
	 * header and at least one SBC frame. In general, there is no constraint
	 * for the MTU value, but the speed might suffer significantly. */
	const size_t mtu_write_payload = t->mtu_write - RTP_HEADER_LEN - sizeof(rtp_media_header_t);
	if (mtu_write_payload < sbc_frame_len) {
		warn("Writing MTU too small for one single SBC frame: %zu < %zu",
				t->mtu_write, RTP_HEADER_LEN + sizeof(rtp_media_header_t) + sbc_frame_len);
		t->mtu_write = RTP_HEADER_LEN + sizeof(rtp_media_header_t) + sbc_frame_len;
	}

	if (ffb_init(&pcm, sbc_pcm_samples * (mtu_write_payload / sbc_frame_len)) == NULL ||
			ffb_init(&bt, t->mtu_write) == NULL) {
		error("Couldn't create data buffers: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup_lock), t);

	rtp_header_t *rtp_header;
	rtp_media_header_t *rtp_media_header;

	/* initialize RTP headers and get anchor for payload */
	uint8_t *rtp_payload = io_thread_init_rtp(bt.data, &rtp_header, &rtp_media_header);
	uint16_t seq_number = ntohs(rtp_header->seq_number);
	uint32_t timestamp = ntohl(rtp_header->timestamp);

	/* array with historical data of queued bytes for BT socket */
	int coutq_history[IO_THREAD_COUTQ_HISTORY_SIZE] = { 0 };
	size_t coutq_i = 0;

	int poll_timeout = -1;
	struct asrsync asrs = { .frames = 0 };
	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		{ -1, POLLIN, 0 },
	};

	transport_pthread_cleanup_unlock(t);
	locked = false;

	debug("Starting IO loop: %s (%s)",
			bluetooth_profile_to_string(t->profile),
			bluetooth_a2dp_codec_to_string(t->codec));
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		ssize_t samples;

		/* add PCM socket to the poll if transport is active */
		pfds[1].fd = t->state == TRANSPORT_ACTIVE ? t->a2dp.pcm.fd : -1;

		switch (poll(pfds, ARRAYSIZE(pfds), poll_timeout)) {
		case 0:
			pthread_cond_signal(&t->a2dp.pcm.drained);
			poll_timeout = -1;
			locked = !transport_pthread_cleanup_lock(t);
			if (t->a2dp.pcm.fd == -1)
				goto final;
			transport_pthread_cleanup_unlock(t);
			locked = false;
			continue;
		case -1:
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		if (pfds[0].revents & POLLIN) {
			/* dispatch incoming event */
			enum ba_transport_signal sig = -1;
			if (read(pfds[0].fd, &sig, sizeof(sig)) != sizeof(sig))
				warn("Couldn't read signal: %s", strerror(errno));
			switch (sig) {
			case TRANSPORT_PCM_OPEN:
			case TRANSPORT_PCM_RESUME:
				poll_timeout = -1;
				asrs.frames = 0;
				break;
			case TRANSPORT_PCM_CLOSE:
				poll_timeout = config.a2dp.keep_alive * 1000;
				break;
			case TRANSPORT_PCM_SYNC:
				poll_timeout = 100;
				break;
			default:
				break;
			}
			continue;
		}

		/* read data from the FIFO - this function will block */
		if ((samples = io_thread_read_pcm(&t->a2dp.pcm, pcm.tail, ffb_len_in(&pcm))) <= 0) {
			if (samples == -1)
				error("FIFO read error: %s", strerror(errno));
			goto fail;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* When the thread is created, there might be no data in the FIFO. In fact
		 * there might be no data for a long time - until client starts playback.
		 * In order to correctly calculate time drift, the zero time point has to
		 * be obtained after the stream has started. */
		if (asrs.frames == 0)
			asrsync_init(&asrs, samplerate);

		if (!config.a2dp.volume)
			/* scale volume or mute audio signal */
			io_thread_scale_pcm(t, pcm.tail, samples, channels);

		/* get overall number of input samples */
		ffb_seek(&pcm, samples);
		samples = ffb_len_out(&pcm);

		/* anchor for RTP payload */
		bt.tail = rtp_payload;

		const int16_t *input = pcm.data;
		size_t input_len = samples;
		size_t output_len = ffb_len_in(&bt);
		size_t pcm_frames = 0;
		size_t sbc_frames = 0;

		/* Generate as many SBC frames as possible to fill the output buffer
		 * without overflowing it. The size of the output buffer is based on
		 * the socket MTU, so such a transfer should be most efficient. */
		while (input_len >= sbc_pcm_samples && output_len >= sbc_frame_len) {

			ssize_t len;
			ssize_t encoded;

			if ((len = sbc_encode(&sbc, input, input_len * sizeof(int16_t),
							bt.tail, output_len, &encoded)) < 0) {
				error("SBC encoding error: %s", strerror(-len));
				break;
			}

			len = len / sizeof(int16_t);
			input += len;
			input_len -= len;
			ffb_seek(&bt, encoded);
			output_len -= encoded;
			pcm_frames += len / channels;
			sbc_frames++;

		}

		rtp_header->seq_number = htons(++seq_number);
		rtp_header->timestamp = htonl(timestamp);
		rtp_media_header->frame_count = sbc_frames;

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		coutq_i = (coutq_i + 1) % ARRAYSIZE(coutq_history);
		if (io_thread_write_bt(t, bt.data, ffb_len_out(&bt), &coutq_history[coutq_i]) == -1) {
			if (errno == ECONNRESET || errno == ENOTCONN) {
				/* exit thread upon BT socket disconnection */
				debug("BT socket disconnected: %d", t->bt_fd);
				goto fail;
			}
			error("BT socket write error: %s", strerror(errno));
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* keep data transfer at a constant bit rate, also
		 * get a timestamp for the next RTP frame */
		asrsync_sync(&asrs, pcm_frames);
		timestamp += pcm_frames * 10000 / samplerate;

		/* update busy delay (encoding overhead) */
		t->delay = asrsync_get_busy_usec(&asrs) / 100;

		/* If the input buffer was not consumed (due to codesize limit), we
		 * have to append new data to the existing one. Since we do not use
		 * ring buffer, we will simply move unprocessed data to the front
		 * of our linear buffer. */
		ffb_shift(&pcm, samples - input_len);

	}

fail:
final:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_pop(!locked);
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
fail_init:
	pthread_cleanup_pop(1);
	return NULL;
}

#if ENABLE_AAC
void *io_thread_a2dp_sink_aac(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	bool locked = !transport_pthread_cleanup_lock(t);

	if (t->bt_fd == -1) {
		error("Invalid BT socket: %d", t->bt_fd);
		goto fail_open;
	}
	if (t->mtu_read <= 0) {
		error("Invalid reading MTU: %zu", t->mtu_read);
		goto fail_open;
	}

	HANDLE_AACDECODER handle;
	AAC_DECODER_ERROR err;

	if ((handle = aacDecoder_Open(TT_MP4_LATM_MCP1, 1)) == NULL) {
		error("Couldn't open AAC decoder");
		goto fail_open;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(aacDecoder_Close), handle);

	const unsigned int channels = transport_get_channels(t);
#ifdef AACDECODER_LIB_VL0
	if ((err = aacDecoder_SetParam(handle, AAC_PCM_MIN_OUTPUT_CHANNELS, channels)) != AAC_DEC_OK) {
		error("Couldn't set min output channels: %s", aacdec_strerror(err));
		goto fail_init;
	}
	if ((err = aacDecoder_SetParam(handle, AAC_PCM_MAX_OUTPUT_CHANNELS, channels)) != AAC_DEC_OK) {
		error("Couldn't set max output channels: %s", aacdec_strerror(err));
		goto fail_init;
	}
#else
	if ((err = aacDecoder_SetParam(handle, AAC_PCM_OUTPUT_CHANNELS, channels)) != AAC_DEC_OK) {
		error("Couldn't set output channels: %s", aacdec_strerror(err));
		goto fail_init;
	}
#endif

	ffb_uint8_t bt = { 0 };
	ffb_uint8_t latm = { 0 };
	ffb_int16_t pcm = { 0 };
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &latm);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_int16_free), &pcm);

	if (ffb_init(&pcm, 2048 * channels) == NULL ||
			ffb_init(&latm, t->mtu_read) == NULL ||
			ffb_init(&bt, t->mtu_read) == NULL) {
		error("Couldn't create data buffers: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup_lock), t);

	uint16_t seq_number = -1;
	int markbit_quirk = -3;

	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		{ -1, POLLIN, 0 },
	};

	transport_pthread_cleanup_unlock(t);
	locked = false;

	debug("Starting IO loop: %s (%s)",
			bluetooth_profile_to_string(t->profile),
			bluetooth_a2dp_codec_to_string(t->codec));
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		CStreamInfo *aacinf;
		ssize_t len;

		/* add BT socket to the poll if transport is active */
		pfds[1].fd = t->state == TRANSPORT_ACTIVE ? t->bt_fd : -1;

		if (poll(pfds, ARRAYSIZE(pfds), -1) == -1) {
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		if (pfds[0].revents & POLLIN) {
			/* dispatch incoming event */
			enum ba_transport_signal sig = -1;
			if (read(pfds[0].fd, &sig, sizeof(sig)) != sizeof(sig))
				warn("Couldn't read signal: %s", strerror(errno));
			continue;
		}

		if ((len = read(pfds[1].fd, bt.tail, ffb_len_in(&bt))) == -1) {
			debug("BT read error: %s", strerror(errno));
			continue;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* it seems that zero is never returned... */
		if (len == 0) {
			debug("BT socket has been closed: %d", pfds[1].fd);
			/* Prevent sending the release request to the BlueZ. If the socket has
			 * been closed, it means that BlueZ has already closed the connection. */
			close(pfds[1].fd);
			t->bt_fd = -1;
			goto fail;
		}

		if (t->a2dp.pcm.fd == -1) {
			seq_number = -1;
			continue;
		}

		const rtp_header_t *rtp_header = (rtp_header_t *)bt.data;
		uint8_t *rtp_latm = (uint8_t *)&rtp_header->csrc[rtp_header->cc];
		size_t rtp_latm_len = len - ((void *)rtp_latm - (void *)rtp_header);

#if ENABLE_PAYLOADCHECK
		if (rtp_header->paytype < 96) {
			warn("Unsupported RTP payload type: %u", rtp_header->paytype);
			continue;
		}
#endif

		/* If in the first N packets mark bit is not set, it might mean, that
		 * the mark bit will not be set at all. In such a case, activate mark
		 * bit quirk workaround. */
		if (markbit_quirk < 0) {
			if (rtp_header->markbit)
				markbit_quirk = 0;
			else if (++markbit_quirk == 0) {
				warn("Activating RTP mark bit quirk workaround");
				markbit_quirk = 1;
			}
		}

		uint16_t _seq_number = ntohs(rtp_header->seq_number);
		if (++seq_number != _seq_number) {
			if (seq_number != 0)
				warn("Missing RTP packet: %u != %u", _seq_number, seq_number);
			seq_number = _seq_number;
		}

		if (ffb_len_in(&latm) < rtp_latm_len) {
			debug("Resizing LATM buffer: %zd -> %zd", latm.size, latm.size + t->mtu_read);
			size_t prev_len = ffb_len_out(&latm);
			ffb_init(&latm, latm.size + t->mtu_read);
			ffb_seek(&latm, prev_len);
		}

		memcpy(latm.tail, rtp_latm, rtp_latm_len);
		ffb_seek(&latm, rtp_latm_len);

		if (markbit_quirk != 1 && !rtp_header->markbit) {
			debug("Fragmented RTP packet [%u]: LATM len: %zd", seq_number, rtp_latm_len);
			continue;
		}

		unsigned int data_len = ffb_len_out(&latm);
		unsigned int valid = ffb_len_out(&latm);

		if ((err = aacDecoder_Fill(handle, &latm.data, &data_len, &valid)) != AAC_DEC_OK)
			error("AAC buffer fill error: %s", aacdec_strerror(err));
		else if ((err = aacDecoder_DecodeFrame(handle, pcm.tail, ffb_blen_in(&pcm), 0)) != AAC_DEC_OK)
			error("AAC decode frame error: %s", aacdec_strerror(err));
		else if ((aacinf = aacDecoder_GetStreamInfo(handle)) == NULL)
			error("Couldn't get AAC stream info");
		else {
			const size_t samples = aacinf->frameSize * aacinf->numChannels;
			io_thread_scale_pcm(t, pcm.data, samples, channels);
			if (io_thread_write_pcm(&t->a2dp.pcm, pcm.data, samples) == -1)
				error("FIFO write error: %s", strerror(errno));
			ffb_rewind(&latm);
		}

	}

fail:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_pop(!locked);
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
fail_init:
	pthread_cleanup_pop(1);
fail_open:
	pthread_cleanup_pop(1);
	return NULL;
}
#endif

#if ENABLE_AAC
void *io_thread_a2dp_source_aac(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;
	const a2dp_aac_t *cconfig = (a2dp_aac_t *)t->a2dp.cconfig;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	bool locked = !transport_pthread_cleanup_lock(t);

	HANDLE_AACENCODER handle;
	AACENC_InfoStruct aacinf;
	AACENC_ERROR err;

	/* create AAC encoder without the Meta Data module */
	const unsigned int channels = transport_get_channels(t);
	if ((err = aacEncOpen(&handle, 0x07, channels)) != AACENC_OK) {
		error("Couldn't open AAC encoder: %s", aacenc_strerror(err));
		goto fail_open;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(aacEncClose), &handle);

	unsigned int aot = AOT_NONE;
	unsigned int bitrate = AAC_GET_BITRATE(*cconfig);
	unsigned int samplerate = transport_get_sampling(t);
	unsigned int channelmode = channels == 1 ? MODE_1 : MODE_2;

	switch (cconfig->object_type) {
	case AAC_OBJECT_TYPE_MPEG2_AAC_LC:
#if AACENCODER_LIB_VERSION <= 0x03040C00 /* 3.4.12 */
		aot = AOT_MP2_AAC_LC;
		break;
#endif
	case AAC_OBJECT_TYPE_MPEG4_AAC_LC:
		aot = AOT_AAC_LC;
		break;
	case AAC_OBJECT_TYPE_MPEG4_AAC_LTP:
		aot = AOT_AAC_LTP;
		break;
	case AAC_OBJECT_TYPE_MPEG4_AAC_SCA:
		aot = AOT_AAC_SCAL;
		break;
	}

	if ((err = aacEncoder_SetParam(handle, AACENC_AOT, aot)) != AACENC_OK) {
		error("Couldn't set audio object type: %s", aacenc_strerror(err));
		goto fail_init;
	}
	if ((err = aacEncoder_SetParam(handle, AACENC_BITRATE, bitrate)) != AACENC_OK) {
		error("Couldn't set bitrate: %s", aacenc_strerror(err));
		goto fail_init;
	}
	if ((err = aacEncoder_SetParam(handle, AACENC_SAMPLERATE, samplerate)) != AACENC_OK) {
		error("Couldn't set sampling rate: %s", aacenc_strerror(err));
		goto fail_init;
	}
	if ((err = aacEncoder_SetParam(handle, AACENC_CHANNELMODE, channelmode)) != AACENC_OK) {
		error("Couldn't set channel mode: %s", aacenc_strerror(err));
		goto fail_init;
	}
	if (cconfig->vbr) {
		if ((err = aacEncoder_SetParam(handle, AACENC_BITRATEMODE, config.aac_vbr_mode)) != AACENC_OK) {
			error("Couldn't set VBR bitrate mode %u: %s", config.aac_vbr_mode, aacenc_strerror(err));
			goto fail_init;
		}
	}
	if ((err = aacEncoder_SetParam(handle, AACENC_AFTERBURNER, config.aac_afterburner)) != AACENC_OK) {
		error("Couldn't enable afterburner: %s", aacenc_strerror(err));
		goto fail_init;
	}
	if ((err = aacEncoder_SetParam(handle, AACENC_TRANSMUX, TT_MP4_LATM_MCP1)) != AACENC_OK) {
		error("Couldn't enable LATM transport type: %s", aacenc_strerror(err));
		goto fail_init;
	}
	if ((err = aacEncoder_SetParam(handle, AACENC_HEADER_PERIOD, 1)) != AACENC_OK) {
		error("Couldn't set LATM header period: %s", aacenc_strerror(err));
		goto fail_init;
	}

	if ((err = aacEncEncode(handle, NULL, NULL, NULL, NULL)) != AACENC_OK) {
		error("Couldn't initialize AAC encoder: %s", aacenc_strerror(err));
		goto fail_init;
	}
	if ((err = aacEncInfo(handle, &aacinf)) != AACENC_OK) {
		error("Couldn't get encoder info: %s", aacenc_strerror(err));
		goto fail_init;
	}

	ffb_uint8_t bt = { 0 };
	ffb_int16_t pcm = { 0 };
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_int16_free), &pcm);

	if (ffb_init(&pcm, aacinf.inputChannels * aacinf.frameLength) == NULL ||
			ffb_init(&bt, RTP_HEADER_LEN + aacinf.maxOutBufBytes) == NULL) {
		error("Couldn't create data buffers: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup_lock), t);

	rtp_header_t *rtp_header;

	/* initialize RTP header and get anchor for payload */
	uint8_t *rtp_payload = io_thread_init_rtp(bt.data, &rtp_header, NULL);
	uint16_t seq_number = ntohs(rtp_header->seq_number);
	uint32_t timestamp = ntohl(rtp_header->timestamp);

	int in_bufferIdentifiers[] = { IN_AUDIO_DATA };
	int out_bufferIdentifiers[] = { OUT_BITSTREAM_DATA };
	int in_bufSizes[] = { pcm.size * sizeof(*pcm.data) };
	int out_bufSizes[] = { aacinf.maxOutBufBytes };
	int in_bufElSizes[] = { sizeof(*pcm.data) };
	int out_bufElSizes[] = { sizeof(*bt.data) };

	AACENC_BufDesc in_buf = {
		.numBufs = 1,
		.bufs = (void **)&pcm.data,
		.bufferIdentifiers = in_bufferIdentifiers,
		.bufSizes = in_bufSizes,
		.bufElSizes = in_bufElSizes,
	};
	AACENC_BufDesc out_buf = {
		.numBufs = 1,
		.bufs = (void **)&rtp_payload,
		.bufferIdentifiers = out_bufferIdentifiers,
		.bufSizes = out_bufSizes,
		.bufElSizes = out_bufElSizes,
	};
	AACENC_InArgs in_args = { 0 };
	AACENC_OutArgs out_args = { 0 };

	/* array with historical data of queued bytes for BT socket */
	int coutq_history[IO_THREAD_COUTQ_HISTORY_SIZE] = { 0 };
	size_t coutq_i = 0;

	int poll_timeout = -1;
	struct asrsync asrs = { .frames = 0 };
	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		{ -1, POLLIN, 0 },
	};

	transport_pthread_cleanup_unlock(t);
	locked = false;

	debug("Starting IO loop: %s (%s)",
			bluetooth_profile_to_string(t->profile),
			bluetooth_a2dp_codec_to_string(t->codec));
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		ssize_t samples;

		/* add PCM socket to the poll if transport is active */
		pfds[1].fd = t->state == TRANSPORT_ACTIVE ? t->a2dp.pcm.fd : -1;

		switch (poll(pfds, ARRAYSIZE(pfds), poll_timeout)) {
		case 0:
			pthread_cond_signal(&t->a2dp.pcm.drained);
			poll_timeout = -1;
			locked = !transport_pthread_cleanup_lock(t);
			if (t->a2dp.pcm.fd == -1)
				goto final;
			transport_pthread_cleanup_unlock(t);
			locked = false;
			continue;
		case -1:
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		if (pfds[0].revents & POLLIN) {
			/* dispatch incoming event */
			enum ba_transport_signal sig = -1;
			if (read(pfds[0].fd, &sig, sizeof(sig)) != sizeof(sig))
				warn("Couldn't read signal: %s", strerror(errno));
			switch (sig) {
			case TRANSPORT_PCM_OPEN:
			case TRANSPORT_PCM_RESUME:
				poll_timeout = -1;
				asrs.frames = 0;
				break;
			case TRANSPORT_PCM_CLOSE:
				poll_timeout = config.a2dp.keep_alive * 1000;
				break;
			case TRANSPORT_PCM_SYNC:
				poll_timeout = 100;
				break;
			default:
				break;
			}
			continue;
		}

		/* read data from the FIFO - this function will block */
		if ((samples = io_thread_read_pcm(&t->a2dp.pcm, pcm.tail, ffb_len_in(&pcm))) <= 0) {
			if (samples == -1)
				error("FIFO read error: %s", strerror(errno));
			goto fail;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (asrs.frames == 0)
			asrsync_init(&asrs, samplerate);

		if (!config.a2dp.volume)
			/* scale volume or mute audio signal */
			io_thread_scale_pcm(t, pcm.tail, samples, channels);

		/* move tail pointer */
		ffb_seek(&pcm, samples);

		while ((in_args.numInSamples = ffb_len_out(&pcm)) > 0) {

			if ((err = aacEncEncode(handle, &in_buf, &out_buf, &in_args, &out_args)) != AACENC_OK)
				error("AAC encoding error: %s", aacenc_strerror(err));

			if (out_args.numOutBytes > 0) {

				size_t payload_len_max = t->mtu_write - RTP_HEADER_LEN;
				size_t payload_len = out_args.numOutBytes;
				rtp_header->timestamp = htonl(timestamp);

				/* If the size of the RTP packet exceeds writing MTU, the RTP payload
				 * should be fragmented. According to the RFC 3016, fragmentation of
				 * the audioMuxElement requires no extra header - the payload should
				 * be fragmented and spread across multiple RTP packets. */
				for (;;) {

					ssize_t ret;
					size_t len;

					len = payload_len > payload_len_max ? payload_len_max : payload_len;
					rtp_header->markbit = payload_len <= payload_len_max;
					rtp_header->seq_number = htons(++seq_number);

					pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

					coutq_i = (coutq_i + 1) % ARRAYSIZE(coutq_history);
					if ((ret = io_thread_write_bt(t, bt.data, RTP_HEADER_LEN + len, &coutq_history[coutq_i])) == -1) {
						if (errno == ECONNRESET || errno == ENOTCONN) {
							/* exit thread upon BT socket disconnection */
							debug("BT socket disconnected: %d", t->bt_fd);
							goto fail;
						}
						error("BT socket write error: %s", strerror(errno));
						break;
					}

					pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

					/* account written payload only */
					ret -= RTP_HEADER_LEN;

					/* break if the last part of the payload has been written */
					if ((payload_len -= ret) == 0)
						break;

					/* move rest of data to the beginning of the payload */
					debug("Payload fragmentation: extra %zd bytes", payload_len);
					memmove(rtp_payload, rtp_payload + ret, payload_len);

				}

			}

			/* keep data transfer at a constant bit rate, also
			 * get a timestamp for the next RTP frame */
			unsigned int frames = out_args.numInSamples / channels;
			asrsync_sync(&asrs, frames);
			timestamp += frames * 10000 / samplerate;

			/* update busy delay (encoding overhead) */
			t->delay = asrsync_get_busy_usec(&asrs) / 100;

			/* If the input buffer was not consumed, we have to append new data to
			 * the existing one. Since we do not use ring buffer, we will simply
			 * move unprocessed data to the front of our linear buffer. */
			ffb_shift(&pcm, out_args.numInSamples);

		}

	}

fail:
final:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_pop(!locked);
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
fail_init:
	pthread_cleanup_pop(1);
fail_open:
	pthread_cleanup_pop(1);
	return NULL;
}
#endif

#if ENABLE_APTX
void *io_thread_a2dp_source_aptx(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	bool locked = !transport_pthread_cleanup_lock(t);

	APTXENC handle = malloc(SizeofAptxbtenc());
	pthread_cleanup_push(PTHREAD_CLEANUP(free), handle);

	if (handle == NULL || aptxbtenc_init(handle, __BYTE_ORDER == __LITTLE_ENDIAN) != 0) {
		error("Couldn't initialize apt-X encoder: %s", strerror(errno));
		goto fail_init;
	}

	ffb_uint8_t bt = { 0 };
	ffb_int16_t pcm = { 0 };
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_int16_free), &pcm);

	const unsigned int channels = transport_get_channels(t);
	const size_t aptx_pcm_samples = 4 * channels;
	const size_t aptx_code_len = 2 * sizeof(uint16_t);
	const size_t mtu_write = t->mtu_write;

	if (ffb_init(&pcm, aptx_pcm_samples * (mtu_write / aptx_code_len)) == NULL ||
			ffb_init(&bt, mtu_write) == NULL) {
		error("Couldn't create data buffers: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup_lock), t);

	/* array with historical data of queued bytes for BT socket */
	int coutq_history[IO_THREAD_COUTQ_HISTORY_SIZE] = { 0 };
	size_t coutq_i = 0;

	int poll_timeout = -1;
	struct asrsync asrs = { .frames = 0 };
	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		{ -1, POLLIN, 0 },
	};

	transport_pthread_cleanup_unlock(t);
	locked = false;

	debug("Starting IO loop: %s (%s)",
			bluetooth_profile_to_string(t->profile),
			bluetooth_a2dp_codec_to_string(t->codec));
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		ssize_t samples;

		/* add PCM socket to the poll if transport is active */
		pfds[1].fd = t->state == TRANSPORT_ACTIVE ? t->a2dp.pcm.fd : -1;

		switch (poll(pfds, ARRAYSIZE(pfds), poll_timeout)) {
		case 0:
			pthread_cond_signal(&t->a2dp.pcm.drained);
			poll_timeout = -1;
			locked = !transport_pthread_cleanup_lock(t);
			if (t->a2dp.pcm.fd == -1)
				goto final;
			transport_pthread_cleanup_unlock(t);
			locked = false;
			continue;
		case -1:
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		if (pfds[0].revents & POLLIN) {
			/* dispatch incoming event */
			enum ba_transport_signal sig = -1;
			if (read(pfds[0].fd, &sig, sizeof(sig)) != sizeof(sig))
				warn("Couldn't read signal: %s", strerror(errno));
			switch (sig) {
			case TRANSPORT_PCM_OPEN:
			case TRANSPORT_PCM_RESUME:
				poll_timeout = -1;
				asrs.frames = 0;
				break;
			case TRANSPORT_PCM_CLOSE:
				poll_timeout = config.a2dp.keep_alive * 1000;
				break;
			case TRANSPORT_PCM_SYNC:
				poll_timeout = 100;
				break;
			default:
				break;
			}
			continue;
		}

		/* read data from the FIFO - this function will block */
		if ((samples = io_thread_read_pcm(&t->a2dp.pcm, pcm.tail, ffb_len_in(&pcm))) <= 0) {
			if (samples == -1)
				error("FIFO read error: %s", strerror(errno));
			goto fail;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (asrs.frames == 0)
			asrsync_init(&asrs, transport_get_sampling(t));

		if (!config.a2dp.volume)
			/* scale volume or mute audio signal */
			io_thread_scale_pcm(t, pcm.tail, samples, channels);

		/* get overall number of input samples */
		ffb_seek(&pcm, samples);
		samples = ffb_len_out(&pcm);

		int16_t *input = pcm.data;
		size_t input_len = samples;

		/* encode and transfer obtained data */
		while (input_len >= aptx_pcm_samples) {

			size_t output_len = ffb_len_in(&bt);
			size_t pcm_frames = 0;

			/* Generate as many apt-X frames as possible to fill the output buffer
			 * without overflowing it. The size of the output buffer is based on
			 * the socket MTU, so such a transfer should be most efficient. */
			while (input_len >= aptx_pcm_samples && output_len >= aptx_code_len) {

				int32_t pcm_l[4];
				int32_t pcm_r[4];
				size_t i;

				for (i = 0; i < 4; i++) {
					pcm_l[i] = input[2 * i];
					pcm_r[i] = input[2 * i + 1];
				}

				if (aptxbtenc_encodestereo(handle, pcm_l, pcm_r, (uint16_t *)bt.tail) != 0) {
					error("Apt-X encoding error: %s", strerror(errno));
					break;
				}

				input += 4 * channels;
				input_len -= 4 * channels;
				ffb_seek(&bt, aptx_code_len);
				output_len -= aptx_code_len;
				pcm_frames += 4;

			}

			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

			coutq_i = (coutq_i + 1) % ARRAYSIZE(coutq_history);
			if (io_thread_write_bt(t, bt.data, ffb_len_out(&bt), &coutq_history[coutq_i]) == -1) {
				if (errno == ECONNRESET || errno == ENOTCONN) {
					/* exit thread upon BT socket disconnection */
					debug("BT socket disconnected: %d", t->bt_fd);
					goto fail;
				}
				error("BT socket write error: %s", strerror(errno));
			}

			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			/* keep data transfer at a constant bit rate */
			asrsync_sync(&asrs, pcm_frames);

			/* update busy delay (encoding overhead) */
			t->delay = asrsync_get_busy_usec(&asrs) / 100;

			/* reinitialize output buffer */
			ffb_rewind(&bt);

		}

		/* If the input buffer was not consumed (due to codesize limit), we
		 * have to append new data to the existing one. Since we do not use
		 * ring buffer, we will simply move unprocessed data to the front
		 * of our linear buffer. */
		ffb_shift(&pcm, samples - input_len);

	}

fail:
final:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_pop(!locked);
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
fail_init:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	return NULL;
}
#endif

#if ENABLE_LDAC
void *io_thread_a2dp_source_ldac(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;
	const a2dp_ldac_t *cconfig = (a2dp_ldac_t *)t->a2dp.cconfig;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	bool locked = !transport_pthread_cleanup_lock(t);

	HANDLE_LDAC_BT handle;
	HANDLE_LDAC_ABR handle_abr;

	if ((handle = ldacBT_get_handle()) == NULL) {
		error("Couldn't open LDAC encoder: %s", strerror(errno));
		goto fail_open_ldac;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(ldacBT_free_handle), handle);

	if ((handle_abr = ldac_ABR_get_handle()) == NULL) {
		error("Couldn't open LDAC ABR: %s", strerror(errno));
		goto fail_open_ldac_abr;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(ldac_ABR_free_handle), handle_abr);

	const unsigned int channels = transport_get_channels(t);
	const unsigned int samplerate = transport_get_sampling(t);
	const size_t ldac_pcm_samples = LDACBT_ENC_LSU * channels;

	if (ldacBT_init_handle_encode(handle, t->mtu_write - RTP_HEADER_LEN - sizeof(rtp_media_header_t),
				config.ldac_eqmid, cconfig->channel_mode, LDACBT_SMPL_FMT_S16, samplerate) == -1) {
		error("Couldn't initialize LDAC encoder: %s", ldacBT_strerror(ldacBT_get_error_code(handle)));
		goto fail_init;
	}

	if (ldac_ABR_Init(handle_abr, 1000 * ldac_pcm_samples / channels / samplerate) == -1) {
		error("Couldn't initialize LDAC ABR");
		goto fail_init;
	}
	if (ldac_ABR_set_thresholds(handle_abr, 6, 4, 2) == -1) {
		error("Couldn't set LDAC ABR thresholds");
		goto fail_init;
	}

	ffb_uint8_t bt = { 0 };
	ffb_int16_t pcm = { 0 };
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_int16_free), &pcm);

	if (ffb_init(&pcm, ldac_pcm_samples) == NULL ||
			ffb_init(&bt, t->mtu_write) == NULL) {
		error("Couldn't create data buffers: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup_lock), t);

	rtp_header_t *rtp_header;
	rtp_media_header_t *rtp_media_header;

	/* initialize RTP headers and get anchor for payload */
	bt.tail = io_thread_init_rtp(bt.data, &rtp_header, &rtp_media_header);
	uint16_t seq_number = ntohs(rtp_header->seq_number);
	uint32_t timestamp = ntohl(rtp_header->timestamp);
	size_t ts_frames = 0;

	/* number of queued bytes in the BT socket */
	int coutq = 0;

	int poll_timeout = -1;
	struct asrsync asrs = { .frames = 0 };
	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		{ -1, POLLIN, 0 },
	};

	transport_pthread_cleanup_unlock(t);
	locked = false;

	debug("Starting IO loop: %s (%s)",
			bluetooth_profile_to_string(t->profile),
			bluetooth_a2dp_codec_to_string(t->codec));
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		ssize_t samples;

		/* add PCM socket to the poll if transport is active */
		pfds[1].fd = t->state == TRANSPORT_ACTIVE ? t->a2dp.pcm.fd : -1;

		switch (poll(pfds, ARRAYSIZE(pfds), poll_timeout)) {
		case 0:
			pthread_cond_signal(&t->a2dp.pcm.drained);
			poll_timeout = -1;
			locked = !transport_pthread_cleanup_lock(t);
			if (t->a2dp.pcm.fd == -1)
				goto final;
			transport_pthread_cleanup_unlock(t);
			locked = false;
			continue;
		case -1:
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		if (pfds[0].revents & POLLIN) {
			/* dispatch incoming event */
			enum ba_transport_signal sig = -1;
			if (read(pfds[0].fd, &sig, sizeof(sig)) != sizeof(sig))
				warn("Couldn't read signal: %s", strerror(errno));
			switch (sig) {
			case TRANSPORT_PCM_OPEN:
			case TRANSPORT_PCM_RESUME:
				poll_timeout = -1;
				asrs.frames = 0;
				break;
			case TRANSPORT_PCM_CLOSE:
				poll_timeout = config.a2dp.keep_alive * 1000;
				break;
			case TRANSPORT_PCM_SYNC:
				poll_timeout = 100;
				break;
			default:
				break;
			}
			continue;
		}

		/* read data from the FIFO - this function will block */
		if ((samples = io_thread_read_pcm(&t->a2dp.pcm, pcm.tail, ffb_len_in(&pcm))) <= 0) {
			if (samples == -1)
				error("FIFO read error: %s", strerror(errno));
			goto fail;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (asrs.frames == 0)
			asrsync_init(&asrs, samplerate);

		if (!config.a2dp.volume)
			/* scale volume or mute audio signal */
			io_thread_scale_pcm(t, pcm.tail, samples, channels);

		/* get overall number of input samples */
		ffb_seek(&pcm, samples);
		samples = ffb_len_out(&pcm);

		int16_t *input = pcm.data;
		size_t input_len = samples;

		/* encode and transfer obtained data */
		while (input_len >= ldac_pcm_samples) {

			int len;
			int encoded;
			int frames;

			if (ldacBT_encode(handle, input, &len, bt.tail, &encoded, &frames) != 0) {
				error("LDAC encoding error: %s", ldacBT_strerror(ldacBT_get_error_code(handle)));
				break;
			}

			rtp_media_header->frame_count = frames;

			frames = len / sizeof(int16_t);
			input += frames;
			input_len -= frames;

			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

			if (encoded &&
					io_thread_write_bt(t, bt.data, ffb_len_out(&bt) + encoded, &coutq) == -1) {
				if (errno == ECONNRESET || errno == ENOTCONN) {
					/* exit thread upon BT socket disconnection */
					debug("BT socket disconnected: %d", t->bt_fd);
					goto fail;
				}
				error("BT socket write error: %s", strerror(errno));
			}

			if (config.ldac_abr)
				ldac_ABR_Proc(handle, handle_abr, coutq / t->mtu_write, 1);

			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			/* keep data transfer at a constant bit rate */
			asrsync_sync(&asrs, frames / channels);
			ts_frames += frames;

			/* update busy delay (encoding overhead) */
			t->delay = asrsync_get_busy_usec(&asrs) / 100;

			if (encoded) {
				timestamp += ts_frames / channels * 10000 / samplerate;
				rtp_header->timestamp = htonl(timestamp);
				rtp_header->seq_number = htons(++seq_number);
				ts_frames = 0;
			}

		}

		/* If the input buffer was not consumed (due to codesize limit), we
		 * have to append new data to the existing one. Since we do not use
		 * ring buffer, we will simply move unprocessed data to the front
		 * of our linear buffer. */
		ffb_shift(&pcm, samples - input_len);

	}

fail:
final:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_pop(!locked);
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
fail_init:
	pthread_cleanup_pop(1);
fail_open_ldac_abr:
	pthread_cleanup_pop(1);
fail_open_ldac:
	pthread_cleanup_pop(1);
	return NULL;
}
#endif

static void close_lsocket(struct ba_transport *t)
{
	if (t->sco.listen_fd > 0) {
		close(t->sco.listen_fd);
		t->sco.listen_fd = -1;
	}
}

static int bind_sco(struct ba_transport *t, int sock)
{
	struct sockaddr_sco addr;
	struct hci_dev_info di;

	if (hci_devinfo(t->device->hci_dev_id, &di) == -1) {
		error("Couldn't get HCI device info: %s", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, &di.bdaddr);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		error("Couldn't bind sco socket");
		return -1;
	}

	return 0;
}

static int timeout_set(int fd, unsigned int msec)
{
	struct itimerspec itimer;
	unsigned int sec = msec / 1000;

	memset(&itimer, 0, sizeof(itimer));
	itimer.it_interval.tv_sec = 0;
	itimer.it_interval.tv_nsec = 0;
	itimer.it_value.tv_sec = sec;
	itimer.it_value.tv_nsec = (msec - (sec * 1000)) * 1000 * 1000;

	return timerfd_settime(fd, 0, &itimer, NULL);
}

static void close_timerfd(void *data)
{
	int fd = (int)data;

	close(fd);
}

void *io_thread_sco(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;
	int defer = 1;
	int sco_timer;
	int timer_added = 0;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	/* buffers for transferring data to and fro SCO socket */
	ffb_uint8_t bt_in = { 0 };
	ffb_uint8_t bt_out = { 0 };
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt_in);
	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt_out);

	/* these buffers shall be bigger than the SCO MTU */
	if (ffb_init(&bt_in, 128) == NULL ||
			ffb_init(&bt_out, 128) == NULL) {
		error("Couldn't create data buffer: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	t->sco.listen_fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO);
	if (t->sco.listen_fd == -1) {
		error("Couldn't open sco socket: %s", strerror(errno));
		goto fail_ffb;
	}
	pthread_cleanup_push(PTHREAD_CLEANUP(close_lsocket), t);
	if (bind_sco(t, t->sco.listen_fd) < 0) {
		error("Couldn't bind sco socket");
		goto fail_sock;
	}
	if (setsockopt(t->sco.listen_fd, SOL_BLUETOOTH, BT_DEFER_SETUP,
		       &defer, sizeof(defer)) < 0) {
		error("Couldn't set defer for sco");
		/* Ignore this error */
	}
	if (listen(t->sco.listen_fd, 1) < 0) {
		error("Couldn't listen %s", strerror(errno));
		goto fail_sock;
	}

	sco_timer = timerfd_create(CLOCK_REALTIME, 0);
	if (sco_timer == -1) {
		error("Couldn't create sco timer %s", strerror(errno));
		goto fail_sock;
	}
	pthread_cleanup_push(PTHREAD_CLEANUP(close_timerfd), (void *)sco_timer);

	int poll_timeout = -1;
	struct asrsync asrs = { .frames = 0 };
	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		/* SCO socket */
		{ -1, POLLIN, 0 },
		{ -1, POLLOUT, 0 },
		/* PCM FIFO */
		{ -1, POLLIN, 0 },
		{ -1, POLLOUT, 0 },
		{ t->sco.listen_fd, POLLIN, 0 },
		{ sco_timer, POLLIN, 0 }, /* [6] for sco timer */
	};

	debug("Starting IO loop: %s",
			bluetooth_profile_to_string(t->profile));
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		/* fresh-start for file descriptors polling */
		pfds[1].fd = pfds[2].fd = -1;
		pfds[3].fd = pfds[4].fd = -1;

		switch (t->codec) {
		case HFP_CODEC_CVSD:
		default:
			if (t->mtu_read > 0 && ffb_len_in(&bt_in) >= t->mtu_read)
				pfds[1].fd = t->bt_fd;
			if (t->mtu_write > 0 && ffb_len_out(&bt_out) >= t->mtu_write)
				pfds[2].fd = t->bt_fd;
			if (t->mtu_write > 0 && ffb_len_in(&bt_out) >= t->mtu_write)
				pfds[3].fd = t->sco.spk_pcm.fd;
			if (ffb_len_out(&bt_in) > 0)
				pfds[4].fd = t->sco.mic_pcm.fd;
		}

		if (t->sco.mic_pcm.fd == -1)
			pfds[1].fd = -1;

		switch (poll(pfds, ARRAYSIZE(pfds), poll_timeout)) {
		case 0:
			pthread_cond_signal(&t->sco.spk_pcm.drained);
			poll_timeout = -1;
			continue;
		case -1:
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (pfds[0].revents & POLLIN) {
			/* dispatch incoming event */

			enum ba_transport_signal sig = -1;
			if (read(pfds[0].fd, &sig, sizeof(sig)) != sizeof(sig))
				warn("Couldn't read signal: %s", strerror(errno));

			/* FIXME: Drain functionality for speaker.
			 * XXX: Right now it is not possible to drain speaker PCM (in a clean
			 *      fashion), because poll() will not timeout if we've got incoming
			 *      data from the microphone (BT SCO socket). In order not to hang
			 *      forever in the transport_drain_pcm() function, we will signal
			 *      PCM drain right now. */
			if (sig == TRANSPORT_PCM_SYNC)
				pthread_cond_signal(&t->sco.spk_pcm.drained);

			/* Received TRANSPORT_PCM_OPEN from client or rfcomm
			 * thread (callsetup=0/1, call=1).
			 * So at least three open events will be received when
			 * answer a call
			 */
			const enum hfp_ind *inds = t->sco.rfcomm->rfcomm.hfp_inds;
			if (sig == TRANSPORT_PCM_OPEN) {
				info("Received transport pcm open event");
			}

			if (sig == TRANSPORT_PCM_CLOSE) {
				info("Received transport pcm close event");
				/* Don't disconnect SCO link positively */
				/* transport_release_bt_sco(t); */
				asrs.frames = 0;
			}

			continue;
		}

		if (asrs.frames == 0)
			asrsync_init(&asrs, transport_get_sampling(t));

		/* t->bt_fd */
		if (pfds[1].revents & POLLIN) {
			/* dispatch incoming SCO data */

			uint8_t *buffer;
			size_t buffer_len;
			ssize_t len;

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				buffer = bt_in.tail;
				buffer_len = ffb_len_in(&bt_in);
			}

retry_sco_read:
			errno = 0;
			if ((len = read(pfds[1].fd, buffer, buffer_len)) <= 0)
				switch (errno) {
				case EINTR:
					goto retry_sco_read;
				case 0:
				case ECONNABORTED:
				case ECONNRESET:
					transport_release_bt_sco(t);
					continue;
				default:
					error("SCO read error: %s", strerror(errno));
					continue;
				}

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				ffb_seek(&bt_in, len);
			}

		}
		else if (pfds[1].revents & (POLLERR | POLLHUP)) {
			debug("SCO poll error status: %#x", pfds[1].revents);
			transport_release_bt_sco(t);
		}

		/* t->bt_fd */
		if (pfds[2].revents & POLLOUT) {
			/* write-out SCO data */

			uint8_t *buffer;
			size_t buffer_len;
			ssize_t len;

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				buffer = bt_out.data;
				buffer_len = t->mtu_write;
			}

retry_sco_write:
			errno = 0;
			if ((len = write(pfds[2].fd, buffer, buffer_len)) <= 0)
				switch (errno) {
				case EINTR:
					goto retry_sco_write;
				case 0:
				case ECONNABORTED:
				case ECONNRESET:
					transport_release_bt_sco(t);
					continue;
				default:
					error("SCO write error: %s", strerror(errno));
					continue;
				}

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				ffb_shift(&bt_out, len);
			}

		}

		/* t->sco.spk_pcm.fd */
		if (pfds[3].revents & POLLIN) {
			/* dispatch incoming PCM data */

			int16_t *buffer;
			ssize_t samples;

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				buffer = (int16_t *)bt_out.tail;
				samples = ffb_len_in(&bt_out) / sizeof(int16_t);
			}

			/* read data from the FIFO - this function will block */
			if ((samples = io_thread_read_pcm(&t->sco.spk_pcm, buffer, samples)) <= 0) {
				if (samples == -1)
					error("FIFO read error: %s", strerror(errno));
				continue;
			}

			if (t->sco.spk_muted)
				snd_pcm_scale_s16le(buffer, samples, 1, 0, 0);

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				ffb_seek(&bt_out, samples * sizeof(int16_t));
			}

		}
		else if (pfds[3].revents & (POLLERR | POLLHUP)) {
			debug("PCM poll error status: %#x", pfds[3].revents);
			close(t->sco.spk_pcm.fd);
			t->sco.spk_pcm.fd = -1;
		}

		/* t->sco.mic_pcm.fd */
		if (pfds[4].revents & POLLOUT) {
			/* write-out PCM data */

			int16_t *buffer;
			ssize_t samples;

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				buffer = (int16_t *)bt_in.data;
				samples = ffb_len_out(&bt_in) / sizeof(int16_t);
			}

			if (t->sco.mic_muted)
				snd_pcm_scale_s16le(buffer, samples, 1, 0, 0);

			if (io_thread_write_pcm(&t->sco.mic_pcm, buffer, samples) == -1)
				error("FIFO write error: %s", strerror(errno));

			switch (t->codec) {
			case HFP_CODEC_CVSD:
			default:
				ffb_shift(&bt_in, samples * sizeof(int16_t));
			}

		}

		/* sco_timer */
		if (pfds[6].revents & POLLIN) {
			bool release = false;
			const enum hfp_ind *inds = t->sco.rfcomm->rfcomm.hfp_inds;

			info("SCO timer expired");

			if (t->bt_fd != -1) {
				warn("Timer expired but sco link was created");
				continue;
			}

			/* TODO: Should  we need to check client */
			/* if (t->sco.spk_pcm.fd == -1 && t->sco.mic_pcm.fd == -1)
			 * 	release = true;
			 */

			if (t->profile == BLUETOOTH_PROFILE_HFP_HF &&
			    inds[HFP_IND_CALL] == HFP_IND_CALL_ACTIVE)
				transport_acquire_bt_sco(t);

			continue;
		}

		/* t->sco.listen_fd */
		if (pfds[5].revents & POLLIN) {
			int sock;
			struct sockaddr_sco addr;
			socklen_t len = sizeof(addr);
			const enum hfp_ind *inds = t->sco.rfcomm->rfcomm.hfp_inds;
			bool release = false;

			info("SCO connection created by peer");
			sock = accept(pfds[5].fd, (struct sockaddr *)&addr,
				      &len);
			if (sock < 0) {
				error("Couldn't accept sco connection");
				continue;
			}

			transport_acquire_bt_sco2(t, sock);
			/* Kill timer that is used to create sco link */
			timeout_set(sco_timer, 0);

			continue;
		}

		/* keep data transfer at a constant bit rate */
		asrsync_sync(&asrs, 48 / 2);
		/* update busy delay (encoding overhead) */
		t->delay = asrsync_get_busy_usec(&asrs) / 100;

	}

fail:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_pop(1); /* for sco timer */
fail_sock:
	pthread_cleanup_pop(1); /* for sco listen sock */
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	return NULL;
}

#if DEBUG
/**
 * Dump incoming BT data to a file. */
void *io_thread_a2dp_sink_dump(void *arg) {
	struct ba_transport *t = (struct ba_transport *)arg;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(PTHREAD_CLEANUP(transport_pthread_cleanup), t);

	ffb_uint8_t bt = { 0 };
	FILE *f = NULL;
	char fname[64];
	char *ptr;

	sprintf(fname, "/tmp/ba-%s-%s.dump",
			bluetooth_profile_to_string(t->profile),
			bluetooth_a2dp_codec_to_string(t->codec));
	for (ptr = fname; *ptr != '\0'; ptr++) {
		*ptr = tolower(*ptr);
		if (*ptr == ' ')
			*ptr = '-';
	}

	debug("Opening BT dump file: %s", fname);
	if ((f = fopen(fname, "wb")) == NULL) {
		error("Couldn't create dump file: %s", strerror(errno));
		goto fail_open;
	}

	pthread_cleanup_push(PTHREAD_CLEANUP(ffb_uint8_free), &bt);
	pthread_cleanup_push(PTHREAD_CLEANUP(fclose), f);

	if (ffb_init(&bt, t->mtu_read) == NULL) {
		error("Couldn't create data buffer: %s", strerror(ENOMEM));
		goto fail_ffb;
	}

	struct pollfd pfds[] = {
		{ t->sig_fd[0], POLLIN, 0 },
		{ t->bt_fd, POLLIN, 0 },
	};

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		ssize_t len;

		if (poll(pfds, ARRAYSIZE(pfds), -1) == -1) {
			if (errno == EINTR)
				continue;
			error("Transport poll error: %s", strerror(errno));
			goto fail;
		}

		if (pfds[0].revents & POLLIN) {
			if (read(pfds[0].fd, bt.data, ffb_blen_in(&bt)) == -1)
				warn("Couldn't read signal: %s", strerror(errno));
			continue;
		}

		if ((len = read(pfds[1].fd, bt.tail, ffb_len_in(&bt))) == -1) {
			debug("BT read error: %s", strerror(errno));
			continue;
		}

		debug("BT read: %zd", len);
		fwrite(bt.data, 1, len, f);
	}

fail:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
fail_ffb:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
fail_open:
	pthread_cleanup_pop(1);
	return NULL;
}
#endif
