/*
 * Copyright 2017 Rockchip Electronics Co. LTD
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

#define MODULE_TAG "hal_m4vd_api"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

#include "mpp_log.h"
#include "mpp_err.h"
#include "mpp_mem.h"
#include "mpp_env.h"
#include "mpp_platform.h"

#include "hal_mpg4d_api.h"
#include "hal_m4vd_com.h"

#include "hal_m4vd_vdpu1.h"
#include "hal_m4vd_vdpu2.h"

RK_U32 hal_mpg4d_debug = 1;

/*!
***********************************************************************
* \brief
*    init
***********************************************************************
*/
//extern "C"
static MPP_RET hal_vpu_mpg4d_init(void *hal, MppHalCfg *cfg)
{
    hal_mpg4_ctx *p_hal = (hal_mpg4_ctx *)hal;
    MppHalApi *p_api = NULL;
    VpuHwMode hw_mode = MODE_NULL;
    RK_U32 hw_flag = 0;

    if (NULL == p_hal)
        return MPP_ERR_VALUE;

    memset(p_hal, 0, sizeof(hal_mpg4_ctx));
    p_api = &p_hal->hal_api;

    hw_flag = mpp_get_vcodec_type();
    mpp_assert(hw_flag & (HAVE_VDPU2 | HAVE_VDPU1));

    if (hw_flag & HAVE_VDPU2)
        hw_mode = VDPU2_MODE;
    if (hw_flag & HAVE_VDPU1)
        hw_mode = VDPU1_MODE;

    switch (hw_mode) {
    case VDPU2_MODE:
        p_api->init = vdpu2_mpg4d_init;
        p_api->deinit = vdpu2_mpg4d_deinit;
        p_api->reg_gen = vdpu2_mpg4d_gen_regs;
        p_api->start = vdpu2_mpg4d_start;
        p_api->wait = vdpu2_mpg4d_wait;
        p_api->reset = NULL;
        p_api->flush = NULL;
        p_api->control = NULL;
        break;
    case VDPU1_MODE:
        p_api->init = vdpu1_mpg4d_init;
        p_api->deinit = vdpu1_mpg4d_deinit;
        p_api->reg_gen = vdpu1_mpg4d_gen_regs;
        p_api->start = vdpu1_mpg4d_start;
        p_api->wait = vdpu1_mpg4d_wait;
        p_api->reset = NULL;
        p_api->flush = NULL;
        p_api->control = NULL;
        break;
    default:
        return MPP_ERR_INIT;
        break;
    }

    return p_api->init (hal, cfg);
}

/*!
***********************************************************************
* \brief
*    deinit
***********************************************************************
*/
//extern "C"
static MPP_RET hal_vpu_mpg4d_deinit(void *hal)
{
    hal_mpg4_ctx *p_hal = (hal_mpg4_ctx *)hal;

    return p_hal->hal_api.deinit(hal);
}

/*!
***********************************************************************
* \brief
*    generate register
***********************************************************************
*/
//extern "C"
static MPP_RET hal_vpu_mpg4d_gen_regs(void *hal, HalTaskInfo *task)
{
    hal_mpg4_ctx *p_hal = (hal_mpg4_ctx *)hal;

    return p_hal->hal_api.reg_gen(hal, task);
}

/*!
***********************************************************************
* \brief h
*    start hard
***********************************************************************
*/
//extern "C"
static MPP_RET hal_vpu_mpg4d_start(void *hal, HalTaskInfo *task)
{
    hal_mpg4_ctx *p_hal = (hal_mpg4_ctx *)hal;

    return p_hal->hal_api.start(hal, task);
}

/*!
***********************************************************************
* \brief
*    wait hard
***********************************************************************
*/
//extern "C"
static MPP_RET hal_vpu_mpg4d_wait(void *hal, HalTaskInfo *task)
{
    hal_mpg4_ctx *p_hal = (hal_mpg4_ctx *)hal;

    return p_hal->hal_api.wait(hal, task);
}

const MppHalApi hal_api_mpg4d = {
    .name = "mpg4d_vpu",
    .type = MPP_CTX_DEC,
    .coding = MPP_VIDEO_CodingMPEG4,
    .ctx_size = sizeof(hal_mpg4_ctx),
    .flag = 0,
    .init = hal_vpu_mpg4d_init,
    .deinit = hal_vpu_mpg4d_deinit,
    .reg_gen = hal_vpu_mpg4d_gen_regs,
    .start = hal_vpu_mpg4d_start,
    .wait = hal_vpu_mpg4d_wait,
    .reset = NULL,
    .flush = NULL,
    .control = NULL,
};
