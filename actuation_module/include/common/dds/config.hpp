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
  cfg.max_msg_size = 1400;

  // Discovery
  cfg.participantIndex = DDSI_PARTICIPANT_INDEX_AUTO;
  cfg.maxAutoParticipantIndex = 60;
  cfg.allowMulticast = DDSI_AMC_SPDP;

  // Trace
  cfg.tracefp = NULL;
  cfg.tracefile = const_cast<char *>("stderr");
#if CONFIG_DDS_LOG_LEVEL == 2
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
