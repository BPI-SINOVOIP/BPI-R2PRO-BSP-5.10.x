/*
 * Copyright 2021 Rockchip Electronics Co. LTD
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

#ifndef __MPP_TRACE_H__
#define __MPP_TRACE_H__

#include "rk_type.h"

#ifdef __cplusplus
extern "C" {
#endif

void mpp_trace_begin(const char* name);
void mpp_trace_end(const char* name);
void mpp_trace_async_begin(const char* name, RK_S32 cookie);
void mpp_trace_async_end(const char* name, RK_S32 cookie);
void mpp_trace_int32(const char* name, RK_S32 value);
void mpp_trace_int64(const char* name, RK_S64 value);

#ifdef __cplusplus
}
#endif

#endif /*__MPP_TRACE_H__*/