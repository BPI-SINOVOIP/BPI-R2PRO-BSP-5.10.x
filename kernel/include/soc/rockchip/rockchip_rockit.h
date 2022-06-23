/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 */
#ifndef __SOC_ROCKCHIP_ROCKIT_H
#define __SOC_ROCKCHIP_ROCKIT_H

#include <linux/dma-buf.h>

#define ROCKIT_BUF_NUM_MAX	3
#define ROCKIT_ISP_NUM_MAX	5
#define ROCKIT_STREAM_NUM_MAX	13

enum function_cmd {
	ROCKIT_BUF_QUE,
	ROCKIT_MPIBUF_DONE
};

struct rkisp_stream_cfg {
	struct rkisp_rockit_buffer *rkisp_buff[ROCKIT_BUF_NUM_MAX];
	int buff_id[ROCKIT_BUF_NUM_MAX];
	void *node;
	int fps_cnt;
	int dst_fps;
	int cur_fps;
	u64 old_time;
	bool is_discard;
};

struct ISP_VIDEO_FRAMES {
	u32	pMbBlk;
	u32	u32Width;
	u32	u32Height;
	u32	u32VirWidth;
	u32	u32VirHeight;
	u32	enField;
	u32	enPixelFormat;
	u32	enVideoFormat;
	u32	enCompressMode;
	u32	enDynamicRange;
	u32	enColorGamut;
	u32	u32TimeRef;
	u64	u64PTS;

	u64	u64PrivateData;
	u32	u32FrameFlag;     /* FRAME_FLAG_E, can be OR operation. */
};

struct rkisp_dev_cfg {
	char *isp_name;
	void *isp_dev;
};

struct rockit_cfg {
	bool is_alloc;
	bool is_empty;
	bool is_qbuf;
	char *current_name;
	dma_addr_t dma_addr;
	int *buff_id;
	int mpi_id;
	int isp_num;
	u32 nick_id;
	u32 event;
	void *node;
	void *mpibuf;
	void *vvi_dev[ROCKIT_ISP_NUM_MAX];
	struct dma_buf *buf;
	struct ISP_VIDEO_FRAMES frame;
	struct rkisp_dev_cfg rkisp_dev_cfg[ROCKIT_ISP_NUM_MAX];
	struct rkisp_stream_cfg rkisp_stream_cfg[ROCKIT_STREAM_NUM_MAX];
	int (*rkisp_rockit_mpibuf_done)(struct rockit_cfg *rockit_isp_cfg);
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V32)

void *rkisp_rockit_function_register(void *function, int cmd);
int rkisp_rockit_get_ispdev(char **name);
int rkisp_rockit_buf_queue(struct rockit_cfg *input_rockit_cfg);

#else

static inline void *rkisp_rockit_function_register(void *function, int cmd) { return NULL; }
static inline int rkisp_rockit_get_ispdev(char **name) { return -EINVAL; }
static inline int rkisp_rockit_buf_queue(struct rockit_cfg *input_rockit_cfg)
{
	return -EINVAL;
}

#endif

#endif
