/* GStreamer
 * Copyright (C) <2021> Collabora Ltd.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
 *   Author: Julian Bouzas <julian.bouzas@collabora.com>
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
 */

#ifndef __GST_MPP_VPX_ALPHA_DECODE_BIN_H__
#define __GST_MPP_VPX_ALPHA_DECODE_BIN_H__

#include "gstmppalphadecodebin.h"

G_BEGIN_DECLS;

#define GST_TYPE_MPP_VPX_ALPHA_DECODE_BIN (gst_mpp_vpx_alpha_decode_bin_get_type())
G_DECLARE_FINAL_TYPE (GstMppVpxAlphaDecodeBin,
    gst_mpp_vpx_alpha_decode_bin, GST, MPP_VPX_ALPHA_DECODE_BIN,
    GstMppAlphaDecodeBin);

gboolean gst_mpp_vpx_alpha_decode_bin_register (GstPlugin * plugin, guint rank);

G_END_DECLS;

#endif /* __GST_MPP_VPX_ALPHA_DECODE_BIN_H__ */
