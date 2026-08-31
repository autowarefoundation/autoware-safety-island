/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include "FreeRTOS.h"
#include "platform_info.h"
#include "rsc_table.h"
#include "mfis/mfis.h"

#define LPRINTF(format, ...) printf(format, ##__VA_ARGS__); vTaskDelay(10);

/* Define shared DRAM area for each channel. */
#define SHARED_CH_RAM_BASE (0x40000000UL)
#define SHARED_CH_RAM_SIZE (0x80000000UL)

/* Remote processor operations from r52 to a720. It defines
 * notification operation and remote processor managementi operations. */
extern const struct remoteproc_ops x5h_r_a_proc_ops;
static struct remoteproc rproc_inst;
static struct mfis_channel mfis_inst =
{
    .ch = MFIS_CHAN,
    .int_source = 0,
    .recv_message = 0,
    .cb_function = NULL
};
/* RPMsg virtio shared buffer pool */
// static struct rpmsg_virtio_shm_pool shpool;

/*----------------------------- RPMSG Platform implementation ----------------------------*/
/* Create platform
- Init remoteproc instance
- Map shared memory and resource table to the instance
- Set resource table
*/
struct remoteproc * platform_create_proc(int mfis_ch, int rsc_index)
{
    void *rsc_table;
    int rsc_size;
    int ret;
    metal_phys_addr_t pa;
    void *tmp;

    /* Initialize the resource table on shared memory */
    init_resource_table();

    rsc_table = get_resource_table(rsc_index, &rsc_size);

    /* Initialize remoteproc instance */
    mfis_inst.ch = mfis_ch;
    if (!remoteproc_init(&rproc_inst, &x5h_r_a_proc_ops, (void*)&mfis_inst))
        return NULL;

    /* mmap resource table */
    pa = (metal_phys_addr_t)rsc_table;
    (void *)remoteproc_mmap(&rproc_inst, &pa,
                NULL, rsc_size,
                NORM_NSHARED_NCACHE|PRIV_RW_USER_RW,
                &rproc_inst.rsc_io);
    LPRINTF("%s: mem->io->virt=0x%lx\r\n", __func__, (uint32_t)rproc_inst.rsc_io->virt);
    LPRINTF("%s: mem->io->phys=0x%lx\r\n", __func__, (uint32_t)*rproc_inst.rsc_io->physmap);
    /* mmap shared memory */
    pa = SHARED_CH_RAM_BASE;
    (void *)remoteproc_mmap(&rproc_inst, (void*)&pa,
                NULL, SHARED_CH_RAM_SIZE,
                NORM_NSHARED_NCACHE|PRIV_RW_USER_RW,
                NULL);

    /* parse resource table to remoteproc */
    ret = remoteproc_set_rsc_table(&rproc_inst, rproc_inst.rsc_io->virt, rsc_size);
    if (ret) {
        LPRINTF("Failed to initialize remoteproc, ret: %d\r\n", ret);
        remoteproc_remove(&rproc_inst);
        return NULL;
    }
    LPRINTF("Initialize remoteproc successfully.\r\n");

    return &rproc_inst;

}

/* Init platform: This function does:
- Init related HW module if required
- Create a `struct remoteproc` and return it to `platform` pointer
*/
int platform_init(int channel, void **platform)
{
    unsigned long mfis_ch = channel;
    unsigned long rsc_id = 0;
    struct remoteproc *rproc;

    if (!platform) {
        LPRINTF("Failed to initialize platform\r\n");
        return -EINVAL;
    }

    rproc = platform_create_proc(mfis_ch, rsc_id);
    if (!rproc) {
        LPRINTF("Failed to create remoteproc device.\r\n");
        return -EINVAL;
    }
    *platform = rproc;

    return 0;
}


/* Create a RPMsg VirtIO device
- Create VirtIO device of remoteproc instance
- (Driver only) Initialize the shared buffer pool
- Init rpmsg virtio device with remoteproc device
-
*/
struct rpmsg_device *
platform_create_rpmsg_vdev(void *platform, unsigned int vdev_index,
                        unsigned int role,
                        void (*rst_cb)(struct virtio_device *vdev),
                        rpmsg_ns_bind_cb ns_bind_cb)
{
    struct remoteproc *rproc = platform;
    struct rpmsg_virtio_device *rpmsg_vdev;
    struct virtio_device *vdev;
    void *shbuf;
    struct metal_io_region *shbuf_io;
    struct mfis_channel* mfis_ch = (struct mfis_channel*)(rproc->priv);
    int ret;

    rpmsg_vdev = metal_allocate_memory(sizeof(*rpmsg_vdev));
    if (!rpmsg_vdev)
        return NULL;
    shbuf_io = remoteproc_get_io_with_pa(rproc, SHARED_CH_RAM_BASE);
    if (!shbuf_io)
    {
        LPRINTF("failed remoteproc_get_io_with_pa\r\n");
        goto err1;
    }
    shbuf = metal_io_phys_to_virt(shbuf_io,
                      SHARED_CH_RAM_BASE); // Shared buff offset = 0

    LPRINTF("creating remoteproc virtio\r\n");
    /* TODO: can we have a wrapper for the following two functions? */
    vdev = remoteproc_create_virtio(rproc, vdev_index, role, rst_cb);
    if (!vdev) {
        LPRINTF("failed remoteproc_create_virtio\r\n");
        goto err1;
    }

    // printf("initializing rpmsg shared buffer pool\r\n");
    /* Only RPMsg virtio driver needs to initialize the shared buffers pool */
    // rpmsg_virtio_init_shm_pool(&shpool, shbuf,
    //                (SHARED_CH_RAM_SIZE - 0));

    LPRINTF("initializing rpmsg vdev\r\n");
    /* RPMsg virtio device can set shared buffers pool argument to NULL */
    ret =  rpmsg_init_vdev(rpmsg_vdev, vdev, ns_bind_cb,
                   shbuf_io,
                   NULL);
    if (ret) {
        LPRINTF("failed rpmsg_init_vdev\r\n");
        goto err2;
    }
    LPRINTF("initializing rpmsg vdev\r\n");
    return rpmsg_virtio_get_rpmsg_device(rpmsg_vdev);
err2:
    remoteproc_remove_virtio(rproc, vdev);
err1:
    metal_free_memory(rpmsg_vdev);
    return NULL;
}

/* Wait for notification from driver
Return 0 if got noti
Otherwise return negative value
*/
int platform_poll(void *platform)
{
    struct remoteproc *rproc = platform;
    struct mfis_channel* mfis = (struct mfis_channel*)rproc->priv;
    int ret = -1;

    if (0 != mfis->int_source)
    {
	remoteproc_get_notification(rproc, RSC_NOTIFY_ID_ANY);
        mfis->int_source = 0; // Reset int source to 0
        ret = 0;
    }

    return ret;
}

/* Deinit RPMsg device, call 2 functions:
- `rpmsg_deinit_vdev`
- `remoteproc_remove_virtio`
*/
void platform_release_rpmsg_vdev(struct rpmsg_device *rpdev, void *platform)
{

}

/* Remove platform resource of remote proc
- Remove `remote_proc` device
- Free memory, deinit HW
*/
void platform_cleanup(void *platform)
{

}
