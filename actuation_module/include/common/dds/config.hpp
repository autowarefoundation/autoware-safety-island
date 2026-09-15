// Copyright (c) 2024-2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef COMMON__DDS_CONFIG_HPP_
#define COMMON__DDS_CONFIG_HPP_

#include <dds/ddsi/ddsi_config.h>
#include <dds/dds.h>
#include "platform/platform_config.h"

#include "common/logger/logger.hpp"
using namespace common::logger;

#if defined(CONFIG_NET_CONFIG_PEER_IPV4_ADDR)
static struct ddsi_config_peer_listelem cfg_peer
{
  nullptr,
  const_cast<char *>(CONFIG_NET_CONFIG_PEER_IPV4_ADDR)
};
#endif

static struct ddsi_config_network_interface_listelem cfg_iface
{
  nullptr,
  {
    0,        // automatic
    nullptr,  // name    } exactly one of these is set at runtime in
    nullptr,  // address } init_config(), depending on the selector form
    1,  // prefer_multicast
    1,  // presence_required
    DDSI_BOOLDEF_DEFAULT, // multicast
    {1, 0}
  }
};

// CycloneDDS selects an interface by OS name (strcmp) or by IP address (locator
// match) — these are different config fields. CONFIG_DDS_NETWORK_INTERFACE is
// overloaded: POSIX/Zephyr pass a name ("lo"), the S32Z2 board passes its IP.
// Route a dotted-quad IPv4 literal to the address field; anything else is a name.
static bool dds_selector_is_ipv4(const char * s)
{
  int groups = 0, digits = 0, octet = 0;
  for (const char * p = s; ; ++p) {
    if (*p >= '0' && *p <= '9') {
      octet = octet * 10 + (*p - '0');
      if (++digits > 3 || octet > 255) return false;
    } else if (*p == '.' || *p == '\0') {
      if (digits == 0) return false;
      ++groups; digits = 0; octet = 0;
      if (*p == '\0') break;
    } else {
      return false;
    }
  }
  return groups == 4;
}

/**
 * @brief Initialize a given DDS configuration structure.
 * @param[out] cfg Configuration structure that will be filled.
 */
inline static void init_config(struct ddsi_config & cfg)
{
  log_debug("Initializing DDS configuration\n");

  // CONFIG_DDS_NETWORK_INTERFACE is a compile-time string literal. An empty
  // value is the documented "empty = auto" default (CMake) used by the host
  // edge-ECU peer: leave cfg.network_interfaces at the ddsi_config_init_default()
  // default so CycloneDDS auto-selects a suitable interface, rather than treating
  // it as a fatal misconfiguration.
  constexpr bool iface_configured = (sizeof(CONFIG_DDS_NETWORK_INTERFACE) > 1);

  if (iface_configured) {
    if (dds_selector_is_ipv4(CONFIG_DDS_NETWORK_INTERFACE)) {
      cfg_iface.cfg.address = const_cast<char *>(CONFIG_DDS_NETWORK_INTERFACE);
      log_info("Network interface (by IP address): %s\n", CONFIG_DDS_NETWORK_INTERFACE);
    } else {
      cfg_iface.cfg.name = const_cast<char *>(CONFIG_DDS_NETWORK_INTERFACE);
      log_info("Network interface (by name): %s\n", CONFIG_DDS_NETWORK_INTERFACE);
    }
  } else {
    log_info("Network interface: auto (CONFIG_DDS_NETWORK_INTERFACE empty)\n");
  }

  ddsi_config_init_default(&cfg);

  // Network interface — pin our descriptor only when one was configured;
  // otherwise leave the auto-selected default in place.
  if (iface_configured) {
    cfg.network_interfaces = &cfg_iface;
  }

  // cfg.enable_topic_discovery_endpoints = DDSI_BOOLDEF_FALSE;

  // Processing
  cfg.retransmit_merging = DDSI_REXMIT_MERGE_ALWAYS;
  // cfg.multiple_recv_threads = DDSI_BOOLDEF_FALSE;  // TODO: Check if this is required

  // Buffers
  cfg.rbuf_size = 8 * 1024;
  cfg.rmsg_chunk_size = 2 * 1024;

  // DDS datagram sizing. The rpmsg-eth link carries a 1500-byte MTU, so a
  // 1400-byte DDS message (the default below, 20 + 8 + 1400 = 1428 <= 1500)
  // leaves as one frame and the peer never IP-fragments. This equals the
  // Linux side's cyclonedds-x5h.xml (MaxMessageSize 1400B, FragmentSize
  // 1344B). The earlier 434/348 sizing for the 462-byte MTU is gone; do not
  // reintroduce it, check-dds-config.sh refuses it.
#ifndef CONFIG_DDS_MAX_MSG_SIZE
#define CONFIG_DDS_MAX_MSG_SIZE 1400
#endif
  cfg.max_msg_size = CONFIG_DDS_MAX_MSG_SIZE;
#undef CONFIG_DDS_MAX_MSG_SIZE  // scope the #ifndef default to this TU's use, not the rest of it
#if defined(CONFIG_DDS_MAX_REXMIT_MSG_SIZE)
  cfg.max_rexmit_msg_size = CONFIG_DDS_MAX_REXMIT_MSG_SIZE;
#endif
#if defined(CONFIG_DDS_FRAGMENT_SIZE)
  cfg.fragment_size = CONFIG_DDS_FRAGMENT_SIZE;
#endif

  // Discovery
  cfg.participantIndex = DDSI_PARTICIPANT_INDEX_AUTO;
  cfg.maxAutoParticipantIndex = 60;
  cfg.allowMulticast = DDSI_AMC_SPDP;
  // CONFIG_DDS_DISABLE_MULTICAST: the S32Z2 bench sits on a switched segment
  // where multicast is merely filtered by IGMP snooping, so leaving
  // allowMulticast at its DDSI_AMC_SPDP default (try multicast for SPDP,
  // unicast for everything else) is harmless there -- CONFIG_DDS_PEER alone
  // fixes discovery. A point-to-point RPMsg link (X5H) carries no multicast
  // capability at all at the netif layer, so a target on that link must
  // never attempt it; this opt-in override (default off, so S32Z2/POSIX/
  // Zephyr behavior is unchanged) forces allowMulticast fully off.
#if defined(CONFIG_DDS_DISABLE_MULTICAST) && (CONFIG_DDS_DISABLE_MULTICAST)
  cfg.allowMulticast = DDSI_AMC_FALSE;
#endif

  // Trace
  //
  // cfg.tracefile == "stderr" is honoured: ddsi_init.c:448-451 maps the literal
  // string "stderr" (case-insensitively) onto the C `stderr` FILE*, and
  // ddsi_init.c:463 hands that to dds_log_cfg_init() as the TRACE sink. On the
  // X5H CR52 that reaches the physical console, because the R-Car BSP's
  // rcar_bsp/.../drivers/serial/serial.c:249 defines a strong `_write()` that
  // ignores its `file` argument entirely and pushes every byte through
  // outbyte()/console_putc() -> uart_rcar_poll_out() -- i.e. stdout and stderr
  // are the same SCIF1 port. Two consequences that decide the mask below:
  //   - that write is a BUSY-POLLED, unbuffered, interrupt-free UART loop, so
  //     the cost of a trace line is paid as wall-clock stall time in whichever
  //     thread emitted it (here: a CycloneDDS ddsrt thread at FreeRTOS priority
  //     2 -- see freertos_main.cpp's ACTUATION_TASK_PRIORITY comment);
  //   - the port is 115200 8N1 (rcar_bsp/.../drivers/serial/scif.h:19), i.e.
  //     ~11.5 kB/s, ~100 lines/s at 100 characters per line. That is the entire
  //     budget.
  //
  // Level 3 (this file's addition) exists because level 2's DDS_LC_ALL cannot
  // be used on this target for a diagnostic run. DDS_LC_ALL includes
  // DDS_LC_TRACE, which is what gates ddsi_receive.c's RSTTRACE() -- several
  // lines per RECEIVED PACKET (see e.g. ddsi_receive.c:2600-2603 and 2669-2673,
  // one line per DATA/DATAFRAG submessage). The board has been measured at
  // tens of inbound frames per second under DDS load, so DDS_LC_ALL would ask
  // for multiples of the console's whole 11.5 kB/s budget and would stall the
  // DDSI threads for as long as it took to drain -- changing the very timing
  // the capture is meant to observe, and plausibly preventing the round trip
  // from ever forming. A trace that destroys the phenomenon is not a trace.
  //
  // Level 3 selects the discovery/liveliness surface instead. Note the mask
  // semantics that make this narrow rather than nominally narrow: dds_log.c's
  // dds_log_cfg_init() (log.c:158-168) sets `mask = tracemask | DDS_LOG_MASK`
  // and nothing else -- DDS_LC_TRACE is set only if it is asked for by name.
  // So DDS_LC_DISCOVERY does NOT drag DDS_LC_TRACE in with it, and every
  // RSTTRACE()/GVTRACE() call site stays silent. What stays visible is exactly
  // the evidence a peer-restart investigation needs:
  //   - ddsi_discovery_spdp.c's GVLOGDISC() in handle_spdp_alive() -- "SPDP ST0
  //     <guid> ... NEW" for a first sighting, "(update)" for a known one,
  //     "(no unicast address)" for a rejected one, and handle_spdp_dead()'s
  //     "SPDP ST<n> ... delete";
  //   - ddsi_lease.c:238's "lease expired: l %p guid ..." -- the single line
  //     that says whether the dead peer's proxy participant ever went away;
  //   - the SEDP endpoint and reader/writer match/unmatch lines (ELOGDISC in
  //     ddsi_endpoint.c / ddsi_proxy_endpoint.c), which is where "the readers
  //     never re-matched" either becomes visible or is disproved.
  // At steady state that is a handful of lines per minute; a discovery burst
  // for one new remote participant is a few hundred lines, i.e. a few seconds
  // of console. Bounded, and it only happens at the moment of interest.
  //
  // Level 3 is diagnostic-only and is NOT the default for any target: every
  // CMakeLists.txt in this repo still defaults CONFIG_DDS_LOG_LEVEL to 0, so
  // the shipping image is byte-identical to before this change. Build the
  // instrumented image with -DCONFIG_DDS_LOG_LEVEL=3.
  cfg.tracefp = NULL;
  cfg.tracefile = const_cast<char *>("stderr");
#if CONFIG_DDS_LOG_LEVEL == 3
    cfg.tracemask = DDS_LC_FATAL | DDS_LC_ERROR | DDS_LC_WARNING | DDS_LC_CONFIG |
      DDS_LC_DISCOVERY;
#elif CONFIG_DDS_LOG_LEVEL == 2
    cfg.tracemask = DDS_LC_ALL;
#elif CONFIG_DDS_LOG_LEVEL == 1
    cfg.tracemask = DDS_LC_FATAL | DDS_LC_ERROR | DDS_LC_WARNING | DDS_LC_CONFIG ;
#else
    cfg.tracemask = 0;
#endif

#if defined(CONFIG_NET_CONFIG_PEER_IPV4_ADDR)
  if (sizeof(CONFIG_NET_CONFIG_PEER_IPV4_ADDR) > 1) {
    cfg.peers = &cfg_peer;
    log_info("Adding peer: %s\n", CONFIG_NET_CONFIG_PEER_IPV4_ADDR);
  }
#endif
}

#endif  // COMMON__DDS_CONFIG_HPP_
