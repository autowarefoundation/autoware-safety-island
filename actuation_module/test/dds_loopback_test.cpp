#include <atomic>
#include <cstdlib>

#include "common/clock/clock.hpp"
#include "common/logger/logger.hpp"
#include "common/node/node.hpp"
#include "platform/platform_threading.h"

#include "SteeringReport.h"

using namespace common::logger;

using SteeringReportMsg = autoware_vehicle_msgs_msg_SteeringReport;

static K_THREAD_STACK_DEFINE(node_stack, CONFIG_THREAD_STACK_SIZE);
#define STACK_SIZE (K_THREAD_STACK_SIZEOF(node_stack))

static std::atomic<int> steering_report_count{0};

static void handle_steering_report(const SteeringReportMsg * msg, void *)
{
  log_info("\n------ STEERING REPORT ------\n");
  log_info("Timestamp: %f\n", Clock::toDouble(msg->stamp));
  log_info("Steering tire angle: %f\n", msg->steering_tire_angle);
  log_info("-------------------------------\n");
  steering_report_count.fetch_add(1, std::memory_order_relaxed);
}

int main(void)
{
  log_info("--------------------------------\n");
  log_info("Starting DDS loopback test\n");
  log_info("--------------------------------\n");
  log_info("Waiting for DHCP to get IP address...\n");
  sleep(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX);

#if defined(CONFIG_ENABLE_SNTP) && CONFIG_ENABLE_SNTP
  if (Clock::init_clock_via_sntp() < 0) {
    log_error("Failed to set time using SNTP\n");
  } else {
    log_info("Time set using SNTP\n");
  }
#endif

  Node node("dds_loopback_test", node_stack, STACK_SIZE);
  if (!node.create_subscription<SteeringReportMsg>(
      "/vehicle/status/steering_status",
      &autoware_vehicle_msgs_msg_SteeringReport_desc,
      handle_steering_report,
      nullptr)) {
    log_error("Failed to create DDS loopback subscriber\n");
    std::exit(1);
  }

  auto publisher = node.create_publisher<SteeringReportMsg>(
    "/vehicle/status/steering_status", &autoware_vehicle_msgs_msg_SteeringReport_desc);

  if (node.spin() != 0) {
    log_error("Failed to start DDS loopback node\n");
    std::exit(1);
  }

  for (int attempt = 0; attempt < 20 && steering_report_count.load(std::memory_order_relaxed) < 2;
       ++attempt) {
    SteeringReportMsg msg{};
    msg.stamp = Clock::toRosTime(Clock::now());
    msg.steering_tire_angle = 0.5;
    if (!publisher->publish(msg)) {
      log_error("Failed to publish DDS loopback steering report\n");
      return 1;
    }
    sleep(1);
  }

  if (steering_report_count.load(std::memory_order_relaxed) < 2) {
    log_error("DDS loopback subscriber did not receive enough steering reports\n");
    return 1;
  }

  log_info("DDS loopback test passed\n");
  return 0;
}
