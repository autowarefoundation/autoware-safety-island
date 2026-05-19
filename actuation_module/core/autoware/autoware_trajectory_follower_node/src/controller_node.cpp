// Copyright 2021 Tier IV, Inc. All rights reserved.
// Edited by: Oguz Ozturk 2025, ARM
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "autoware/trajectory_follower_node/controller_node.hpp"
#include "autoware/mpc_lateral_controller/mpc_lateral_controller.hpp"
#include "autoware/pid_longitudinal_controller/pid_longitudinal_controller.hpp"
#include <autoware/trajectory_follower_base/lateral_controller_base.hpp>

#include "common/logger/logger.hpp"
#include "common/clock/clock.hpp"
using namespace common::logger;

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// static K_THREAD_STACK_DEFINE(node_stack, CONFIG_THREAD_STACK_SIZE)  __aligned(4);  // TODO: may be needed for possible eigen memory issues
static K_THREAD_STACK_DEFINE(node_stack, CONFIG_THREAD_STACK_SIZE);
#define STACK_SIZE (K_THREAD_STACK_SIZEOF(node_stack))

namespace autoware::motion::control::trajectory_follower_node
{
Controller::Controller() : Node("controller", node_stack, STACK_SIZE)
{
  using std::placeholders::_1;

  const double ctrl_period = declare_parameter<double>("ctrl_period", 0.15);  // TODO: Orignal autoware period is 0.03 30ms
  timeout_thr_sec_ = declare_parameter<double>("timeout_thr_sec", 0.5);

  const auto lateral_controller_mode =
    getLateralControllerMode(declare_parameter<std::string>("lateral_controller_mode", "mpc"));
  log_debug("Lateral controller mode: %d", lateral_controller_mode);
  switch (lateral_controller_mode) {
    case LateralControllerMode::MPC: {
      lateral_controller_ =
        std::make_shared<mpc_lateral_controller::MpcLateralController>(*this);
      break;
    }
    default:
      log_error("[LateralController] invalid algorithm");
      std::exit(1);
  }

  const auto longitudinal_controller_mode =
    getLongitudinalControllerMode(declare_parameter<std::string>("longitudinal_controller_mode", "pid"));
  log_debug("Longitudinal controller mode: %d", longitudinal_controller_mode);
  switch (longitudinal_controller_mode) {
    case LongitudinalControllerMode::PID: {
      longitudinal_controller_ =
        std::make_shared<pid_longitudinal_controller::PidLongitudinalController>(*this);
      break;
    }
    default:
      log_error("[LongitudinalController] invalid algorithm");
      std::exit(1);
  }

  // Timer
  {
    const auto period_ms = ctrl_period*1000;
    create_timer(period_ms, [this]() { callbackTimerControl(); });
  }

  // Subscribers
  auto subscriber_steering_status = create_subscription<SteeringReportMsg>("/vehicle/status/steering_status",
                                                              &autoware_vehicle_msgs_msg_SteeringReport_desc,
                                                              callbackSteeringStatus, this);
  auto subscriber_trajectory = create_subscription<TrajectoryMsg_Raw>("/planning/scenario_planning/trajectory",
                                                              &autoware_planning_msgs_msg_Trajectory_desc,
                                                              callbackTrajectory, this);
  auto subscriber_odometry = create_subscription<OdometryMsg>("/localization/kinematic_state",
                                                              &nav_msgs_msg_Odometry_desc,
                                                              callbackOdometry, this);
  auto subscriber_acceleration = create_subscription<AccelWithCovarianceStampedMsg>("/localization/acceleration",
                                                              &geometry_msgs_msg_AccelWithCovarianceStamped_desc,
                                                              callbackAcceleration, this);
  auto subscriber_operation_mode_state = create_subscription<OperationModeStateMsg>("/system/operation_mode/state",
                                                              &autoware_adapi_v1_msgs_msg_OperationModeState_desc,
                                                              callbackOperationModeState, this);
    
  // Publishers
  control_cmd_pub_ = create_publisher<ControlMsg>(
    "/control/trajectory_follower/control_cmd", &autoware_control_msgs_msg_Control_desc);
  pub_processing_time_lat_ms_ =
    create_publisher<Float64StampedMsg>("/control/trajectory_follower/lateral/debug/processing_time_ms", &tier4_debug_msgs_msg_Float64Stamped_desc);
  pub_processing_time_lon_ms_ =
    create_publisher<Float64StampedMsg>("/control/trajectory_follower/longitudinal/debug/processing_time_ms", &tier4_debug_msgs_msg_Float64Stamped_desc);
}

// SUBSCRIBER CALLBACKS
void Controller::callbackSteeringStatus(const SteeringReportMsg* msg, void* arg) {
  // static int count = 0;
  // log_debug("-------STEERING STATUS----IDX %d----", count++);
  // log_debug("Timestamp: %ld", Clock::toDouble(msg->stamp));
  // log_debug("Received steering status: %f", msg->steering_tire_angle);
  // log_debug("--------------------------------");

  // Put data into state pointers
  Controller* controller = static_cast<Controller*>(arg);
  controller->current_steering_ = *msg;
  controller->has_steering_ = true;
}

void Controller::callbackOperationModeState(const OperationModeStateMsg* msg, void* arg) {
  // static int count = 0;
  // log_debug("-------OPERATION MODE STATE----IDX %d----", count++);
  // log_debug("Timestamp: %ld", Clock::toDouble(msg->stamp));
  // log_debug("Mode: %d", msg->mode);
  // log_debug("Autoware control enabled: %d", msg->is_autoware_control_enabled);
  // log_debug("In transition: %d", msg->is_in_transition);
  // log_debug("--------------------------------");

  // Put data into state pointers
  Controller* controller = static_cast<Controller*>(arg);
  controller->current_operation_mode_ = *msg;
  controller->has_operation_mode_ = true;
}

void Controller::callbackOdometry(const OdometryMsg* msg, void* arg) {
  // static int count = 0;
  // log_debug("-------ODOMETRY----IDX %d----", count++);
  // log_debug("Timestamp: %ld", Clock::toDouble(msg->stamp));
  // log_debug("Position: %lf, %lf, %lf", msg->pose.pose.position.x, msg->pose.pose.position.y, msg->pose.pose.position.z);
  // log_debug("Linear Twist: %lf, %lf, %lf", msg->twist.twist.linear.x, msg->twist.twist.linear.y, msg->twist.twist.linear.z);
  // log_debug("-------------------------------");

  // Put data into state pointers
  Controller* controller = static_cast<Controller*>(arg);
  controller->current_odometry_ = *msg;
  controller->has_odometry_ = true;
}

void Controller::callbackAcceleration(const AccelWithCovarianceStampedMsg* msg, void* arg) {
  // static int count = 0;
  // log_debug("-------ACCELERATION----IDX %d----", count++);
  // log_debug("Timestamp: %ld", Clock::toDouble(msg->stamp));
  // log_debug("Linear acceleration: %lf, %lf, %lf", msg->accel.accel.linear.x, msg->accel.accel.linear.y, msg->accel.accel.linear.z);
  // log_debug("Angular acceleration: %lf, %lf, %lf", msg->accel.accel.angular.x, msg->accel.accel.angular.y, msg->accel.accel.angular.z);
  // log_debug("-------------------------------");

  // Put data into state pointers
  Controller* controller = static_cast<Controller*>(arg);
  controller->current_accel_ = *msg;
  controller->has_accel_ = true;
}

void Controller::callbackTrajectory(const TrajectoryMsg_Raw* msg, void* arg) {
  // static int count = 0;
  // log_debug("-------TRAJECTORY----IDX %d----", count++);
  // log_debug("Timestamp: %f", Clock::toDouble(msg->header.stamp));
  // log_debug("Trajectory size: %u", msg->points._length);
  // log_debug("-------------------------------");

  // Copy the data instead of storing the pointer
  Controller* controller = static_cast<Controller*>(arg);
  controller->current_trajectory_ = TrajectoryMsg(msg);  // Copy the entire message
  controller->has_trajectory_ = true;
}

Controller::LateralControllerMode Controller::getLateralControllerMode(
  const std::string & controller_mode) const
{
  if (controller_mode == "mpc") return LateralControllerMode::MPC;

  return LateralControllerMode::INVALID;
}

Controller::LongitudinalControllerMode Controller::getLongitudinalControllerMode(
  const std::string & controller_mode) const
{
  if (controller_mode == "pid") return LongitudinalControllerMode::PID;

  return LongitudinalControllerMode::INVALID;
}

bool Controller::processData()
{
  bool is_ready = true;

  const auto & logData = [this](const std::string & data_type) {
    log_info_throttle(("Waiting for " + data_type + " data").c_str());
  };

  if (!has_accel_) {
    logData("acceleration");
    is_ready = false;
  }
  if (!has_steering_) {
    logData("steering");
    is_ready = false;
  }
  if (!has_trajectory_) {
    logData("trajectory");
    is_ready = false;
  }
  if (!has_odometry_) {
    logData("odometry");
    is_ready = false;
  }
  if (!has_operation_mode_) {
    logData("operation mode");
    is_ready = false;
  }

  return is_ready;
}

bool Controller::isTimeOut(
  const trajectory_follower::LongitudinalOutput & lon_out,
  const trajectory_follower::LateralOutput & lat_out)
{
  const auto now = Clock::now();
  if ((now - Clock::toDouble(lat_out.control_cmd.stamp)) > timeout_thr_sec_) {
    log_warn_throttle("Lateral control command too old, control_cmd will not be published.");
    return true;
  }
  if ((now - Clock::toDouble(lon_out.control_cmd.stamp)) > timeout_thr_sec_) {
    log_warn_throttle("Longitudinal control command too old, control_cmd will not be published.");
    return true;
  }
  return false;
}

std::optional<trajectory_follower::InputData> Controller::createInputData()
{
  if (!processData()) {
    return {};
  }

  trajectory_follower::InputData input_data;
  input_data.current_trajectory = current_trajectory_;
  input_data.current_odometry = current_odometry_;
  input_data.current_steering = current_steering_;
  input_data.current_accel = current_accel_;
  input_data.current_operation_mode = current_operation_mode_;

  return input_data;
}

void Controller::callbackTimerControl()
{
  // log_debug("Timer control callback");

  // 1. create input data
  const auto input_data = createInputData();
  if (!input_data) {
    log_info_throttle("Control is skipped since input data is not ready.");
    return;
  }

  log_debug("Input data created");

  // 2. check if controllers are ready
  const bool is_lat_ready = lateral_controller_->isReady(*input_data);
  const bool is_lon_ready = longitudinal_controller_->isReady(*input_data);
  if (!is_lat_ready || !is_lon_ready) {
    log_info_throttle("Control is skipped since lateral and/or longitudinal controllers are not ready to run.");
    return;
  }

  log_debug("Controllers are ready");

  // 3. run controllers
  stop_watch_.tic("lateral");
  const auto lat_out = lateral_controller_->run(*input_data);
  stop_watch_.toc("lateral");
  log_debug("Lateral controller elapsed time: %f", stop_watch_.toc("lateral"));

  log_debug("-------LAT OUT--", 0);
  log_debug("Lateral output: %f", lat_out.control_cmd.steering_tire_angle);
  log_debug("Lateral steering tire rotation rate: %f", lat_out.control_cmd.steering_tire_rotation_rate);
  log_debug("Lateral is defined steering tire rotation rate: %s", lat_out.control_cmd.is_defined_steering_tire_rotation_rate ? "true" : "false");

  publishProcessingTime(stop_watch_.toc("lateral"), pub_processing_time_lat_ms_);

  stop_watch_.tic("longitudinal");
  const auto lon_out = longitudinal_controller_->run(*input_data);
  stop_watch_.toc("longitudinal");
  log_debug("Longitudinal controller elapsed time: %f", stop_watch_.toc("longitudinal"));

  // TODO: do not calculate jerk here, it is not used !
  log_debug("-------LON OUT--", 0);
  log_debug("Longitudinal output: %f", lon_out.control_cmd.velocity);
  log_debug("Longitudinal acceleration: %f", lon_out.control_cmd.acceleration);
  log_debug("Longitudinal is defined acceleration: %s", lon_out.control_cmd.is_defined_acceleration ? "true" : "false");
  log_debug("Longitudinal is defined jerk: %s", lon_out.control_cmd.is_defined_jerk ? "true" : "false");
  log_debug("-------------------------------");

  publishProcessingTime(stop_watch_.toc("longitudinal"), pub_processing_time_lon_ms_);

  log_debug("Controllers ran");

  // 4. sync with each other controllers
  longitudinal_controller_->sync(lat_out.sync_data);
  lateral_controller_->sync(lon_out.sync_data);

  log_debug("Controllers synced");

  // TODO(Horibe): Think specification. This comes from the old implementation.
  // if (isTimeOut(lon_out, lat_out)) return;

  // 5. publish control command
  publishControlCommand(lon_out, lat_out);

  // 6. Reset flags for next cycle
  // TODO: Check if this is required, autoware version keeps publishing even there is no new data
  // reset_data_flags();
}

void Controller::publishControlCommand(const trajectory_follower::LongitudinalOutput & lon_out, const trajectory_follower::LateralOutput & lat_out) {
  ControlMsg out{0};
  out.stamp = Clock::toRosTime(Clock::now());
  out.lateral.steering_tire_angle = lat_out.control_cmd.steering_tire_angle;
  out.lateral.steering_tire_rotation_rate = lat_out.control_cmd.steering_tire_rotation_rate;
  out.lateral.stamp = out.stamp;
  out.longitudinal = lon_out.control_cmd;
  if (control_cmd_pub_->publish(out)) {
    log_debug("Control command published");
  } else {
    log_error("Control command not published");
  }
}

void Controller::publishProcessingTime(
  const double t_ms, const std::shared_ptr<Publisher<Float64StampedMsg>> pub)
{
  Float64StampedMsg msg{};
  msg.stamp = Clock::toRosTime(Clock::now());
  msg.data = t_ms;
  pub->publish(msg);
}
}  // namespace autoware::motion::control::trajectory_follower_node
