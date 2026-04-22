#include "common/node/node.hpp"
#include "common/clock/clock.hpp"
#include "common/logger/logger.hpp"
using namespace common::logger;

#include "platform/platform_threading.h"

// Msgs
#include "SteeringReport.h"
#include "Trajectory.h"
#include "Odometry.h"
#include "AccelWithCovarianceStamped.h"
#include "OperationModeState.h"
using SteeringReportMsg = autoware_vehicle_msgs_msg_SteeringReport;
using TrajectoryMsg = autoware_planning_msgs_msg_Trajectory;
using OdometryMsg = nav_msgs_msg_Odometry;
using AccelerationMsg = geometry_msgs_msg_AccelWithCovarianceStamped;
using OperationModeStateMsg = autoware_adapi_v1_msgs_msg_OperationModeState;

static K_THREAD_STACK_DEFINE(node_stack, CONFIG_THREAD_STACK_SIZE);
#define STACK_SIZE (K_THREAD_STACK_SIZEOF(node_stack))

#define PUBLISH_PERIOD_MS (2000)

/*
    This test is used to test the DDS communication between ROS2 and Zephyr
    It is used to validate the message conversion between ROS2 and Zephyr
*/
int main(void) {
    log_info("--------------------------------\n");
    log_info("Starting DDS publisher\n");
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

    // Create a node and publishers for all the topics the subscriber expects
    Node node("dds_test_pub", node_stack, STACK_SIZE);
    
    // Create publishers for all message types
    auto steering_publisher = node.create_publisher<SteeringReportMsg>("/vehicle/status/steering_status", &autoware_vehicle_msgs_msg_SteeringReport_desc);
    // auto trajectory_publisher = node.create_publisher<TrajectoryMsg>("/planning/scenario_planning/trajectory", &autoware_planning_msgs_msg_Trajectory_desc);
    auto odometry_publisher = node.create_publisher<OdometryMsg>("/localization/kinematic_state", &nav_msgs_msg_Odometry_desc);
    auto acceleration_publisher = node.create_publisher<AccelerationMsg>("/localization/acceleration", &geometry_msgs_msg_AccelWithCovarianceStamped_desc);
    auto operation_mode_publisher = node.create_publisher<OperationModeStateMsg>("/system/operation_mode/state", &autoware_adapi_v1_msgs_msg_OperationModeState_desc);

    log_info("--------------------------------\n");
    log_info("DDS publisher started\n");
    log_info("--------------------------------\n");

    while(true) {
        auto current_time = Clock::toRosTime(Clock::now());
        
        // Publish SteeringReport message
        SteeringReportMsg steering_msg;
        steering_msg.stamp = current_time;
        steering_msg.steering_tire_angle = 0.5; // radians
        steering_publisher->publish(steering_msg);
        log_info("Published steering report: angle=%.2f\n", steering_msg.steering_tire_angle);

        // Publish Trajectory message
        // TrajectoryMsg trajectory_msg;
        // trajectory_msg.header.stamp = current_time;
        // trajectory_msg.header.frame_id = "map";
        // // Initialize trajectory points (simplified - just 3 points)
        // trajectory_msg.points._length = 3;
        // trajectory_msg.points._maximum = 3;
        // trajectory_msg.points._buffer = new autoware_planning_msgs_msg_TrajectoryPoint[3];
        // for (int i = 0; i < 3; i++) {
        //     trajectory_msg.points._buffer[i].pose.position.x = i * 10.0;
        //     trajectory_msg.points._buffer[i].pose.position.y = i * 5.0;
        //     trajectory_msg.points._buffer[i].pose.position.z = 0.0;
        //     trajectory_msg.points._buffer[i].longitudinal_velocity_mps = 10.0;
        //     trajectory_msg.points._buffer[i].lateral_velocity_mps = 0.0;
        //     trajectory_msg.points._buffer[i].acceleration_mps2 = 1.0;
        // }
        // trajectory_publisher->publish(trajectory_msg);
        // log_info("Published trajectory with %d points\n", trajectory_msg.points._length);
        // delete[] trajectory_msg.points._buffer;

        // Publish Odometry message
        OdometryMsg odometry_msg;
        odometry_msg.header.stamp = current_time;
        odometry_msg.header.frame_id = "odom";
        odometry_msg.pose.pose.position.x = 10.0;
        odometry_msg.pose.pose.position.y = 20.0;
        odometry_msg.pose.pose.position.z = 0.0;
        odometry_msg.twist.twist.linear.x = 5.0;
        odometry_msg.twist.twist.linear.y = 0.0;
        odometry_msg.twist.twist.linear.z = 0.0;
        odometry_publisher->publish(odometry_msg);
        log_info("Published odometry: pos=(%.1f, %.1f, %.1f), vel=(%.1f, %.1f, %.1f)\n", 
                 odometry_msg.pose.pose.position.x, odometry_msg.pose.pose.position.y, odometry_msg.pose.pose.position.z,
                 odometry_msg.twist.twist.linear.x, odometry_msg.twist.twist.linear.y, odometry_msg.twist.twist.linear.z);

        // Publish Acceleration message
        AccelerationMsg acceleration_msg;
        acceleration_msg.header.stamp = current_time;
        acceleration_msg.header.frame_id = "base_link";
        acceleration_msg.accel.accel.linear.x = 2.0;
        acceleration_msg.accel.accel.linear.y = 0.0;
        acceleration_msg.accel.accel.linear.z = 0.0;
        acceleration_msg.accel.accel.angular.x = 0.0;
        acceleration_msg.accel.accel.angular.y = 0.0;
        acceleration_msg.accel.accel.angular.z = 0.1;
        acceleration_publisher->publish(acceleration_msg);
        log_info("Published acceleration: linear=(%.1f, %.1f, %.1f), angular=(%.1f, %.1f, %.1f)\n",
                 acceleration_msg.accel.accel.linear.x, acceleration_msg.accel.accel.linear.y, acceleration_msg.accel.accel.linear.z,
                 acceleration_msg.accel.accel.angular.x, acceleration_msg.accel.accel.angular.y, acceleration_msg.accel.accel.angular.z);

        // Publish OperationModeState message
        OperationModeStateMsg operation_mode_msg;
        operation_mode_msg.stamp = current_time;
        operation_mode_msg.mode = 1; // Some mode value
        operation_mode_msg.is_autoware_control_enabled = true;
        operation_mode_msg.is_in_transition = false;
        operation_mode_publisher->publish(operation_mode_msg);
        log_info("Published operation mode: mode=%d, autoware_enabled=%d, in_transition=%d\n",
                 operation_mode_msg.mode, operation_mode_msg.is_autoware_control_enabled, operation_mode_msg.is_in_transition);

        log_info("--------------------------------\n");
        sleep(PUBLISH_PERIOD_MS / 1000);
    }

    return 0;
}