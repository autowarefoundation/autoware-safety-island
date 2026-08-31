/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * All rights reserved.
 * Copyright (c) 2017 Xilinx, Inc.
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PLATFORM_INFO_H_
#define PLATFORM_INFO_H_

#include <openamp/remoteproc.h>
#include <openamp/virtio.h>
#include <openamp/rpmsg.h>

#include "platform_info_common.h"

/* Cortex R52 memory attributes */
#define DEVICE_SHARED       0x00000001U /* device, shareable */
#define DEVICE_NONSHARED    0x00000010U /* device, non shareable */
#define NORM_NSHARED_NCACHE 0x00000008U /* Non cacheable  non shareable */
#define NORM_SHARED_NCACHE  0x0000000CU /* Non cacheable shareable */
#define PRIV_RW_USER_RW     (0x00000003U<<8U) /* Full Access */

#if defined __cplusplus
extern "C" {
#endif

struct remoteproc_priv {
	const char *kick_dev_name;
	const char *kick_dev_bus_name;
	struct metal_device *kick_dev;
	struct metal_io_region *kick_io;
#ifndef RPMSG_NO_IPI
	unsigned int ipi_chn_mask; /**< IPI channel mask */
	atomic_int ipi_nokick;
#endif /* !RPMSG_NO_IPI */
};

#if defined __cplusplus
}
#endif

#endif /* PLATFORM_INFO_H_ */
