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

  // CONFIG_DDS_MAX_MSG_SIZE / CONFIG_DDS_MAX_REXMIT_MSG_SIZE /
  // CONFIG_DDS_FRAGMENT_SIZE (Task 21): shared by every platform target
  // (S32Z2, POSIX, Zephyr, the host unit-test build) *and* the Linux
  // dds_pub/dds_sub edge-ECU peer, all built from this same header, so these
  // three are opt-in knobs, not a hardcoded value -- a target that sets none
  // of them must keep exactly today's numbers (CONFIG_DDS_MAX_MSG_SIZE
  // defaults to the pre-existing literal 1400 below; CONFIG_DDS_MAX_REXMIT_
  // MSG_SIZE and CONFIG_DDS_FRAGMENT_SIZE are simply never assigned when
  // unset, leaving ddsi_config_init_default()'s own values -- 1456 B and
  // 1344 B, cyclonedds/src/core/ddsi/defconfig.c:20-21, matching the
  // documented defaults in ddsi__cfgelems.h:272,284 -- untouched).
  //
  // Only the X5H firmware (freertos_x5h/CMakeLists.txt) and its arm64 Linux
  // peer (freertos_x5h/scripts/build-edge-ecu-peer-arm64.sh) set all three,
  // because both ends sit on the same frozen 462-byte RPMsg-netif MTU and
  // setting only one end still IP-fragments on the other. Derivation, sized
  // for that link only:
  //
  //   UDP payload ceiling = MTU - IPv4 header - UDP header
  //                        = 462 - 20 - 8 = 434 B.
  //   The 20 B IPv4 figure is this stack's actual on-wire header, not a
  //   textbook number: lwip/src/include/lwip/prot/ip4.h defines
  //   IP_HLEN == 20, and ip4.c's ip4_output() (`u16_t ip_hlen = IP_HLEN;`)
  //   only grows that beyond IP_HLEN when a caller supplies IP options.
  //   IP_OPTIONS_SEND itself is not a user knob this port sets or leaves
  //   unset -- lwip/src/include/lwip/ip4.h defines it as the derived macro
  //   `#define IP_OPTIONS_SEND (LWIP_IPV4 && LWIP_IGMP)`; the real reason
  //   it evaluates false here is lwip_port/lwipopts.h's `LWIP_IGMP 0`
  //   (this point-to-point RPMsg link has no multicast-capable link layer,
  //   so IGMP has nothing to do -- see that file's own comment). So every
  //   IPv4 header this stack emits is exactly 20 B, no options.
  //
  //   CONFIG_DDS_MAX_MSG_SIZE: ddsi__cfgelems.h:260-262/269-270 defines
  //   MaxMessageSize as "the maximum size of the UDP payload that Cyclone
  //   DDS will generate" -- i.e. it already IS the whole-datagram ceiling,
  //   headers and all -- so it is set to the 434 B UDP payload ceiling
  //   directly.
  //
  //   CONFIG_DDS_FRAGMENT_SIZE: FragmentSize is the size of the *sample*
  //   data placed in one DATA_FRAG submessage, separate from
  //   MaxMessageSize. Fix-round-1 CORRECTION (this replaces a first draft
  //   that shipped as 366 B and was rejected in review as still
  //   IP-fragmenting on exactly the traffic this task exists to fix --
  //   Trajectory/~908 B and Odometry/~724 B, Linux to CR52, the direction
  //   reporting no_firstcontact): the first draft's 68 B overhead --
  //   ddsi_transmit.c's own ddsi_create_fragment_message() comment, "/*
  //   INFO_TS: 12 bytes, ddsi_rtps_datafrag_t: 36 bytes ... */", plus the
  //   20 B ddsi_rtps_header_t -- was real but incomplete in two ways:
  //
  //   1. Payload alignment. ddsi_xmsg_serdata() (ddsi_xmsg.c:640-645) pads
  //      the fragment's payload length with `align4u()` before it goes
  //      into the wire iovec (`m->refd_payload = ddsi_serdata_to_ser_ref
  //      (serdata, off, len4, ...)`), and ddsi_xmsg_submsg_setnext()
  //      (ddsi_xmsg.c:474) asserts `(plsize % 4) == 0` on exactly that
  //      padded length -- the padding is mandatory and is transmitted, not
  //      an internal bookkeeping artefact. A FragmentSize that is not
  //      itself a multiple of 4 silently grows by up to 3 B on the wire.
  //
  //   2. Directed retransmits carry an extra submessage. When a pack's
  //      destination differs from what came before it (ddsi_xmsg.c's
  //      xpack-append logic, ~line 1543-1566), an INFO_DST submessage is
  //      inserted and `sz += sizeof (*dst)`; `dst` is a
  //      `ddsi_rtps_info_dst_t *` (ddsi__protocol.h:124-127), 4 B
  //      submessage header + 12 B guid_prefix = 16 B. Confirmed this
  //      applies to the recovery path that matters here:
  //      ddsi_xmsg_setdst_prd() -- called from the directed-retransmit
  //      branch at ddsi_receive.c:1058 (`ddsi_enqueue_sample_wrlock_held
  //      (wr, seq, sample.serdata, prd, 0)` with prd set) -- sets
  //      `dstmode = NN_XMSG_DST_ONE` via ddsi_xmsg_setdst1(), which is
  //      exactly the mode the xpack-append check above tests for. This
  //      matters more than it looks: this task's entire premise is that a
  //      lost RTPS fragment is individually NACKed and retransmitted,
  //      unlike a lost IP fragment that destroys its whole datagram --
  //      but if the retransmission itself IP-fragments, recovery is
  //      all-or-nothing too, and the premise breaks exactly when it is
  //      needed. (retransmit_merging = DDSI_REXMIT_MERGE_ALWAYS below
  //      routes *merged* retransmits through a different, prd == NULL
  //      path, but merging requires `assumed_in_sync`, which is false
  //      precisely during first contact -- the failure this task chases.)
  //
  //   Corrected budget: 20 (RTPS header) + 16 (INFO_DST, worst case: a
  //   directed retransmit) + 12 (INFO_TS) + 36 (DATA_FRAG header) = 84 B.
  //   434 - 84 = 350, which is not a multiple of 4 (see point 1 above), so
  //   round down to the nearest multiple of 4:
  //     CONFIG_DDS_FRAGMENT_SIZE = 348 B.
  //   Verified against both budgets this now has to fit inside:
  //     worst case (directed retransmit of fragment 0):
  //       20 + 16 + 12 + 36 + 348 = 432 B <= 434 B (2 B to spare, the
  //       remainder of rounding 350 down to a multiple of 4).
  //     common case (no INFO_DST, i.e. not a directed retransmit):
  //       20 + 12 + 36 + 348 = 416 B <= 434 B.
  //   No frame-count cost from the drop 366 -> 348: Trajectory (~908 B)
  //   still needs only 3 fragments either way (3 * 348 = 1044 >= 908).
  //
  //   Constraint from ddsi_transmit.c:387 (`nf_in_submsg = max_msg_size /
  //   fragment_size`): fragment_size must not exceed max_msg_size or that
  //   division truncates to 0 (the code then clamps back up to 1, so this
  //   is a robustness/efficiency requirement, not an outright div-by-flow
  //   hazard, but the constraint is still honoured here: 348 <= 434, giving
  //   nf_in_submsg = 1 -- exactly one RTPS fragment per wire message, which
  //   matches the tight budget above).
  //
  //   CONFIG_DDS_MAX_REXMIT_MSG_SIZE: MaxRexmitMessageSize is the same
  //   "maximum UDP payload" ceiling, applied to retransmits
  //   (ddsi__cfgelems.h:272-274,282). Retransmits cross the identical
  //   462-byte link, so it gets the identical ceiling: 434 B (and, per the
  //   worst-case check above, the corrected 348 B FragmentSize now
  //   actually fits under it with the INFO_DST accounted for).
  //
  //   Caveat (ddsi__cfgelems.h:266-268, verbatim: "especially for very low
  //   values of MaxMessageSize... larger payloads may sporadically be
  //   observed (currently up to 1192 B)"): CycloneDDS treats MaxMessageSize
  //   as best-effort, not a hard cap, so this reduces IP fragmentation, it
  //   does not eliminate it -- which is why item 2 (lwip_port/lwipopts.h's
  //   IP_REASS_* sizing) also exists; this item alone was never meant to be
  //   sufficient on its own.
  //
  //   Caveat (ddsi__cfgelems.h:291-294 + ddsi_receive.c:473): the DDSI spec
  //   wants fragments >= 1025 B, but Cyclone only enforces that when
  //   DDSI_SC_STRICT_P(config) is true, i.e. when
  //   cfg.standards_conformance <= DDSI_SC_STRICT. This file never assigns
  //   cfg.standards_conformance, so it keeps ddsi_config_init_default()'s
  //   own default (defconfig.c: `INT32_C(2)`, i.e. DDSI_SC_LAX --
  //   ddsi_config.h's enum has PEDANTIC=0 < STRICT=1 < LAX=2), and
  //   DDSI_SC_STRICT_P is false. So this build's own 348 B FragmentSize
  //   (well under 1025) is never rejected by ddsi_receive.c:473. Both ends
  //   of this link are the same CycloneDDS build with the same
  //   (never-strict) setting, so this is a non-issue for interop between
  //   them; it would only matter for interop with a third-party, spec-
  //   strict DDS implementation, which this point-to-point link does not
  //   have.
  //
  //   Fix-round-1 REMOVED CAVEAT: the first draft warned that a 32 B
  //   inline-QoS allowance (ddsi_create_fragment_message()'s own
  //   `expected_inline_qos_size`: statusinfo 8 + keyhash 20 + sentinel 4)
  //   was unbudgeted and could push a single-fragment datagram over the
  //   ceiling if a keyed topic attached a keyhash. Checked directly against
  //   this build's generated topic descriptors (build/*/autoware_msgs/
  //   *.c): every one of them, including Trajectory.c and Odometry.c, has
  //   `.m_nkeys = 0u` -- there are no keyed topics anywhere in this
  //   codebase, so GenerateKeyhash (default off) never has a key to hash
  //   in the first place, no reader ever requests one, and
  //   DDS_HAS_SECURITY is undefined for this build
  //   (cdds_target_out/include/dds/features.h: `/* #undef DDS_HAS_SECURITY
  //   */`) so the security-prefix submessages that would otherwise also
  //   ride inside inline QoS do not exist either. Inline QoS is not a risk
  //   on this link and is not budgeted for.
  //
  //   Fix-round-1 CONFIRMED NON-ISSUE: SPDP (participant discovery) is
  //   never RTPS-fragmented regardless of FragmentSize --
  //   ddsi_transmit.c:917-923/466-471 special-case it -- and
  //   ddsi_receive.c:2701-2706 drops a fragmented SPDP sample outright if
  //   one ever arrived. This matters here because a FragmentSize change
  //   could otherwise be suspected of touching discovery too; it does not
  //   -- discovery was never broken by this change, in either direction.
  //
  // CORRECTION: an earlier draft of this derivation, following a citation
  // of cyclonedds/src/core/ddsi/defconfig.c:19-21, took the CycloneDDS
  // default MaxRexmitMessageSize as 1400 B. The actual source at that
  // location reads `cfg->max_rexmit_msg_size = UINT32_C (1456);` -- 1456 B,
  // matching ddsi__cfgelems.h's own documented default ("1456 B"). This
  // comment uses the verified 1456 B figure; 1400 B was never the real
  // default, only max_msg_size's separate, unrelated override happens to
  // equal 1400. NOTE: the only #ifndef default in this file is
  // CONFIG_DDS_MAX_MSG_SIZE's own (1400, immediately below) -- there is no
  // equivalent #ifndef default for MaxRexmitMessageSize/FragmentSize, by
  // design (see the #if defined(...)-only guards below: an earlier version
  // of this note incorrectly implied one existed for 1456 too).
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
