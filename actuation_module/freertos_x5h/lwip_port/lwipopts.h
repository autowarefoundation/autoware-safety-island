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
// PBUF_POOL_SIZE (Task 21 fix round 1, raised from 64) -- this is the real,
// hard ceiling on pbufs in flight anywhere in the receive path at once: one
// pbuf per queued TCPIP_MBOX_SIZE entry, one per queued
// DEFAULT_UDP_RECVMBOX_SIZE entry (per socket), and one per
// IP_REASS_MAX_PBUFS slot mid-reassembly, ALL drawn from this single pool
// (glue_rx_deliver(), rpmsg_netif.c, does the one `pbuf_alloc(PBUF_RAW, len,
// PBUF_POOL)` per inbound RPMsg frame that ultimately backs all three uses).
// Review finding (Important #2, fix round 1): a first draft left this at 64
// and sized TCPIP_MBOX_SIZE/DEFAULT_UDP_RECVMBOX_SIZE to 64 each on the
// claim that "64 is the largest value either mailbox can usefully take,
// since the pool is 64" -- true in isolation, false once
// IP_REASS_MAX_PBUFS and the ~3 real UDP sockets are counted too: worst-case
// simultaneous demand on the pool is TCPIP_MBOX_SIZE(64) + up to
// 3 * DEFAULT_UDP_RECVMBOX_SIZE(64) + IP_REASS_MAX_PBUFS(24) = 280 pbufs
// against a pool of only 64, so the pool -- not either mailbox depth --
// silently caps effective burst absorption well below the "64" the old
// comment implied, to roughly 40 pbufs by the review's estimate.
// Doubled to 128 rather than just correcting the comment: the cost is small
// (128 * PBUF_POOL_BUFSIZE(520 B) = 65536 B total, i.e. +64 * 520 B =
// +33280 B / +32.5 KiB over the previous 64) against the 10 MiB Core1 slot
// (~1.7 MiB free at 83.0%, see check-image-budget.sh output), and it
// directly raises the real ceiling instead of leaving mailbox/reassembly
// sizing arguments that describe a headroom this pool cannot actually
// deliver. This does not remove the contention -- TCPIP_MBOX_SIZE,
// DEFAULT_UDP_RECVMBOX_SIZE and IP_REASS_MAX_PBUFS below still compete for
// the same 128, and the worst-case sum above still exceeds it -- it raises
// the point at which that competition first bites (rx_drop_no_pbuf, a
// distinct and already-visible counter, see rpmsg_netif_get_stats()) rather
// than pretending the competition is not there. What would make this wrong:
// a real workload that needs more than ~2x the effective absorption the old
// 64-deep pool gave; nothing observed on this link so far implies that.
#define PBUF_POOL_SIZE              128
#define PBUF_POOL_BUFSIZE           520   /* one full frame + pbuf overhead */
// MEMP_NUM_REASSDATA / IP_REASS_MAX_PBUFS (Task 21) -- previously left at
// lwIP's own defaults (5 / 10), which this file never mentioned even though
// they matter more than most values it does spell out: with the pre-Task-21
// CONFIG_DDS_MAX_MSG_SIZE (1400 B, ~4 IP fragments on this link's 434 B UDP
// payload -- see the TCPIP_MBOX_SIZE comment above for that arithmetic), 10
// pbufs across ALL concurrent reassemblies was only about two datagrams in
// flight before lwIP started evicting the oldest partial one -- and losing
// one fragment, or an evicted partial, silently discards the whole
// datagram, with no IP-level retransmission (common/dds/config.hpp's own
// Task 21 comment).
//
// Task 21's item 1 (CONFIG_DDS_MAX_MSG_SIZE/FRAGMENT_SIZE=434/348) makes
// every RTPS-level fragment fit in one UDP datagram of its own (<=432 B,
// see common/dds/config.hpp's derivation), so a single RTPS fragment never
// itself needs IP reassembly -- but ddsi__cfgelems.h's own documented
// caveat -- "especially for very low values of MaxMessageSize... larger
// payloads may sporadically be observed (currently up to 1192 B)" -- means
// CycloneDDS's ceiling is best-effort, not absolute, so reassembly capacity
// is still needed for that residual case:
//   worst-case residual fragments per datagram = ceil(1192 / 434) = 3.
// MEMP_NUM_REASSDATA=8 budgets for up to 8 such oversized datagrams
// reassembling concurrently (a discovery burst can carry the one SPDP
// participant announce plus several SEDP endpoint announces close
// together; this codebase's DDS entity count is small enough that 8 is a
// generous, not exact, ceiling on how many could plausibly overlap).
// IP_REASS_MAX_PBUFS = MEMP_NUM_REASSDATA * 3 = 24 follows directly from
// that pairing. Sanity-checked against lwip/src/include/lwip/opt.h's own
// documented invariant on this value ("configure PBUF_POOL_SIZE >
// IP_REASS_MAX_PBUFS so the stack can still receive packets even with the
// maximum amount of fragments enqueued for reassembly" -- the doubled
// "PBUF_POOL_SIZE > 2 * IP_REASS_MAX_PBUFS" variant of that rule applies
// only with IPv6 reassembly also enabled, and LWIP_IPV6=0 above): 64 > 24
// holds with real margin (2.7x, not just >1x), leaving pbufs available for
// the TCPIP_MBOX_SIZE/socket-recvmbox queues above rather than reassembly
// alone being able to claim the entire pool.
//
// What would make this wrong: if a real discovery burst turns out to carry
// materially more than 8 concurrently-oversized datagrams, the 9th+ would
// still evict the oldest partial reassembly exactly as before this change,
// just at a higher watermark -- diagnosable the same way Task 18 found the
// original mailbox-overflow defect (rx_drop_input_err/the reassembly
// timeout path), not a silent regression.
#define MEMP_NUM_REASSDATA            8
#define IP_REASS_MAX_PBUFS           24
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
// TCPIP_MBOX_SIZE / DEFAULT_UDP_RECVMBOX_SIZE (Task 21, raised from 32) --
// derivation, and a fix-round-1 CORRECTION to it (Important #1): a first
// draft of this comment justified the raise as "item 1 cuts the common case
// to ONE frame per datagram... so the same absolute mailbox depth now
// absorbs far more datagrams per burst than before." That is wrong for
// TCPIP_MBOX_SIZE and backwards for DEFAULT_UDP_RECVMBOX_SIZE; corrected
// below. The 32 -> 64 raise itself was directionally right and is
// unchanged; only the reasoning was.
//
// rpmsg_poll_task (priority 5, this file's TCPIP_THREAD_PRIO + 1) strictly
// preempts tcpip_thread (priority 4) -- see rpmsg_transport.c's own
// priority-map comment. (CORRECTED: this used to attribute that preemption
// to configUSE_TIME_SLICING == 0, which is the wrong reason for a right
// claim. A strictly higher-priority runnable task preempts a lower one
// under configUSE_PREEMPTION == 1 alone; time slicing has nothing to do
// with it, and only ever governs EQUAL-priority tasks. The sizing
// conclusion below is unaffected.) So whatever the poll task drains from
// the vring and pushes into
// TCPIP_MBOX in one pass -- from the moment it wakes until its own
// vTaskDelay(1) -- tcpip_thread gets zero chance to drain any of it; the
// mailbox has to be able to hold that whole pass by itself.
//
// Before Task 21, common/dds/config.hpp's CONFIG_DDS_MAX_MSG_SIZE override
// (1400 B, unchanged by that macro's own default) meant almost every
// full-size DDS datagram IP-fragmented into ~4 pieces on this 434 B UDP
// payload (462 MTU - 20 IPv4 - 8 UDP): "eight datagrams inside one tick"
// was already enough 4-frame datagrams to fill the old TCPIP_MBOX_SIZE=32
// exactly -- a zero-margin fit, not a deliberately chosen target, and
// exactly what Task 18's board run saw (rx_drop_input_err climbing to 841
// during a single DDS discovery burst while the mailbox stayed full).
//
// TCPIP_MBOX_SIZE holds one entry per LINK FRAME, not per DDS datagram:
// glue_rx_deliver() (rpmsg_netif.c) does one `pbuf_alloc(PBUF_RAW, len,
// PBUF_POOL)` per inbound RPMsg message and hands it straight to
// netif->input(), which is what queues into TCPIP_MBOX -- before IP
// reassembly, before UDP demux. Item 1 (CONFIG_DDS_MAX_MSG_SIZE/
// FRAGMENT_SIZE=434/348) does not change how many link frames a given
// sample costs: the total bytes on the wire for a sample are roughly the
// same either way, and this link's per-frame payload capacity (~434 B) is
// unchanged, so the total frame count a sample needs is roughly conserved
// regardless of whether CycloneDDS internally slices it into one big
// IP-fragmented UDP datagram or several small RTPS-fragmented ones -- the
// brief's own Fact 1 established exactly this (a 1400 B payload costs the
// same ~4 link frames whoever splits it). So item 1 relieves close to zero
// TCPIP_MBOX pressure; it is the reassembly-loss fix (item 1's real job),
// not a mailbox-pressure fix.
//
// DEFAULT_UDP_RECVMBOX_SIZE goes the OTHER way under item 1, and by more
// than a little: before, a large sample (e.g. Trajectory ~908 B, Odometry
// ~724 B -- exactly the Linux->CR52 traffic behind the no_firstcontact
// failure this task exists to fix) arrived as its IP fragments got
// silently reassembled by lwIP into ONE complete UDP datagram before ever
// reaching the socket layer, so it cost few DEFAULT_UDP_RECVMBOX entries
// (one per RTPS-level fragment CycloneDDS itself used, not per IP
// fragment). After item 1, each RTPS fragment IS its own complete,
// unfragmented UDP datagram (see common/dds/config.hpp's derivation: a
// single fragment now fits under the IP MTU on its own), so it is
// delivered to the socket recvmbox directly, with NO IP reassembly step to
// coalesce it with its siblings first -- a sample that used to need N
// small reassembly buffers behind ~1 recvmbox entry now needs N separate
// recvmbox entries instead. Trajectory (~908 B / 348 B-per-fragment = 3
// fragments) roughly triples its DEFAULT_UDP_RECVMBOX demand versus the
// pre-Task-21 shape.
//
// Both mailboxes are raised to 64 anyway, and 64 is still the right number
// -- but for a different reason than "absorbs more datagrams per burst":
// PBUF_POOL_SIZE (above, raised to 128 in the same fix round -- see that
// macro's own comment for why 64 was already an under-sized shared ceiling
// once DEFAULT_UDP_RECVMBOX_SIZE's newly-higher real demand is counted) is
// the actual system-wide ceiling on pbufs in flight across TCPIP_MBOX,
// every UDP recvmbox, and IP_REASS_MAX_PBUFS combined; a single mailbox
// depth cannot productively exceed what that shared pool can back, and 64
// remains the largest depth either mailbox can request without first
// contending against the pool's other consumers on paper (they still
// contend in practice -- see PBUF_POOL_SIZE's comment -- raising the
// mailbox depth further would not change that). RAM cost: this port's
// sys_mbox_new() (lwip_port/sys_arch.c) backs each mailbox with a FreeRTOS
// queue of `void *`, i.e. one 4-byte pointer per slot on this 32-bit
// Cortex-R52 target -- (64-32) * 4 B = 128 B for TCPIP_MBOX_SIZE, and up to
// another 128 B per UDP PCB for DEFAULT_UDP_RECVMBOX_SIZE (this file's own
// MEMP_NUM_UDP_PCB comment above puts real usage at ~3 sockets, i.e. ~384 B
// there in practice, with a worst-case ceiling of 8 * 128 B = 1024 B if
// every possible PCB were opened). These queues are allocated via
// pvPortMalloc() (rcar_bsp's heap_useNewlib.c), which serves newlib's own
// [HeapBase, HeapLimit) heap region -- NOT this file's MEM_SIZE (256 KiB),
// which is lwIP's own separate internal heap for pbufs/PCBs/etc. Both are
// carved out of the same 10 MiB Core1 slot at link time either way, so the
// "negligible against the 10 MiB slot" conclusion is unaffected; only the
// specific pool named was wrong.
//
// For this choice to be wrong: a burst would have to sustain, for longer
// than tcpip_thread needs to drain 64 queued entries (each drain is one
// UDP/IP receive-processing pass, not itself rate-limited by anything on
// this link), a production rate high enough to refill the mailbox faster
// than that drain -- at which point no finite mailbox depth fixes it and
// the real fix would be elsewhere (e.g. throttling discovery traffic
// itself), not a bigger number here.
#define TCPIP_MBOX_SIZE              64
#define DEFAULT_UDP_RECVMBOX_SIZE    64
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
// PRIORITY + 1; see include/platform/freertos/x5h/pthread.h). Tying the
// tcpip thread to the same priority as the DDS/controller pthreads is a
// real hazard and the raise below is right, but state WHY correctly.
//
// CORRECTED: this paragraph used to argue that "FreeRTOS never round-robins
// ready tasks of equal priority ... a ready task only yields to an
// equal-priority peer when it blocks or calls taskYIELD()", and concluded
// that whichever task the scheduler picked "could hold the CPU
// indefinitely". Only the first clause is true, and only as far as it goes:
// configUSE_TIME_SLICING == 0 removes the TICK-driven switch
// (rcar_bsp/FreeRTOS/Source/tasks.c:4810-4841) and nothing else. Task
// selection still runs listGET_OWNER_OF_NEXT_ENTRY (tasks.c:178-193 on this
// build, which has configUSE_PORT_OPTIMISED_TASK_SELECTION == 0), so the
// ready list rotates on EVERY context switch -- including the ones caused
// by a higher-priority task becoming ready or blocking again, which on this
// port happens constantly. So equal-priority tasks do interleave, and an
// equal-priority tie costs a peer roughly half the CPU rather than all of
// it. The full derivation, with the kernel's own wording, is in
// freertos_main.cpp's ACTUATION_TASK_PRIORITY block; it is not repeated
// here.
//
// The raise is unaffected by that correction, because what it buys comes
// from STRICT priority, not from time slicing: at 4 the tcpip thread
// preempts every application task outright and runs whenever it has work,
// which is the property this file needs. Halving its CPU against a
// CPU-bound peer would already be unacceptable for RX delivery; being
// preempted by one would be worse. Neither is possible at 4.
//
// This target's real priority map (see rpmsg_transport.c's own comment on
// the poll task priority for the full picture and how it was verified):
//   31  rpmsg_vdev_hb    (transient, deleted after vdev bring-up)
//    5  rpmsg_poll_task  (TCPIP_THREAD_PRIO + 1, this file's own transport)
//    4  tcpip_thread     (TCPIP_THREAD_PRIO, set here)
//    3  FreeRTOS timer service (configTIMER_TASK_PRIORITY, vendor-fixed)
//    2  actuation_task   (ACTUATION_TASK_PRIORITY, freertos_main.cpp) --
//       and every CycloneDDS ddsrt thread with it, since those inherit the
//       priority of the task that creates them (the Controller constructor,
//       on actuation_task) rather than pthread.h's
//    1  the controller's own pthread and anything else pthread.h creates
//       (tskIDLE_PRIORITY + 1, see that file)
//
// CORRECTED (this replaces a line of this very map that a later board
// session found to be false): the entry above actuation_task's used to read
// "30  actuation_task (blocked forever in pthread_join once startup runs)".
// The parenthetical is what let a 30 sit above the whole network stack
// unchallenged through a review, and it is the reason the actuation image
// never transmitted a frame. actuation_task does block in pthread_join --
// eventually, at src/main.cpp:57 -- but it first runs configure_network()
// and the entire Controller constructor (CycloneDDS participant + Eigen MPC)
// without blocking, and a runnable task is never preempted by a STRICTLY
// LOWER-priority one. (That is plain priority under configUSE_PREEMPTION;
// the original wording credited configUSE_TIME_SLICING == 0, which governs
// only equal priorities and is not what made a 30 fatal here.) So for that
// whole stretch the
// tcpip thread at 4 and the poll task at 5 simply did not run: the channel
// was announced and then nothing was ever transmitted, while Linux logged
// its virtio_rpmsg send path timing out waiting for the remote to return a
// tx buffer. "It blocks eventually" was never the property this map needed;
// "it cannot hold the CPU while the network must run" is, and the launcher
// now sits at 2 with a compile-time assertion in freertos_main.cpp holding
// it below both network tasks. Note also that TCPIP_THREAD_PRIO's own job
// grew as a result: at 4 it is above the DDS threads whether they land at 1
// (pthread.h) or 2 (inherited from the launcher).
//
// TCPIP_THREAD_PRIO=4: strictly above every task that runs application code
// -- the controller/DDS pthreads at 1, the CycloneDDS ddsrt threads and the
// actuation launcher at 2 -- so the tcpip thread cannot be starved or even
// merely delayed by any of them. Strictly above, not merely different: at
// EQUAL priority it would round-robin with a CPU-bound peer and get about
// half the CPU, which is already too little for RX delivery. The margin
// here comes from priority alone and needs no time-slicing argument.
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
// never starved by the application tasks below it. It guarantees nothing
// about tasks placed ABOVE it: holding the actuation launcher below this
// value is freertos_main.cpp's static_assert, not this file's, because the
// value that can drift is the launcher's.
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
