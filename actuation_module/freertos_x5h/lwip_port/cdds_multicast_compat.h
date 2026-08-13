// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Multicast-socket-option compatibility shim for building CycloneDDS's own
// target library (scripts/build-cdds-target.sh's standalone cross-build).
//
// lwipopts.h sets LWIP_IGMP=0 (and, following from that, the default
// LWIP_MULTICAST_TX_OPTIONS=0) -- correct for this transport, since Task 6's
// RPMsg netif has no multicast-capable link layer (see lwipopts.h's own
// comment). With those off, lwip/sockets.h does not define
// IP_MULTICAST_TTL / IP_MULTICAST_IF / IP_MULTICAST_LOOP /
// IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP / struct ip_mreq at all -- but
// CycloneDDS's src/core/ddsi/src/ddsi_udp.c
// (set_mc_options_transmit_ipv4[_if](), joinleave_asm_mcgroup()) references
// all of them unconditionally, with no CycloneDDS build option to compile
// them out. Without this shim, CycloneDDS's own target library does not
// compile at all, regardless of our lwipopts.h.
//
// Force-included (-include) only for the standalone CycloneDDS cross-build;
// not needed by the main actuation_x5h link, since libddsc.a is linked
// there as a pre-built static library, not compiled from source (see
// CMakeLists.txt's "Pre-built CycloneDDS target library" section).
//
// Every definition here is #ifndef-guarded against lwip/sockets.h's own
// (LWIP_IGMP=1) definitions, using the same literal values, so this becomes
// a pure no-op if lwIP is ever reconfigured to define them itself.
// setsockopt() calls using these values still resolve through
// lwip_setsockopt()'s own IGMP-disabled code path at runtime (a graceful
// no-op/ENOPROTOOPT, not a crash) -- acceptable because this transport
// genuinely has no multicast group to join, and network bring-up stays
// stubbed until Task 6 regardless of this shim.
#ifndef CDDS_MULTICAST_COMPAT_H_
#define CDDS_MULTICAST_COMPAT_H_

#include "lwip/inet.h"  // struct in_addr

#ifndef IP_MULTICAST_TTL
#define IP_MULTICAST_TTL   5
#endif
#ifndef IP_MULTICAST_IF
#define IP_MULTICAST_IF    6
#endif
#ifndef IP_MULTICAST_LOOP
#define IP_MULTICAST_LOOP  7
#endif
#ifndef IP_ADD_MEMBERSHIP
#define IP_ADD_MEMBERSHIP  3
#endif
#ifndef IP_DROP_MEMBERSHIP
#define IP_DROP_MEMBERSHIP 4
#endif

#ifndef CDDS_MULTICAST_COMPAT_HAVE_IP_MREQ
#define CDDS_MULTICAST_COMPAT_HAVE_IP_MREQ 1
struct ip_mreq {
  struct in_addr imr_multiaddr;
  struct in_addr imr_interface;
};
#endif

#endif  // CDDS_MULTICAST_COMPAT_H_
