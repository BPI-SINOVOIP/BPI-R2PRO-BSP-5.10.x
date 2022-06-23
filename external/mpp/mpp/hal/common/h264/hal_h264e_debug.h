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

#ifndef __HAL_H264E_DEBUG_H__
#define __HAL_H264E_DEBUG_H__

#include "mpp_log.h"

#define HAL_H264E_DBG_SIMPLE            (0x00000001)
#define HAL_H264E_DBG_FUNCTION          (0x00000002)
#define HAL_H264E_DBG_FLOW              (0x00000004)
#define HAL_H264E_DBG_DETAIL            (0x00000008)

#define HAL_H264E_DBG_BUFFER            (0x00000010)
#define HAL_H264E_DBG_REGS              (0x00000020)
#define HAL_H264E_DBG_AMEND             (0x00000040)

#define HAL_H264E_DBG_RC                (0x00000100)

#define hal_h264e_dbg(flag, fmt, ...)   _mpp_dbg(hal_h264e_debug, flag, fmt, ## __VA_ARGS__)
#define hal_h264e_dbg_f(flag, fmt, ...) _mpp_dbg_f(hal_h264e_debug, flag, fmt, ## __VA_ARGS__)

#define hal_h264e_dbg_func(fmt, ...)    hal_h264e_dbg_f(HAL_H264E_DBG_FUNCTION, fmt, ## __VA_ARGS__)
#define hal_h264e_dbg_flow(fmt, ...)    hal_h264e_dbg_f(HAL_H264E_DBG_FLOW, fmt, ## __VA_ARGS__)
#define hal_h264e_dbg_detail(fmt, ...)  hal_h264e_dbg_f(HAL_H264E_DBG_DETAIL, fmt, ## __VA_ARGS__)

#define hal_h264e_dbg_buffer(fmt, ...)  hal_h264e_dbg_f(HAL_H264E_DBG_BUFFER, fmt, ## __VA_ARGS__)
#define hal_h264e_dbg_regs(fmt, ...)    hal_h264e_dbg_f(HAL_H264E_DBG_REGS, fmt, ## __VA_ARGS__)
#define hal_h264e_dbg_amend(fmt, ...)   hal_h264e_dbg_f(HAL_H264E_DBG_AMEND, fmt, ## __VA_ARGS__)

#define hal_h264e_dbg_rc(fmt, ...)      hal_h264e_dbg_f(HAL_H264E_DBG_RC, fmt, ## __VA_ARGS__)

#define hal_h264e_enter()               hal_h264e_dbg_func("enter\n");
#define hal_h264e_leave()               hal_h264e_dbg_func("leave\n");

extern RK_U32 hal_h264e_debug;

#endif /* __HAL_H264E_DEBUG_H__ */
