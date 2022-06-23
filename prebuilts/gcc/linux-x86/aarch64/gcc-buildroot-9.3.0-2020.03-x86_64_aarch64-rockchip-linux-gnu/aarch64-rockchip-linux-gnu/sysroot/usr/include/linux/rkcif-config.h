/* SPDX-License-Identifier: (GPL-2.0+ OR MIT)
 *
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKCIF_CONFIG_H
#define _RKCIF_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define RKCIF_API_VERSION		KERNEL_VERSION(0, 1, 0x8)

#define V4L2_CID_CIF_DATA_COMPACT	(V4L2_CID_PRIVATE_BASE + 0)

enum cif_csi_lvds_memory {
	CSI_LVDS_MEM_16BITS = 0,
	CSI_LVDS_MEM_COMPACT,
	CSI_LVDS_MEM_MAX,
};

#endif
