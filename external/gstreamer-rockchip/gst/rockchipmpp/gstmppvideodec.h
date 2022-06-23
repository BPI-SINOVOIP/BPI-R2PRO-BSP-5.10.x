/*
 * Copyright 2017 Rockchip Electronics Co., Ltd
 *     Author: Randy Li <randy.li@rock-chips.com>
 *
 * Copyright 2021 Rockchip Electronics Co., Ltd
 *     Author: Jeffy Chen <jeffy.chen@rock-chips.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef  __GST_MPP_VIDEO_DEC_H__
#define  __GST_MPP_VIDEO_DEC_H__

#include "gstmppdec.h"

G_BEGIN_DECLS;

#define GST_TYPE_MPP_VIDEO_DEC (gst_mpp_video_dec_get_type())
G_DECLARE_FINAL_TYPE (GstMppVideoDec, gst_mpp_video_dec, GST,
    MPP_VIDEO_DEC, GstMppDec);

gboolean gst_mpp_video_dec_register (GstPlugin * plugin, guint rank);

G_END_DECLS;

#endif /* __GST_MPP_VIDEO_DEC_H__ */
