// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// lwIP options for the NXP S32Z2 build. Pulled in by the lwIP sources
// from the NXP RTD via the lwIP build system.

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1

#define LWIP_SOCKET                     1
#define LWIP_COMPAT_SOCKETS             1
#define LWIP_NETCONN                    1
#define LWIP_NETIF_API                  1

#define LWIP_TCPIP_CORE_LOCKING         1
#define TCPIP_THREAD_STACKSIZE          8192
#define TCPIP_THREAD_PRIO               (12)   /* configMAX_PRIORITIES - 3 */
#define DEFAULT_THREAD_STACKSIZE        4096
// Priority of the NETC RX poll thread (eth_port.c ethif_poll_thread) created
// with sys_thread_new(..., DEFAULT_THREAD_PRIO). lwIP's opt.h defaults this to
// 1, which would tie it with the DDS/controller pthreads (tskIDLE_PRIORITY+1)
// and let them block RX servicing. Place it just above the tcpip thread (12) so
// it is never starved while draining the RX ring; it sleeps one tick per sweep
// (OsIf_TimeDelay(1)), so the tcpip thread still gets the CPU to process frames.
#define DEFAULT_THREAD_PRIO             (13)

// Mailbox depths. lwIP's opt.h defaults these to 0, and NXP's sys_arch.c
// sys_mbox_new() spins forever (`b .`) when asked for a size <= 0 — so with
// NO_SYS=0 the very first mbox (the tcpip thread's) hangs at bring-up unless
// these are set > 0. Match the S32CT-generated lwipopts (TCPIP=40, recv=20,
// accept=10); our hand-rolled lwipopts.h is first on the include path and
// shadows that generated file.
#define TCPIP_MBOX_SIZE                 40
#define DEFAULT_UDP_RECVMBOX_SIZE       20
#define DEFAULT_TCP_RECVMBOX_SIZE       20
#define DEFAULT_RAW_RECVMBOX_SIZE       10
#define DEFAULT_ACCEPTMBOX_SIZE         10

// S32Z2's int_sram_dram region is only 512 KB for all data; the lwIP heap
// has to share it with the FreeRTOS kernel heap (configTOTAL_HEAP_SIZE),
// initialised globals, and CycloneDDS buffers.
#define MEM_SIZE                        (64 * 1024)
#define MEMP_NUM_TCP_PCB                4
// CycloneDDS opens 5 UDP sockets for a single participant with multicast
// (unicast disc+data, multicast disc+data, and one transmit conn per
// interface), each backed by an lwIP netconn + UDP PCB. lwIP defaults both
// MEMP_NUM_NETCONN and MEMP_NUM_UDP_PCB to 4 -- one short -- so the 5th
// netconn_alloc()/udp_new() returns NULL and dds_create_domain fails. Size
// both for the participant's sockets plus DHCP/DNS headroom.
#define MEMP_NUM_NETCONN                16
#define MEMP_NUM_UDP_PCB                16
#define MEMP_NUM_PBUF                   16
#define PBUF_POOL_SIZE                  16

#define LWIP_DHCP                       0   /* static IP bring-up; DHCP unused */
#define LWIP_IGMP                       1   /* SPDP multicast */
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_RAW                        0
#define LWIP_DNS                        1

#define LWIP_SO_RCVBUF                  1
#define SO_REUSE                        1

// Compiled in for completeness; the static-IP bring-up in lwip_bringup.c does
// not register a status callback (it blocks only on tcpip_init, not on a DHCP
// lease).
#define LWIP_NETIF_STATUS_CALLBACK      1

// NXP's NETC <-> lwIP glue (code/ports/netif/ethif/rtd/generic/eth_port.c)
// extends struct pbuf with a back-pointer to the NETC RX buffer so it can
// hand it back to Eth_43_NETC_ProvideRxBuffer() when the pbuf is freed.
// lwIP exposes this extension point via LWIP_PBUF_CUSTOM_DATA.
#define LWIP_PBUF_CUSTOM_DATA           uint8_t *rx_buf;

#endif  // LWIPOPTS_H
