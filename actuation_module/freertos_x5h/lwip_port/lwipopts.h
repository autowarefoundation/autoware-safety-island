// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// lwIP configuration for the R-Car X5H Core1 RPMsg-netif transport (see
// rpmsg_netif.{h,c}/lwip_bringup.c for the real netif this stack runs; this
// file only has to keep the stack within the 10 MiB Core1 slot budget --
// see actuation_module/freertos_x5h/scripts/check-image-budget.sh -- and
// give it a correct, live-verified thread-priority layout).
//
// PBUF_POOL_BUFSIZE/TCP_MSS/MTU below are all sized around the 462-byte
// RPMsg/OpenAMP payload budget the transport uses: MTU 462, TCP_MSS =
// MTU - 40 (IPv4 20 + TCP 20) = 422, PBUF_POOL_BUFSIZE = one full 462-byte
// frame plus lwIP's own pbuf/ETH/IP header overhead, rounded up.
#ifndef PLATFORM_FREERTOS_X5H_LWIP_PORT_LWIPOPTS_H_
#define PLATFORM_FREERTOS_X5H_LWIP_PORT_LWIPOPTS_H_

// ---- OS ----
#define NO_SYS                     0

// ---- APIs CycloneDDS needs ----
#define LWIP_SOCKET                1   /* CycloneDDS uses sockets */
#define LWIP_NETCONN               1

// ---- Protocol scope: IPv4/UDP/ICMP only, no TCP/DHCP/IGMP/IPv6 ----
// (the DDS network interface is a fixed static address --
// CONFIG_DDS_NETWORK_INTERFACE=172.16.52.2 -- and Task 6's RPMsg netif has
// no multicast-capable link layer, so IGMP has nothing to do)
#define LWIP_IPV6                  0
#define LWIP_DHCP                  0
#define LWIP_IGMP                  0
#define LWIP_ICMP                  1

// LWIP_TCP (review finding): this section's heading claimed UDP/ICMP only,
// but leaving LWIP_TCP unset takes lwIP's own default of 1 (lwip/opt.h), so
// the entire TCP implementation was compiled and linked in. Nothing here
// opens a TCP socket -- CycloneDDS on this target is configured UDP-only --
// and unlike a --gc-sections link the dead code could not be dropped later:
// CMakeLists.txt does NOT use --gc-sections, deliberately (see its own "ld
// 2.42 --gc-sections+-O2+-g segfault" comment), so every TCP object stayed
// resident in the 10 MiB Core1 slot. Turning it off makes the heading true
// and the slot budget honest: measured on this target, the slot window drops
// from 0x84d890 to 0x843c20, i.e. 40,048 bytes (39.1 KiB) of never-executed
// TCP recovered, and `arm-none-eabi-nm` no longer shows any lwIP tcp_*
// implementation symbol in actuation_x5h.elf (tcp_input / tcp_output /
// tcp_write / tcp_connect / tcp_slowtmr all gone; udp_input, ip4_input,
// icmp_input and etharp_output all still present, as they must be).
//
// Safe to disable at link level on this lwIP (2.2.1): the netconn/socket
// entry points a TCP caller would reach are compiled unconditionally but
// their TCP bodies are `#if LWIP_TCP`-guarded with `#else` stubs returning
// ERR_ARG (api_lib.c's netconn_accept / netconn_listen_with_backlog), so
// there is no undefined reference from sockets.c -- a would-be TCP caller
// gets a runtime error instead of a broken build. TCP_MSS below is retained
// because PBUF_POOL_BUFSIZE's derivation is written in terms of it.
#define LWIP_TCP                   0

// ---- Static pool sizing (this task's memory-risk gate) ----
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (256 * 1024)
#define PBUF_POOL_SIZE               64
#define PBUF_POOL_BUFSIZE           520   /* one full frame + pbuf overhead */
#define TCP_MSS                     422   /* MTU 462 - 40 */
// 8, not the S32Z2 bench's 16: CycloneDDS opens up to 5 UDP sockets for a
// single participant with multicast (unicast disc+data, multicast
// disc+data, one transmit conn), but CONFIG_DDS_DISABLE_MULTICAST=1 on this
// point-to-point RPMsg link (CMakeLists.txt's Task 8 comment) removes both
// multicast sockets, leaving at most ~3 in real use; S32Z2 also pads its 16
// for DHCP/DNS headroom that this port does not need (LWIP_DHCP=0 above,
// and no DNS resolver is used here either). 8 keeps a comfortable margin
// over the ~3 actually needed without carrying the sibling's multicast/DHCP
// headroom this target has no use for.
#define MEMP_NUM_UDP_PCB              8
#define MEMP_NUM_NETCONN              16
// Bytes, not words (review round 1 fix; was 4096): sys_arch.c's
// sys_thread_new() -- the only caller of xTaskCreate() for this value --
// divides its stacksize argument by sizeof(StackType_t) before passing it
// to xTaskCreate(), which itself takes a word count. lwIP's own
// tcpip.c passes TCPIP_THREAD_STACKSIZE straight through as that
// stacksize argument (i.e. this macro is documented by lwIP as bytes), so
// the previous 4096 gave the single tcpip thread -- which runs every
// tcpip_callback(), the whole IP stack, and all inbound/outbound packet
// processing through the RPMsg netif -- only 4 KiB of stack, not the 4096
// words (16 KiB) a byte/word mixup could easily be misread as. Raised to a
// full 16 KiB to give real headroom for the netif input path.
#define TCPIP_THREAD_STACKSIZE     16384
#define TCPIP_MBOX_SIZE              32
#define DEFAULT_UDP_RECVMBOX_SIZE    32
// DEFAULT_TCP_RECVMBOX_SIZE/DEFAULT_ACCEPTMBOX_SIZE/DEFAULT_RAW_RECVMBOX_SIZE
// (review finding, Minor): lwIP's own lwip/opt.h defaults every one of these
// to 0 when not set here. sys_arch.c's sys_mbox_new() (this port) passes
// that `size` argument straight to xQueueCreate(), whose own contract
// asserts uxQueueLength > 0 (configASSERT -> __BKPT on this port, an
// immediate hard fault, not a graceful failure). Nothing on this transport
// creates a TCP/raw/accept netconn today -- CycloneDDS here only ever opens
// UDP sockets -- so this path is unreachable in the current image, exactly
// as unreachable as it was on freertos_s32z2 before that target's own
// lwipopts.h set all five of these for the identical reason. Set now, before
// it is needed, rather than left as a latent __BKPT waiting for the first
// TCP/raw/accept caller.
#define DEFAULT_TCP_RECVMBOX_SIZE    32
#define DEFAULT_ACCEPTMBOX_SIZE      16
#define DEFAULT_RAW_RECVMBOX_SIZE    16
#define SO_REUSE                     1
#define LWIP_SO_RCVTIMEO             1

// ---- Thread priorities (review finding, Important #1) ----
//
// Previously unset here, so both TCPIP_THREAD_PRIO and DEFAULT_THREAD_PRIO
// silently took lwIP's own lwip/opt.h default of 1 -- the exact priority
// FreeRTOS gives every DDS/controller pthread on this port (tskIDLE_
// PRIORITY + 1; see include/platform/freertos/x5h/pthread.h). With
// configUSE_TIME_SLICING == 0 for this target (rcar_bsp's own
// FreeRTOSConfig.h), FreeRTOS never round-robins ready tasks of equal
// priority on a tick -- a ready task only yields to an equal-priority peer
// when it blocks or calls taskYIELD(). Tying the tcpip thread to the same
// priority as the DDS/controller pthreads therefore risked exactly the
// starvation this review is about: whichever of them the scheduler picked
// first could hold the CPU indefinitely.
//
// This target's real priority map (see rpmsg_transport.c's own header
// comment on RPMSG_POLL_TASK_PRIORITY for the full picture and how it was
// verified):
//   31  rpmsg_vdev_hb    (transient, deleted after vdev bring-up)
//   30  actuation_task   (blocked forever in pthread_join once startup runs)
//    5  rpmsg_poll_task  (TCPIP_THREAD_PRIO + 1, this file's own transport)
//    4  tcpip_thread     (TCPIP_THREAD_PRIO, set here)
//    3  FreeRTOS timer service (configTIMER_TASK_PRIORITY, vendor-fixed)
//    1  DDS/controller pthreads (tskIDLE_PRIORITY + 1, see pthread.h)
//
// TCPIP_THREAD_PRIO=4: strictly above the DDS/controller pthreads (1) so
// the tcpip thread is never starved by them under configUSE_TIME_SLICING=0.
// DEFAULT_THREAD_PRIO=(TCPIP_THREAD_PRIO + 1): mirrors
// freertos_s32z2/include/.../lwipopts.h's own DEFAULT_THREAD_PRIO-above-
// TCPIP_THREAD_PRIO placement (there, DEFAULT_THREAD_PRIO is where NXP's
// eth_port.c RX-poll thread actually runs). Nothing in this lwIP tree
// spawns a thread at DEFAULT_THREAD_PRIO today (SNMP, its only in-tree
// consumer, is not built here), so this is currently unused, defence-in-
// depth sizing -- but it is derived from, not independent of,
// TCPIP_THREAD_PRIO, so it cannot silently drift back to lwIP's own
// default-1 if TCPIP_THREAD_PRIO is ever changed here without a matching
// edit. The value happens to equal RPMSG_POLL_TASK_PRIORITY (5): both
// represent "one priority level above the tcpip thread, for whatever
// services this netif's I/O promptly," so sharing the number is
// intentional, not a coincidence to be tidied away.
//
// Scoped to this project's own concern only -- whether the controller
// pthread itself (priority 1) should instead be raised above the tcpip
// thread is explicitly OUT OF SCOPE here: that pthread is spawned via
// include/platform/freertos/x5h/pthread.h's pthread_create(), a POSIX-
// compat shim shared with other platforms, and repricing it is a separate,
// larger change this lwipopts.h fix does not make. What this fix guarantees
// is only that the tcpip thread -- and therefore RX delivery into it -- is
// never starved by the controller/DDS pthreads sitting at priority 1.
#define TCPIP_THREAD_PRIO             4
#define DEFAULT_THREAD_PRIO          (TCPIP_THREAD_PRIO + 1)

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
