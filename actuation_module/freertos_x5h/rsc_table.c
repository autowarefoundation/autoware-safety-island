/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * All rights reserved.
 * Copyright (c) 2015 Xilinx, Inc. All rights reserved.
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * This file populates resource table for BM remote
 * for use by the Linux host
 */

#include <stdio.h>
#include <openamp/open_amp.h>
#include "rsc_table.h"

extern char __resource_table_start;
extern char __resource_table_end;

/* Place resource table in special ELF section */
#define __section_t(S) __attribute__((__section__(#S)))
#define __resource __section_t(.resource_table)

#define RPMSG_VDEV_DFEATURES (1 << VIRTIO_RPMSG_F_NS)

/* VirtIO rpmsg device id */
#define VIRTIO_ID_RPMSG_ 7

#define NUM_VRINGS 0x02
#define VRING_ALIGN 0x1000
#ifndef RING_TX
#define RING_TX FW_RSC_U32_ADDR_ANY
#endif /* !RING_TX */
#ifndef RING_RX
#define RING_RX FW_RSC_U32_ADDR_ANY
#endif /* RING_RX */
#define VRING_SIZE 256

#define NUM_TABLE_ENTRIES 1

/* Added a workaround because FlashWriter/IPL still does not support
   loading to the SDRAM region. Therefore, the same struct data is
   duplicated: one copy is located in SDRAM for use by the Linux host
   (it will be removed when converting to .srec to avoid FlashWriter hang-ups),
   and the other is placed in VRAM to be manually loaded into SDRAM at runtime.
 */

static struct remote_resource_table __resource resources = {
    /* Version */
    1,

    /* NUmber of table entries */
    NUM_TABLE_ENTRIES,
    /* reserved fields */
    {
        0,
        0,
    },

    /* Offsets of rsc entries */
    {
        offsetof(struct remote_resource_table, rpmsg_vdev),
    },

    /* Virtio device entry */
    {
        RSC_VDEV,
        VIRTIO_ID_RPMSG_,
        31,
        RPMSG_VDEV_DFEATURES,
        0,
        0,
        0,
        NUM_VRINGS,
        {0, 0},
    },

    /* Vring rsc entry - part of vdev rsc entry */
    {RING_TX, VRING_ALIGN, VRING_SIZE, 1, 0},
    {RING_RX, VRING_ALIGN, VRING_SIZE, 2, 0},
};

static const struct remote_resource_table resources_data = {
    /* Version */
    1,

    /* NUmber of table entries */
    NUM_TABLE_ENTRIES,
    /* reserved fields */
    {
        0,
        0,
    },

    /* Offsets of rsc entries */
    {
        offsetof(struct remote_resource_table, rpmsg_vdev),
    },

    /* Virtio device entry */
    {
        RSC_VDEV,
        VIRTIO_ID_RPMSG_,
        31,
        RPMSG_VDEV_DFEATURES,
        0,
        0,
        0,
        NUM_VRINGS,
        {0, 0},
    },

    /* Vring rsc entry - part of vdev rsc entry */
    {RING_TX, VRING_ALIGN, VRING_SIZE, 1, 0},
    {RING_RX, VRING_ALIGN, VRING_SIZE, 2, 0},
};

void init_resource_table(void)
{
    size_t len = (uintptr_t)&__resource_table_end - (uintptr_t)&__resource_table_start;

    memcpy((void *)(uintptr_t)&__resource_table_start, &resources_data, len);
}

void *get_resource_table(int rsc_id, int *len)
{
    (void)rsc_id;

    *len = (uintptr_t)&__resource_table_end - (uintptr_t)&__resource_table_start;

    return (void *)&__resource_table_start;
}
