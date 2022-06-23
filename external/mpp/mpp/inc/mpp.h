/*
 * Copyright 2015 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MPP_H__
#define __MPP_H__

#include "mpp_queue.h"
#include "mpp_task_impl.h"

#include "mpp_dec.h"
#include "mpp_enc.h"
#include "mpp_impl.h"

#define MPP_DBG_FUNCTION                    (0x00000001)
#define MPP_DBG_PACKET                      (0x00000002)
#define MPP_DBG_FRAME                       (0x00000004)
#define MPP_DBG_BUFFER                      (0x00000008)

/*
 * mpp notify event flags
 * When event happens mpp will signal deocder / encoder with different flag.
 * These event will wake up the codec thread or hal thread
 */
#define MPP_INPUT_ENQUEUE                   (0x00000001)
#define MPP_OUTPUT_DEQUEUE                  (0x00000002)
#define MPP_INPUT_DEQUEUE                   (0x00000004)
#define MPP_OUTPUT_ENQUEUE                  (0x00000008)
#define MPP_RESET                           (0xFFFFFFFF)

/* mpp dec event flags */
#define MPP_DEC_NOTIFY_PACKET_ENQUEUE       (MPP_INPUT_ENQUEUE)
#define MPP_DEC_NOTIFY_FRAME_DEQUEUE        (MPP_OUTPUT_DEQUEUE)
#define MPP_DEC_NOTIFY_EXT_BUF_GRP_READY    (0x00000010)
#define MPP_DEC_NOTIFY_INFO_CHG_DONE        (0x00000020)
#define MPP_DEC_NOTIFY_BUFFER_VALID         (0x00000040)
#define MPP_DEC_NOTIFY_TASK_ALL_DONE        (0x00000080)
#define MPP_DEC_NOTIFY_TASK_HND_VALID       (0x00000100)
#define MPP_DEC_NOTIFY_TASK_PREV_DONE       (0x00000200)
#define MPP_DEC_NOTIFY_BUFFER_MATCH         (0x00000400)
#define MPP_DEC_CONTROL                     (0x00010000)
#define MPP_DEC_RESET                       (MPP_RESET)

/* mpp enc event flags */
#define MPP_ENC_NOTIFY_FRAME_ENQUEUE        (MPP_INPUT_ENQUEUE)
#define MPP_ENC_NOTIFY_PACKET_DEQUEUE       (MPP_OUTPUT_DEQUEUE)
#define MPP_ENC_NOTIFY_FRAME_DEQUEUE        (MPP_INPUT_DEQUEUE)
#define MPP_ENC_NOTIFY_PACKET_ENQUEUE       (MPP_OUTPUT_ENQUEUE)
#define MPP_ENC_CONTROL                     (0x00000010)
#define MPP_ENC_RESET                       (MPP_RESET)

/*
 * mpp hierarchy
 *
 * mpp layer create mpp_dec or mpp_dec instance
 * mpp_dec create its parser and hal module
 * mpp_enc create its control and hal module
 *
 *                                  +-------+
 *                                  |       |
 *                    +-------------+  mpp  +-------------+
 *                    |             |       |             |
 *                    |             +-------+             |
 *                    |                                   |
 *                    |                                   |
 *                    |                                   |
 *              +-----+-----+                       +-----+-----+
 *              |           |                       |           |
 *          +---+  mpp_dec  +--+                 +--+  mpp_enc  +---+
 *          |   |           |  |                 |  |           |   |
 *          |   +-----------+  |                 |  +-----------+   |
 *          |                  |                 |                  |
 *          |                  |                 |                  |
 *          |                  |                 |                  |
 *  +-------v------+     +-----v-----+     +-----v-----+     +------v-------+
 *  |              |     |           |     |           |     |              |
 *  |  dec_parser  |     |  dec_hal  |     |  enc_hal  |     |  enc_control |
 *  |              |     |           |     |           |     |              |
 *  +--------------+     +-----------+     +-----------+     +--------------+
 */

#ifdef __cplusplus

class Mpp
{
public:
    Mpp(MppCtx ctx = NULL);
    ~Mpp();
    MPP_RET init(MppCtxType type, MppCodingType coding);

    MPP_RET start();
    MPP_RET stop();

    MPP_RET pause();
    MPP_RET resume();

    MPP_RET put_packet(MppPacket packet);
    MPP_RET get_frame(MppFrame *frame);

    MPP_RET put_frame(MppFrame frame);
    MPP_RET get_packet(MppPacket *packet);

    MPP_RET poll(MppPortType type, MppPollType timeout);
    MPP_RET dequeue(MppPortType type, MppTask *task);
    MPP_RET enqueue(MppPortType type, MppTask task);

    MPP_RET reset();
    MPP_RET control(MpiCmd cmd, MppParam param);

    MPP_RET notify(RK_U32 flag);
    MPP_RET notify(MppBufferGroup group);

    mpp_list        *mPktIn;
    mpp_list        *mPktOut;
    mpp_list        *mFrmIn;
    mpp_list        *mFrmOut;
    /* counters for debug */
    RK_U32          mPacketPutCount;
    RK_U32          mPacketGetCount;
    RK_U32          mFramePutCount;
    RK_U32          mFrameGetCount;
    RK_U32          mTaskPutCount;
    RK_U32          mTaskGetCount;

    /*
     * packet buffer group
     *      - packets in I/O, can be ion buffer or normal buffer
     * frame buffer group
     *      - frames in I/O, normally should be a ion buffer group
     */
    MppBufferGroup  mPacketGroup;
    MppBufferGroup  mFrameGroup;
    RK_U32          mExternalFrameGroup;

    /*
     * Mpp task queue for advance task mode
     */
    /*
     * Task data flow:
     *                  |
     *     user         |          mpp
     *           mInputTaskQueue
     * mUsrInPort  ->   |   -> mMppInPort
     *                  |          |
     *                  |          v
     *                  |       process
     *                  |          |
     *                  |          v
     * mUsrOutPort <-   |   <- mMppOutPort
     *           mOutputTaskQueue
     */
    MppPort         mUsrInPort;
    MppPort         mUsrOutPort;
    MppPort         mMppInPort;
    MppPort         mMppOutPort;
    MppTaskQueue    mInputTaskQueue;
    MppTaskQueue    mOutputTaskQueue;

    MppPollType     mInputTimeout;
    MppPollType     mOutputTimeout;

    MppTask         mInputTask;
    MppTask         mEosTask;

    MppCtx          mCtx;
    MppDec          mDec;
    MppEnc          mEnc;

    RK_U32          mEncAyncIo;

private:
    void clear();

    MppCtxType      mType;
    MppCodingType   mCoding;

    RK_U32          mInitDone;
    RK_U32          mMultiFrame;

    RK_U32          mStatus;

    /* decoder paramter before init */
    MppDecCfgSet    mDecInitcfg;
    RK_U32          mParserFastMode;
    RK_U32          mParserNeedSplit;
    RK_U32          mParserInternalPts;     /* for MPEG2/MPEG4 */
    RK_U32          mImmediateOut;
    /* backup extra packet for seek */
    MppPacket       mExtraPacket;

    /* dump info for debug */
    MppDump         mDump;

    MPP_RET control_mpp(MpiCmd cmd, MppParam param);
    MPP_RET control_osal(MpiCmd cmd, MppParam param);
    MPP_RET control_codec(MpiCmd cmd, MppParam param);
    MPP_RET control_dec(MpiCmd cmd, MppParam param);
    MPP_RET control_enc(MpiCmd cmd, MppParam param);
    MPP_RET control_isp(MpiCmd cmd, MppParam param);

    /* for special encoder async io mode */
    MPP_RET put_frame_async(MppFrame frame);
    MPP_RET get_packet_async(MppPacket *packet);

    Mpp(const Mpp &);
    Mpp &operator=(const Mpp &);
};


extern "C" {
}
#endif

#endif /*__MPP_H__*/
