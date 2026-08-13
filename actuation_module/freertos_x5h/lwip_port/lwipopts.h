// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// lwIP configuration for the R-Car X5H Core1 RPMsg-netif transport (Task 6
// wires the real netif; this task only needs the stack to compile and link
// within the 10 MiB Core1 slot budget -- see
// actuation_module/freertos_x5h/scripts/check-image-budget.sh).
//
// PBUF_POOL_BUFSIZE/TCP_MSS/MTU below are all sized around the 462-byte
// RPMsg/OpenAMP payload budget the transport will use once Task 6 brings up
// the netif: MTU 462, TCP_MSS = MTU - 40 (IPv4 20 + TCP 20) = 422,
// PBUF_POOL_BUFSIZE = one full 462-byte frame plus lwIP's own pbuf/ETH/IP
// header overhead, rounded up.
#ifndef PLATFORM_FREERTOS_X5H_LWIP_PORT_LWIPOPTS_H_
#define PLATFORM_FREERTOS_X5H_LWIP_PORT_LWIPOPTS_H_

// ---- OS ----
#define NO_SYS                     0

// ---- APIs CycloneDDS needs ----
#define LWIP_SOCKET                1   /* CycloneDDS uses sockets */
#define LWIP_NETCONN               1

// ---- Protocol scope: IPv4/UDP/ICMP only, no DHCP/IGMP/IPv6 ----
// (the DDS network interface is a fixed static address --
// CONFIG_DDS_NETWORK_INTERFACE=172.16.52.2 -- and Task 6's RPMsg netif has
// no multicast-capable link layer, so IGMP has nothing to do)
#define LWIP_IPV6                  0
#define LWIP_DHCP                  0
#define LWIP_IGMP                  0
#define LWIP_ICMP                  1

// ---- Static pool sizing (this task's memory-risk gate) ----
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (256 * 1024)
#define PBUF_POOL_SIZE               64
#define PBUF_POOL_BUFSIZE           520   /* one full frame + pbuf overhead */
#define TCP_MSS                     422   /* MTU 462 - 40 */
#define MEMP_NUM_UDP_PCB              8
#define MEMP_NUM_NETCONN              16
#define TCPIP_THREAD_STACKSIZE     4096
#define TCPIP_MBOX_SIZE              32
#define DEFAULT_UDP_RECVMBOX_SIZE    32
#define SO_REUSE                     1
#define LWIP_SO_RCVTIMEO             1

// ---- Additions beyond the task brief's list, found necessary while
// writing this port (both required by lwip/sys.h's own contract, not
// optional lwIP tuning knobs) ----

// sys_arch_protect()/sys_arch_unprotect() (lwip_port/sys_arch.c) are only
// compiled into the link, and SYS_ARCH_PROTECT/UNPROTECT only expand to
// calls into them, when SYS_LIGHTWEIGHT_PROT is set here (see
// lwip/src/include/lwip/sys.h). Without this, lwIP's buffer/heap
// allocators run with no inter-task protection at all.
#define SYS_LIGHTWEIGHT_PROT         1

// newlib's <sys/time.h> already declares `struct timeval` (pulled in
// transitively by CycloneDDS and by common/ code that uses gettimeofday()
// semantics); LWIP_TIMEVAL_PRIVATE=0 tells lwip/sockets.h to reuse that
// definition instead of declaring its own, avoiding a duplicate-definition
// error in any translation unit that sees both headers. (The S32Z2 port hit
// the identical newlib/lwIP timeval collision -- see
// freertos_s32z2/scripts/build-cdds-target.sh's -DLWIP_TIMEVAL_PRIVATE=0 --
// same newlib toolchain family, same fix.)
#define LWIP_TIMEVAL_PRIVATE         0

#endif  // PLATFORM_FREERTOS_X5H_LWIP_PORT_LWIPOPTS_H_
