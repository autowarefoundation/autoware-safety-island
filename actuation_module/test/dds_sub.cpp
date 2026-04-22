#include "common/node/node.hpp"
#include "common/clock/clock.hpp"
#include "common/logger/logger.hpp"
#include "autoware/autoware_msgs/messages.hpp"
using namespace common::logger;

#include "platform/platform_threading.h"

// Stack sizes for node and timer threads
static K_THREAD_STACK_DEFINE(node_stack, CONFIG_THREAD_STACK_SIZE);
#define STACK_SIZE (K_THREAD_STACK_SIZEOF(node_stack))

/*
    This test is used to test the DDS communication between ROS2 and Zephyr
    It is used to validate the message conversion between ROS2 and Zephyr
    It is used to validate the sequence wrapper    
*/
static void handle_steering_report(const SteeringReportMsg* msg, void* arg) {
    log_info("\n------ STEERING REPORT ------\n");
    log_info("Timestamp: %f\n", Clock::toDouble(msg->stamp));
    // log_info("Steering tire angle: %lf\n", msg->steering_tire_angle);
    log_info("-------------------------------\n");
}

static void handle_operation_mode_state(const OperationModeStateMsg* msg, void* arg) {
    log_info("\n------ OPERATION MODE STATE ------\n");
    log_info("Timestamp: %f\n", Clock::toDouble(msg->stamp));
    // log_info("Mode: %d\n", msg->mode);
    // log_info("Autoware control enabled: %d\n", msg->is_autoware_control_enabled);
    // log_info("In transition: %d\n", msg->is_in_transition);
    log_info("-------------------------------\n");
}

static void handle_odometry(const OdometryMsg* msg, void* arg) {
    log_info("\n------ ODOMETRY ------\n");
    log_info("Timestamp: %f\n", Clock::toDouble(msg->header.stamp));
    // log_info("Position: %lf, %lf, %lf\n", msg->pose.pose.position.x, msg->pose.pose.position.y, msg->pose.pose.position.z);
    // log_info("Linear Twist: %lf, %lf, %lf\n", msg->twist.twist.linear.x, msg->twist.twist.linear.y, msg->twist.twist.linear.z);
    log_info("-------------------------------\n");
}

static void handle_acceleration(const AccelerationMsg* msg, void* arg) {
    log_info("\n------ ACCELERATION ------\n");
    log_info("Timestamp: %f\n", Clock::toDouble(msg->header.stamp));
    // log_info("Linear acceleration: %lf, %lf, %lf\n", msg->accel.accel.linear.x, msg->accel.accel.linear.y, msg->accel.accel.linear.z);
    // log_info("Angular acceleration: %lf, %lf, %lf\n", msg->accel.accel.angular.x, msg->accel.accel.angular.y, msg->accel.accel.angular.z);
    log_info("-------------------------------\n");
}

static void handle_trajectory(const TrajectoryMsg_Raw* msg, void* arg) {
    static int count = 0;
    TrajectoryMsg trajectory_msg(msg);  // Convert the raw DDS sequence to a vector
    log_success("\n------ TRAJECTORY --IDX: %d ------\n", count++);
    log_success("Timestamp: %f\n", Clock::toDouble(trajectory_msg.header.stamp));
    log_success("Trajectory size: %d\n", trajectory_msg.points.size());
    log_success("-------------------------------\n");
}


static void callbackTimer() {
    log_info("Callback timer\n");
}

int main(void) {
    log_info("--------------------------------\n");
    log_info("Starting DDS subscriber\n");
    log_info("--------------------------------\n");
    log_info("Waiting for DHCP to get IP address...\n");
    sleep(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX);

#ifdef CONFIG_ENABLE_SNTP
    if (Clock::init_clock_via_sntp() < 0) {
        log_error("Failed to set time using SNTP\n");
    }
    else {
        log_info("Time set using SNTP\n");
    }
#endif
    
    // Create a node
    Node node("dds_test_sub", node_stack, STACK_SIZE);

    // Create test timer for 500ms
    node.create_timer(500, std::bind(&callbackTimer));

    // Create subscribers for all the topics the publisher expects
    node.create_subscription<SteeringReportMsg>("/vehicle/status/steering_status",
                                                                &autoware_vehicle_msgs_msg_SteeringReport_desc,
                                                                handle_steering_report, &node);
    node.create_subscription<TrajectoryMsg_Raw>("/planning/scenario_planning/trajectory",
                                                                &autoware_planning_msgs_msg_Trajectory_desc,
                                                                handle_trajectory, &node);
    node.create_subscription<OdometryMsg>("/localization/kinematic_state",
                                                                &nav_msgs_msg_Odometry_desc,
                                                                handle_odometry, &node);
    node.create_subscription<AccelerationMsg>("/localization/acceleration",
                                                                &geometry_msgs_msg_AccelWithCovarianceStamped_desc,
                                                                handle_acceleration, &node);
    node.create_subscription<OperationModeStateMsg>("/system/operation_mode/state",
                                                                &autoware_adapi_v1_msgs_msg_OperationModeState_desc,
                                                                handle_operation_mode_state, &node);

    log_info("--------------------------------\n");
    log_info("DDS subscriber started\n");
    log_info("--------------------------------\n");
    node.spin();

    while(true) {
        sleep(1);
    }

    return 0;
}