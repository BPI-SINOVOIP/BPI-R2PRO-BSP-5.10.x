/*
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

#ifndef  __GST_MPP_H265_ENC_H__
#define  __GST_MPP_H265_ENC_H__

#include "gstmppenc.h"

G_BEGIN_DECLS;

#define GST_TYPE_MPP_H265_ENC (gst_mpp_h265_enc_get_type())
G_DECLARE_FINAL_TYPE (GstMppH265Enc, gst_mpp_h265_enc, GST,
    MPP_H265_ENC, GstMppEnc);

gboolean gst_mpp_h265_enc_register (GstPlugin * plugin, guint rank);

G_END_DECLS;

#endif /* __GST_MPP_H265_ENC_H__ */
