// Copyright (c) 2022-2023, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <memory>
#include <vector>
#include <pthread.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet.h>

#include "common/logger/logger.hpp"
using namespace common::logger;

#if defined(CONFIG_NET_DHCPV4)
/* Semaphore to indicate a lease has been acquired. */
static K_SEM_DEFINE(got_address, 0, 1);

static struct net_mgmt_event_callback mgmt_cb;
#endif  // CONFIG_NET_DHCPV4

static void net_if_cb(struct net_if * iface, void * user_data)
{
  auto ifs = reinterpret_cast<std::vector<struct net_if *> *>(user_data);
  ifs->push_back(iface);
}

static int setup_iface(
  struct net_if * iface, const char * addr, const char * gw, const char * netmask, uint16_t tag)
{
  struct in_addr inaddr;

  if (net_addr_pton(AF_INET, addr, &inaddr)) {
    log_error("Invalid address: %s", addr);
    return 1;
  }
  if (!net_if_ipv4_addr_add(iface, &inaddr, NET_ADDR_MANUAL, 0)) {
    log_error("Cannot add %s to interface %p", addr, iface);
    return 1;
  }

  if (net_addr_pton(AF_INET, gw, &inaddr)) {
    log_error("Invalid address: %s", gw);
    return 1;
  }
  net_if_ipv4_set_gw(iface, &inaddr);

  if (net_addr_pton(AF_INET, netmask, &inaddr)) {
    log_error("Invalid address: %s", netmask);
    return 1;
  }
  net_if_ipv4_set_netmask(iface, &inaddr);

#if defined(CONFIG_NET_VLAN)
  if (tag > 0) {
    int ret = net_eth_vlan_enable(iface, tag);
    if (ret < 0) {
      log_error("Cannot set VLAN tag %d to interface %p", tag, iface);
      return 1;
    }
  }
#endif

  return 0;
}

#if defined(CONFIG_NET_DHCPV4)
static void handler(
  struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
  log_debug(">>> DHCP Handler Entered Event: %u\n", mgmt_event);

  int i = 0;

  if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
    log_error("Not a DHCP event\n");
    return;
  }

  for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
    char buf[NET_IPV4_ADDR_LEN];

    if (iface->config.ip.ipv4->unicast[i].addr_type != NET_ADDR_DHCP) {
      continue;
    }

    log_info("  IP address: %s\n",
      net_addr_ntop(AF_INET,
          &iface->config.ip.ipv4->unicast[i].address.in_addr,
              buf, sizeof(buf)));
    log_info("  Lease time: %u seconds\n",
       iface->config.dhcpv4.lease_time);
    log_info("  Netmask:    %s\n",
      net_addr_ntop(AF_INET,
               &iface->config.ip.ipv4->netmask,
               buf, sizeof(buf)));
    log_info("  Gateway:    %s\n",
      net_addr_ntop(AF_INET,
             &iface->config.ip.ipv4->gw,
             buf, sizeof(buf)));

    k_sem_give(&got_address);
  }
}
#endif  // CONFIG_NET_DHCPV4

int configure_network(void)
{
  // Initialize network interfaces
  std::vector<struct net_if *> ifs {};
  net_if_foreach(net_if_cb, &ifs);
  log_info("Network interfaces found: %d\n", ifs.size());

  if (ifs.size() >= 1 && sizeof(CONFIG_NET_IFACE1_ADDR) > 1) {
    log_info("Configuring network interface 1\n");
    int ret = setup_iface(
      ifs[0],
      CONFIG_NET_IFACE1_ADDR,
      CONFIG_NET_IFACE1_GW,
      CONFIG_NET_IFACE1_NETMASK,
      CONFIG_NET_IFACE1_VLAN);
    if (ret) {
      log_error("Failed to configure network interface 1\n");
      return 1;
    }
    log_info("Network interface 1 configured\n");
  }

  if (ifs.size() >= 2 && sizeof(CONFIG_NET_IFACE2_ADDR) > 1) {
    log_info("Configuring network interface 2\n");
    int ret = setup_iface(
      ifs[1],
      CONFIG_NET_IFACE2_ADDR,
      CONFIG_NET_IFACE2_GW,
      CONFIG_NET_IFACE2_NETMASK,
      CONFIG_NET_IFACE2_VLAN);
    if (ret) {
      log_error("Failed to configure network interface 2\n");
      return 1;
    }
    log_info("Network interface 2 configured\n");
  }

#if CONFIG_NET_DHCPV4 && !CONFIG_NET_CONFIG_AUTO_INIT
  if(ifs.size() >= 1) {
    log_info("Bringing up interface %p\n", ifs[0]);
    net_if_up(ifs[0]);

    log_debug("Initializing DHCP event callback...\n");
    net_mgmt_init_event_callback(&mgmt_cb, handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&mgmt_cb);
    net_dhcpv4_start(ifs[0]);

    log_debug("Waiting for DHCP lease semaphore...\n");
    if (k_sem_take(&got_address, K_SECONDS(10)) != 0) {
      log_error("Did not get a DHCP lease within 10 seconds\n");
    } else {
      log_debug("DHCP lease semaphore acquired.\n");
    }
  }
#endif  // CONFIG_NET_DHCPV4

  return 0;
}
