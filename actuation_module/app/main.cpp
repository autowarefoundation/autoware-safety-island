// Copyright (c) 2024-2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#include "common/clock/clock.hpp"
#include "common/logger/logger.hpp"
using namespace common::logger;

#include "autoware/trajectory_follower_node/controller_node.hpp"

int main(void)
{   
    autoware::motion::control::trajectory_follower_node::Controller* controller;
    
    log_success("-----------------------------------------");
    log_success("ARM - Autoware: Actuation Safety Island");
    log_success("-----------------------------------------");
    log_info("Waiting for DHCP to get IP address...");
    sleep(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX);

    // TODO: Disable SNTP if no internet connection is available
#ifdef CONFIG_ENABLE_SNTP
    log_info("Setting time using SNTP...\n");
    if (Clock::init_clock_via_sntp() < 0) {
        log_error("Failed to set time using SNTP\n");
        std::exit(1);
    }
#endif

    log_info("Starting Controller Node...");
    try
    {
        controller = new autoware::motion::control::trajectory_follower_node::Controller();
        int ret = controller->spin();
        if (ret != 0) {
            log_error("Failed to start Controller Node");
            std::exit(1);
        }
        log_success("Controller Node Started");
        log_success("-----------------------------------------");
    }
    catch(const std::exception& e)
    {
        log_error("Failed to start Controller Node: %s", e.what());
        std::exit(1);
    }

    log_success("Actuation Safety Island is Live");
    log_success("-----------------------------------------");

    controller->wait_for_completion();

    log_info("Actuation Safety Island is Shutting Down");
    log_success("-----------------------------------------");

    return 0;
}
