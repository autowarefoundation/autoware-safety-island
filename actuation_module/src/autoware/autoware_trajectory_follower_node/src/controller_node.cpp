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

#include "platform/platform_threading.h"

#if defined(PLATFORM_FREERTOS_X5H)
#include "si_channel.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
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
Controller::Controller(const StartupConfig & config)
: Node("controller", node_stack, STACK_SIZE), startup_config_(config)
{
  using std::placeholders::_1;

  const double ctrl_period = declare_parameter<double>("ctrl_period", 0.15);  // TODO: Orignal autoware period is 0.03 30ms
  timeout_thr_sec_ = declare_parameter<double>("timeout_thr_sec", 0.5);

  // -------------------------------------------------------------------
  // Supervision configuration (vp_si_control_contract): mode, per-source
  // arrival-age timeouts, actuation sanity bounds and the SI_STOP demand.
  // Defaults are the measured proposal in SI #62 (see controller_node.hpp).
  supervision_mode_ = config.mode == StartupConfig::Mode::VP_CONTROL ?
    supervision::Mode::VP_CONTROL : supervision::Mode::SI_CONTROL;
  source_timeout_candidate_ = declare_parameter<double>(
    "source_timeout_candidate_s", source_timeout_candidate_);
  source_timeout_ego_ = declare_parameter<double>(
    "source_timeout_ego_s", source_timeout_ego_);
  source_timeout_steering_ = declare_parameter<double>(
    "source_timeout_steering_s", source_timeout_steering_);
  source_timeout_opmode_ = declare_parameter<double>(
    "source_timeout_opmode_s", source_timeout_opmode_);
  source_timeout_heartbeat_ = declare_parameter<double>(
    "source_timeout_heartbeat_s", source_timeout_heartbeat_);
  max_steering_rad_ = declare_parameter<double>(
    "max_steering_rad", max_steering_rad_);
  max_abs_accel_mps2_ = declare_parameter<double>(
    "max_abs_accel_mps2", max_abs_accel_mps2_);
  max_abs_velocity_mps_ = declare_parameter<double>(
    "max_abs_velocity_mps", max_abs_velocity_mps_);
  stop_decel_mps2_ = static_cast<float>(declare_parameter<double>(
    "stop_decel_mps2", stop_decel_mps2_));
  reenable_window_s_ = declare_parameter<double>("reenable_window_s", reenable_window_s_);
  // Same node parameters the MPC reads (declare_parameter returns an existing
  // value), so this is the steering limit the follower saturates at.
  follower_max_steer_rad_ =
    autoware::vehicle_info_utils::VehicleInfoUtils(*this).getVehicleInfo().max_steer_angle_rad;
  char opmode_limit[24];
  if (std::isinf(source_timeout_opmode_)) {
    std::snprintf(opmode_limit, sizeof(opmode_limit), "no limit");
  } else {
    std::snprintf(opmode_limit, sizeof(opmode_limit), "%.2fs", source_timeout_opmode_);
  }
  log_info(
    "Supervision mode: %s; trajectory source: %s (candidate %.2fs ego %.2fs steering %.2fs opmode %s heartbeat %.2fs; stop %.2f m/s^2)",
    supervision_mode_ == supervision::Mode::VP_CONTROL ? "vp" : "si",
    config.trajectory_source == StartupConfig::TrajectorySource::VP ? "vp" : "autoware",
    source_timeout_candidate_, source_timeout_ego_,
    source_timeout_steering_, opmode_limit, source_timeout_heartbeat_,
    static_cast<double>(stop_decel_mps2_));
  log_info(
    "Legacy control_cmd policy: %s",
    config.legacy_control_cmd == StartupConfig::LegacyControlCmd::STOP_ONLY ?
    "stop_only" : "always");
  log_info(
    "Operation-mode reader durability: %s",
    config.operation_mode_durability == StartupConfig::OpModeDurability::VOLATILE ?
    "volatile" : "transient_local");

  // Output identity: a nonzero SI session for this process boot, written
  // into every ApprovedRequest so the observer can tell SI restarts apart.
  // The helper keeps the conversion defined: the epoch-millisecond value is
  // out of uint32_t range, and the ARM targets' saturating conversion would
  // otherwise collapse every boot to the same constant.
  si_session_ = supervision::siSessionFromClock(Clock::now());

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
  if (supervision_mode_ == supervision::Mode::SI_CONTROL) {
    if (startup_config_.trajectory_source == StartupConfig::TrajectorySource::VP) {
      // VP's own path + speed schedule; the SI places it with the same-frame
      // ego pose (vp_reference.hpp), so no adapter sits between VP and SI.
      if (!create_subscription<DrivingReferenceMsg>(
        "/vehicle/driving_reference",
        &visionpilot_msgs_msg_DrivingReference_desc,
        callbackDrivingReference, this))
      {
        throw std::runtime_error("cannot subscribe to selected VP driving reference");
      }
    } else if (!create_subscription<TrajectoryMsg_Raw>(
      "/planning/scenario_planning/trajectory",
      &autoware_planning_msgs_msg_Trajectory_desc, callbackTrajectory, this))
    {
      throw std::runtime_error("cannot subscribe to selected Autoware trajectory");
    }
  }
  auto subscriber_odometry = create_subscription<OdometryMsg>("/localization/kinematic_state",
                                                              &nav_msgs_msg_Odometry_desc,
                                                              callbackOdometry, this);
  auto subscriber_acceleration = create_subscription<AccelWithCovarianceStampedMsg>("/localization/acceleration",
                                                              &geometry_msgs_msg_AccelWithCovarianceStamped_desc,
                                                              callbackAcceleration, this);
  // Durability must match the deployment's bridge. The operation mode is
  // published only when it changes: the classic demo bridge retains it with
  // TRANSIENT_LOCAL so a late-joining SI still receives it (the reader
  // default), while the E2E rig's bridge publishes VOLATILE, where a
  // TRANSIENT_LOCAL request does not match at all — the bridge logs
  // "requesting incompatible QoS ... DURABILITY_QOS_POLICY" and never
  // delivers a state, so the follower never becomes ready (observed on the
  // E2E rig 2026-09-25, SI_OPERATION_MODE_DURABILITY=volatile).
  const auto op_mode_durability =
    config.operation_mode_durability == StartupConfig::OpModeDurability::VOLATILE ?
    DDS_DURABILITY_VOLATILE : DDS_DURABILITY_TRANSIENT_LOCAL;
  auto subscriber_operation_mode_state = create_subscription<OperationModeStateMsg>("/system/operation_mode/state",
                                                              &autoware_adapi_v1_msgs_msg_OperationModeState_desc,
                                                              callbackOperationModeState, this,
                                                              op_mode_durability);
    
  output_mode_ = common::can::configured_control_command_output_mode();
  log_info("Control command output mode: %s", common::can::output_mode_name(output_mode_));

  // Supervision I/O: the VP command candidate (used in VP_CONTROL and for
  // cross-checking in either mode), the explicit operator re-enable topic,
  // and the single actuator-facing supervised output (ApprovedRequest carrying
  // session/sequence/decision/source/cycle identity alongside the control
  // payload).
  if (supervision_mode_ == supervision::Mode::VP_CONTROL &&
    !create_subscription<DrivingCommandMsg>(
      "/vehicle/driving_command", &visionpilot_msgs_msg_DrivingCommand_desc,
      callbackDrivingCommand, this))
  {
    throw std::runtime_error("cannot subscribe to selected VP driving command");
  }
  create_subscription<BoolMsg>(
    "/control/safety_island/reenable", &std_msgs_msg_Bool_desc,
    callbackReenable, this);
  create_subscription<Float64StampedMsg>(
    "/safety_island/vp_heartbeat", &tier4_debug_msgs_msg_Float64Stamped_desc,
    callbackHeartbeat, this);
  approved_request_pub_ = create_publisher<ApprovedRequestMsg>(
    "/control/safety_island/approved_request", &safety_island_msgs_msg_ApprovedRequest_desc);

  // Publishers
  if (common::can::output_mode_uses_dds(output_mode_)) {
    control_cmd_pub_ = create_publisher<ControlMsg>(
      "/control/trajectory_follower/control_cmd", &autoware_control_msgs_msg_Control_desc);
  }

  if (common::can::output_mode_uses_can(output_mode_)) {
    can_output_ = std::make_shared<common::can::ControlCommandCanOutput>();
    if (!can_output_->init()) {
      if (output_mode_ == common::can::ControlCommandOutputMode::CAN_ONLY) {
        log_error("CAN output initialization failed in CAN_ONLY mode");
        std::exit(1);
      }
      log_error("CAN output initialization failed; DDS output remains active in DDS_AND_CAN mode");
      can_output_.reset();
    }
  }

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
  controller->watch_steering_.note(Clock::now());
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
  controller->watch_opmode_.note(Clock::now());
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
  // *msg is a shallow copy of a CycloneDDS loaned sample: its char* members
  // (header.frame_id, child_frame_id) point into storage the subscriber returns
  // via dds_return_loan() the moment this callback returns. The controller only
  // consumes the numeric pose/twist fields, so drop the unused string pointers
  // instead of retaining them as dangling references into freed loan storage.
  controller->current_odometry_.header.frame_id = nullptr;
  controller->current_odometry_.child_frame_id = nullptr;
  controller->has_odometry_ = true;
  controller->watch_odom_.note(Clock::now());
  if (controller->supervision_mode_ == supervision::Mode::SI_CONTROL &&
    controller->startup_config_.trajectory_source == StartupConfig::TrajectorySource::VP)
  {
    const auto & q = msg->pose.pose.orientation;
    controller->ego_history_.note(
      msg->header.stamp.sec, msg->header.stamp.nanosec,
      {msg->pose.pose.position.x, msg->pose.pose.position.y,
        vp_reference::yawFromQuaternion(q.x, q.y, q.z, q.w)});
  }
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
  // Drop the loaned char* string (header.frame_id) before dds_return_loan() runs
  // — see callbackOdometry; only the numeric accel fields are consumed.
  controller->current_accel_.header.frame_id = nullptr;
  controller->has_accel_ = true;
  controller->watch_accel_.note(Clock::now());
}

void Controller::callbackTrajectory(const TrajectoryMsg_Raw* msg, void* arg) {
  Controller* controller = static_cast<Controller*>(arg);
  if (controller->supervision_mode_ != supervision::Mode::SI_CONTROL ||
    controller->startup_config_.trajectory_source != StartupConfig::TrajectorySource::AUTOWARE)
  {
    return;
  }
  // Ingress must accept only what the follower can run: the MPC's isReady()
  // requires at least vp_reference::kMinFollowerPoints points, and its
  // isValidTrajectory() additionally consumes the orientation quaternion,
  // lateral velocity, heading rate and wheel angles. A trajectory that passed
  // SI but failed the follower would keep the source watchdog fresh while
  // every tick published HOLD instead of the required fault response.
  if (!msg || msg->points._length < vp_reference::kMinFollowerPoints ||
    msg->points._length > 250 || !msg->points._buffer)
  {
    controller->latchFault("invalid autoware trajectory");
    return;
  }
  for (uint32_t i = 0; i < msg->points._length; ++i) {
    const auto & point = msg->points._buffer[i];
    if (!std::isfinite(point.pose.position.x) || !std::isfinite(point.pose.position.y) ||
      !std::isfinite(point.pose.position.z) ||
      !std::isfinite(point.pose.orientation.x) || !std::isfinite(point.pose.orientation.y) ||
      !std::isfinite(point.pose.orientation.z) || !std::isfinite(point.pose.orientation.w) ||
      !std::isfinite(point.longitudinal_velocity_mps) ||
      !std::isfinite(point.lateral_velocity_mps) ||
      !std::isfinite(point.acceleration_mps2) ||
      !std::isfinite(point.heading_rate_rps) ||
      !std::isfinite(point.front_wheel_angle_rad) ||
      !std::isfinite(point.rear_wheel_angle_rad))
    {
      controller->latchFault("non-finite autoware trajectory");
      return;
    }
  }
  const SourceStamp stamp{msg->header.stamp.sec, msg->header.stamp.nanosec};
  const auto check = controller->autoware_trajectory_identity_.check(stamp);
  if (check == CandidateCheck::DUPLICATE) {
    return;  // a republished old plan is not fresh
  }
  if (check != CandidateCheck::ACCEPT) {
    controller->latchFault("autoware trajectory stamp regression or invalid stamp");
    return;
  }

  // Copy the loaned DDS sample; the follower only consumes the points and stamp.
  controller->current_trajectory_ = TrajectoryMsg(msg);  // Copy the entire message
  // TrajectoryMsg deep-copies points but its `header = msg->header` shallow-copies
  // the loaned char* frame_id, which dds_return_loan() frees once this callback
  // returns — see callbackOdometry. Only the points/header.stamp are consumed, so
  // drop the dangling string pointer rather than retaining it (publishPredictedTraj()
  // is currently disabled, but this keeps the field safe for any future reader).
  controller->current_trajectory_.header.frame_id = nullptr;
  controller->has_trajectory_ = true;
  controller->autoware_trajectory_identity_.note(stamp);
  controller->watch_candidate_.note(Clock::now());
}

void Controller::callbackDrivingReference(const DrivingReferenceMsg* msg, void* arg)
{
  Controller* controller = static_cast<Controller*>(arg);
  if (controller->supervision_mode_ != supervision::Mode::SI_CONTROL ||
    controller->startup_config_.trajectory_source != StartupConfig::TrajectorySource::VP)
  {
    return;
  }
  // An unusable reference is ignored, never turned into a stop: only a fresh
  // accepted reference refreshes the source watchdog, which owns the stop.
  auto ignore = [controller](const char * why) {
      ++controller->vp_reference_rejects_;
      log_warn_throttle(
        "VP driving reference ignored: %s (%llu ignored so far)", why,
        (unsigned long long)controller->vp_reference_rejects_);
    };
  if (!msg || msg->speed_horizon_mps._length > vp_reference::kMaxHorizon ||
    (msg->speed_horizon_mps._length > 0 && !msg->speed_horizon_mps._buffer))
  {
    controller->latchFault("invalid vp driving reference");
    return;
  }
  const uint32_t horizon_len = msg->speed_horizon_mps._length;
  const auto reason = vp_reference::referenceReason(
    msg->valid, msg->path_valid, msg->has_source_stamp, horizon_len,
    msg->horizon_dt_s, msg->path_x_max_m);
  if (reason != vp_reference::Reject::NONE) {
    ignore(vp_reference::rejectName(reason));
    return;
  }
  bool finite = std::isfinite(msg->path_a) && std::isfinite(msg->path_b) &&
    std::isfinite(msg->path_c) && std::isfinite(msg->path_x_max_m) &&
    std::isfinite(msg->horizon_dt_s);
  for (uint32_t i = 0; finite && i < horizon_len; ++i) {
    finite = std::isfinite(msg->speed_horizon_mps._buffer[i]);
  }
  if (!finite) {
    controller->latchFault("non-finite vp driving reference");
    return;
  }
  const SourceStamp stamp{msg->source_stamp.sec, msg->source_stamp.nanosec};
  const auto check = controller->vp_candidate_identity_.check(msg->session, msg->cycle, stamp);
  if (check == CandidateCheck::DUPLICATE) {
    return;  // same VP cycle or camera frame: do not refresh the watchdog
  }
  if (check != CandidateCheck::ACCEPT) {
    controller->latchFault("vp driving reference regression or invalid stamp");
    return;
  }
  vp_reference::Pose2D ego{};
  if (!controller->ego_history_.find(stamp.sec, stamp.nanosec, ego)) {
    ignore("no-same-frame-ego");
    return;
  }
  const std::vector<double> horizon(
    msg->speed_horizon_mps._buffer, msg->speed_horizon_mps._buffer + horizon_len);
  std::vector<vp_reference::Point> points;
  if (!vp_reference::convert(
      msg->path_a, msg->path_b, msg->path_c, msg->path_x_max_m, ego, horizon,
      msg->horizon_dt_s, points))
  {
    ignore("bad-shape");
    return;
  }

  TrajectoryMsg trajectory;
  trajectory.header.stamp = msg->source_stamp;
  trajectory.header.frame_id = nullptr;  // the follower reads points and stamp only
  for (const auto & p : points) {
    TrajectoryPointMsg point{};
    point.pose.position.x = p.x;
    point.pose.position.y = p.y;
    point.pose.orientation.z = std::sin(0.5 * p.yaw);
    point.pose.orientation.w = std::cos(0.5 * p.yaw);
    point.longitudinal_velocity_mps = static_cast<float>(p.velocity_mps);
    point.acceleration_mps2 = static_cast<float>(p.acceleration_mps2);
    // time_from_start is informational for this follower; clamp the seconds
    // into int32 rather than casting a huge finite derived time out of range
    // (undefined behavior). convert() already rejects non-finite times.
    const double t = std::max(0.0, p.time_from_start_s);
    point.time_from_start.sec = static_cast<int32_t>(
      std::min(t, static_cast<double>(std::numeric_limits<int32_t>::max())));
    point.time_from_start.nanosec = static_cast<uint32_t>(
      std::min(999999999.0, std::round((t - std::floor(t)) * 1e9)));
    trajectory.points.push_back(point);
  }
  controller->current_trajectory_ = std::move(trajectory);
  controller->has_trajectory_ = true;
  controller->vp_candidate_identity_.note(msg->session, msg->cycle, stamp);
  controller->accepted_source_session_ = msg->session;
  controller->accepted_source_cycle_ = msg->cycle;
  controller->watch_candidate_.note(Clock::now());
}

void Controller::callbackDrivingCommand(const DrivingCommandMsg* msg, void* arg)
{
  Controller* controller = static_cast<Controller*>(arg);
  const double now = Clock::now();
  const DrivingCommandMsg in = *msg;  // scalar fields only: safe plain copy

  // 'valid=false' means VP produced no usable plan this cycle. That is a
  // legitimate miss, not itself a fault: persistent loss is covered by the
  // per-source freshness check on the control timer.
  if (!in.valid) {
    log_warn_throttle(
      "VP cycle %llu invalid: no usable plan this cycle",
      (unsigned long long)in.cycle);
    return;
  }
  if (!in.has_source_stamp) {
    log_warn_throttle(
      "VP cycle %llu has no source stamp: driving decision cannot be traced",
      (unsigned long long)in.cycle);
    return;
  }
  // In VP_CONTROL the command reaches the actuator unchanged, so this is the
  // only identity gate between VisionPilot and the vehicle. It is the same
  // gate the DrivingReference path uses: a repeated camera frame is not
  // fresh (a new cycle on an old frame must not keep the watchdog alive), a
  // retired or regressing session/cycle/stamp is a stream fault, and the first
  // sample is accepted whatever its cycle number.
  const SourceStamp stamp{in.source_stamp.sec, in.source_stamp.nanosec};
  const auto check = controller->vp_candidate_identity_.check(in.session, in.cycle, stamp);
  if (check == CandidateCheck::DUPLICATE) {
    return;  // same cycle or camera frame: do not refresh the watchdog
  }
  if (check != CandidateCheck::ACCEPT) {
    controller->latchFault("vp command regression or invalid stamp");
    log_warn(
      "VP command regression or invalid stamp: session %u cycle %llu",
      in.session, (unsigned long long)in.cycle);
    return;
  }
  if (!std::isfinite(in.steering_tire_angle_rad) ||
    !std::isfinite(in.target_speed_mps) || !std::isfinite(in.acceleration_mps2))
  {
    controller->latchFault("vp command not finite");
    log_warn("VP command cycle %llu has non-finite values", (unsigned long long)in.cycle);
    return;
  }
  if (std::fabs(in.steering_tire_angle_rad) > controller->max_steering_rad_) {
    controller->latchFault("vp steering out of range");
    log_warn(
      "VP steering %.3f rad beyond the actuation limit +/- %.3f rad (cycle %llu)",
      in.steering_tire_angle_rad, controller->max_steering_rad_, (unsigned long long)in.cycle);
    return;
  }
  if (in.target_speed_mps < 0.0 || in.target_speed_mps > controller->max_abs_velocity_mps_) {
    controller->latchFault("vp speed out of range");
    log_warn(
      "VP target speed %.3f m/s outside [0, %.1f] (cycle %llu)",
      in.target_speed_mps, controller->max_abs_velocity_mps_, (unsigned long long)in.cycle);
    return;
  }
  if (std::fabs(in.acceleration_mps2) > controller->max_abs_accel_mps2_) {
    controller->latchFault("vp accel out of range");
    log_warn(
      "VP acceleration %.3f m/s^2 beyond the actuation limit +/- %.1f (cycle %llu)",
      in.acceleration_mps2, controller->max_abs_accel_mps2_, (unsigned long long)in.cycle);
    return;
  }

  // The supervised request is accepted as-is: in VP_CONTROL SI must not
  // recompute VP's driving decision, only check and pass it (or stop).
  controller->vp_candidate_identity_.note(in.session, in.cycle, stamp);
  controller->current_vp_cmd_ = in;
  controller->has_vp_cmd_ = true;
  controller->watch_vp_cmd_.note(now);
  log_debug(
    "VP command accepted: session %u cycle %llu source %.3f steering %.4f speed %.3f accel %.3f",
    in.session, (unsigned long long)in.cycle, Clock::toDouble(in.source_stamp),
    in.steering_tire_angle_rad, in.target_speed_mps, in.acceleration_mps2);
}

void Controller::callbackReenable(const BoolMsg* msg, void* arg)
{
  Controller* controller = static_cast<Controller*>(arg);
  if (msg->data) {
    controller->reenable_.request(Clock::now());
    log_info(
      "Operator re-enable requested (takes effect when all sources are fresh, within %.1f s)",
      controller->reenable_window_s_);
  }
}

void Controller::callbackHeartbeat(const Float64StampedMsg* msg, void* arg)
{
  (void)msg;  // only the arrival counts; the value is VisionPilot's own command
  static_cast<Controller*>(arg)->watch_heartbeat_.note(Clock::now());
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
  // Cycle phase stopwatch (M2.1): one CYCLE line per cycle so the UART log shows
  // where the control period actually goes on hardware. Debug-only and compiled
  // out at the default INFO level (see PROFILE_* in logger.hpp).
  PROFILE_POINT(cyc_t0);

  const double now = Clock::now();

  // 1. Supervision: selected-source + feedback arrival checks. This runs
  // before anything else and is independent of controller readiness — a
  // fault on the selected source (or dead feedback) latches SI_STOP, and
  // the explicit stop is then published from here every control cycle,
  // never synthesized by anything downstream. Each source is checked under
  // its own timeout so a slow watchdog cannot be hidden by a fast one. The
  // fault-detection moment is this tick: that is where the agreed 500 ms
  // fault-to-first-applied-brake measurement starts, reported separately
  // from the source-loss moment itself (vp_si_control_contract).
  const char * stale_reason = nullptr;
  double stale_age = 0.0;
  const bool was_latched = supervision_.latched;
  if (supervisorStaleReason(now, &stale_reason, &stale_age)) {
    latchFault(std::string(stale_reason));
    if (!was_latched) {
      // Edge-only detail; the continuing state is logged by latchFault at
      // most when the reason changes. A per-tick line here would flood the
      // UART console for as long as the fault lasts.
      log_warn(
        "SI fault: %s arrived %.2f s ago; latching SI_STOP (fault_id %u)",
        stale_reason, stale_age, supervision_.fault_id);
    }
  }
#if defined(PLATFORM_FREERTOS_X5H)
  // rpmsg-si fault input from Linux. Only the rising edge latches: fault=0
  // does not release a stop, only an operator re-enable does.
  const bool si_fault = si_channel_fault() != 0;
  if (si_fault && !si_fault_was_) {
    latchFault("si fault input");
  }
  si_fault_was_ = si_fault;
#endif

  // 2. Re-enable: an explicit operator request clears the latch only once
  // every checked source is fresh again — a still-dead source cannot resume
  // normal driving (vp_si_control_contract: "a fault never silently changes
  // mode or source; resuming normal driving requires explicit re-enable").
  // A cleared latch resumes NORMAL supervision in this same tick (no extra
  // SI_STOP without an active fault).
  if (supervision_.latched) {
    if (reenable_.expireIfOld(now, reenable_window_s_)) {
      log_warn("Re-enable request expired after %.1f s without all sources fresh", reenable_window_s_);
    }
    const bool requested = reenable_.pending;
    if (supervision_.stopThisTick(reenable_.pending, requested && allSourcesFresh(now))) {
      if (requested) {
        log_warn_throttle(
          "Re-enable requested but not all sources are fresh; staying in SI_STOP");
      }
      publishSiStop(now);

      PROFILE_POINT(cyc_t_end);
      PROFILE_LOG(
        "CYCLE in=0.0 lat=0.0 lon=0.0 pub=0.0 total=%.1f [ms] (SI_STOP)",
        PROFILE_MS(cyc_t0, cyc_t_end));
      return;
    }
    log_info("SI re-enabled by operator request; resuming NORMAL supervision");
  }

  // 3. Supervisor decision for this cycle.
  if (supervision_mode_ == supervision::Mode::VP_CONTROL) {
    // Supervise-and-pass: SI must not recompute VP's driving decision, so
    // the accepted VP command becomes the approved payload unchanged.
    if (!has_vp_cmd_) {
      log_info_throttle("VP_CONTROL: no accepted VP command yet");
      publishHold();
      return;
    }
    // SI_CONTROL is held back by processData() until the vehicle feedback has
    // arrived; VP_CONTROL has no follower, so it needs the same guard. A
    // source that was never seen is not "stale", so without this a bridge that
    // never delivers odometry, acceleration, steering or operation mode would
    // let VisionPilot's commands through as NORMAL indefinitely.
    if (!feedbackSeen()) {
      log_info_throttle("VP_CONTROL: waiting for vehicle feedback (odometry, acceleration, steering, operation mode)");
      publishHold();
      return;
    }
    const ControlMsg approved = vpToControl(current_vp_cmd_);
    accepted_source_session_ = current_vp_cmd_.session;
    accepted_source_cycle_ = current_vp_cmd_.cycle;
    publishApprovedRequest(
      supervision::Decision::NORMAL, supervision::SelectedSource::VP_COMMAND,
      approved);

    PROFILE_POINT(cyc_t_end);
    PROFILE_LOG(
      "CYCLE in=0.0 lat=0.0 lon=0.0 pub=0.0 total=%.1f [ms] (VP)",
      PROFILE_MS(cyc_t0, cyc_t_end));
    return;
  }

  // 4. SI_CONTROL: the follower follows the selected trajectory source
  // (the SI-placed VP driving reference, or Autoware planning in the
  // third configuration). Its output is only a candidate input to the
  // gate; everything actuator-facing still goes through
  // publishApprovedRequest() below.
  const auto input_data = createInputData();
  if (!input_data) {
    log_info_throttle("Control is skipped since input data is not ready.");
    publishHold();
    return;
  }

  log_debug("Input data created");

  // 5. check if controllers are ready
  const bool is_lat_ready = lateral_controller_->isReady(*input_data);
  const bool is_lon_ready = longitudinal_controller_->isReady(*input_data);
  if (!is_lat_ready || !is_lon_ready) {
    log_info_throttle("Control is skipped since lateral and/or longitudinal controllers are not ready to run.");
    publishHold();
    return;
  }

  log_debug("Controllers are ready");

  PROFILE_POINT(cyc_t_ready);

  // 6. run controllers
  stop_watch_.tic("lateral");
  const auto lat_out = lateral_controller_->run(*input_data);
  stop_watch_.toc("lateral");
  log_debug("Lateral controller elapsed time: %f", stop_watch_.toc("lateral"));

  log_debug("-------LAT OUT--", 0);
  log_debug("Lateral output: %f", lat_out.control_cmd.steering_tire_angle);
  log_debug("Lateral steering tire rotation rate: %f", lat_out.control_cmd.steering_tire_rotation_rate);
  log_debug("Lateral is defined steering tire rotation rate: %s", lat_out.control_cmd.is_defined_steering_tire_rotation_rate ? "true" : "false");

  publishProcessingTime(stop_watch_.toc("lateral"), pub_processing_time_lat_ms_);

  PROFILE_POINT(cyc_t_lat);

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

  PROFILE_POINT(cyc_t_lon);

  // 7. sync with each other controllers
  longitudinal_controller_->sync(lat_out.sync_data);
  lateral_controller_->sync(lon_out.sync_data);

  log_debug("Controllers synced");

  // TODO(Horibe): Think specification. This comes from the old implementation.
  // if (isTimeOut(lon_out, lat_out)) return;

  // 8. publish through the gate. The former all-inputs staleness gate is
  // gone: it was refreshed by *any* input and declared "stale" with the
  // freshest sample as its metric, so the loss of one selected source
  // could be masked by churn on the others (vp_si_control_contract).
  // Step 1 above replaces it with named per-source checks and a stop.
  publishControlCommand(lon_out, lat_out);

  PROFILE_POINT(cyc_t_end);
  PROFILE_LOG("CYCLE in=%.1f lat=%.1f lon=%.1f pub=%.1f total=%.1f [ms]",
    PROFILE_MS(cyc_t0, cyc_t_ready), PROFILE_MS(cyc_t_ready, cyc_t_lat),
    PROFILE_MS(cyc_t_lat, cyc_t_lon), PROFILE_MS(cyc_t_lon, cyc_t_end),
    PROFILE_MS(cyc_t0, cyc_t_end));

  // 9. Reset flags for next cycle
  // TODO: Check if this is required, autoware version keeps publishing even there is no new data
  // reset_data_flags();
}

void Controller::publishControlCommand(
  const trajectory_follower::LongitudinalOutput & lon_out,
  const trajectory_follower::LateralOutput & lat_out)
{
  ControlMsg out{};
  out.stamp = Clock::toRosTime(Clock::now());
  out.lateral.steering_tire_angle = lat_out.control_cmd.steering_tire_angle;
  out.lateral.steering_tire_rotation_rate = lat_out.control_cmd.steering_tire_rotation_rate;
  out.lateral.is_defined_steering_tire_rotation_rate = lat_out.control_cmd.is_defined_steering_tire_rotation_rate;
  out.lateral.stamp = out.stamp;
  out.longitudinal = lon_out.control_cmd;

  // The follower only proposes: a non-finite or out-of-envelope command is a
  // bad normal output and latches SI_STOP instead of being approved (speed and
  // acceleration use the same limits VP_CONTROL applies to VisionPilot's command;
  // steering uses the vehicle's own limit, see follower_max_steer_rad_). This is the one
  // place a candidate from the follower becomes NORMAL, so a bad trajectory
  // source, MPC output or speed horizon cannot reach the actuator unchecked.
  if (const char * why = supervision::envelopeViolation(
      out.lateral.steering_tire_angle, out.longitudinal.velocity, out.longitudinal.acceleration,
      // + 1e-3 rad: the MPC clamps in float, exactly at its limit.
      {follower_max_steer_rad_ + 1e-3, max_abs_velocity_mps_, max_abs_accel_mps2_}))
  {
    latchFault(std::string("follower ") + why);
    log_warn(
      "Follower output rejected (%s): steer %.3f rad, speed %.2f m/s, accel %.2f m/s^2",
      why, static_cast<double>(out.lateral.steering_tire_angle),
      static_cast<double>(out.longitudinal.velocity),
      static_cast<double>(out.longitudinal.acceleration));
    publishSiStop(Clock::now());
    return;
  }

  // The follower output becomes the approved payload of this cycle; every
  // actuator-facing surface (DDS and CAN, supervised topic and legacy
  // topic) is written only by publishApprovedRequest() below.
  publishApprovedRequest(
    supervision::Decision::NORMAL, supervision::SelectedSource::FOLLOWER, out);
}

void Controller::latchFault(const std::string & why)
{
  if (!supervision_.latched) {
    supervision_.latch(why);
    // Origin of the speed ramp in stoppedPayload(). current_odometry_ is
    // uninitialized until its first sample, and a heartbeat can latch first.
    stop_v0_mps_ = has_odometry_ ? current_odometry_.twist.twist.linear.x : 0.0;
    stop_t0_ = Clock::now();
    log_warn("SI_STOP latched: %s (fault_id %u)", why.c_str(), supervision_.fault_id);
    reenable_.clear();  // a fault invalidates an earlier re-enable request
  } else if (supervision_.reason != why) {
    supervision_.reason = why;
    // A different fault than the one the operator's press answered: the press
    // predates it, so it must not clear it.
    reenable_.clear();
    log_warn_throttle("SI_STOP continues: %s (fault_id %u)", why.c_str(), supervision_.fault_id);
  }
}

bool Controller::supervisorStaleReason(double now, const char ** reason_out, double * age_out) const
{
  struct Check {
    const supervision::SourceWatch * watch;
    double timeout;
    const char * name;
  };
  const Check feedback[] = {
    {&watch_odom_, source_timeout_ego_, "odometry"},
    {&watch_accel_, source_timeout_ego_, "acceleration"},
    {&watch_steering_, source_timeout_steering_, "steering report"},
    {&watch_opmode_, source_timeout_opmode_, "operation mode"},
    {&watch_heartbeat_, source_timeout_heartbeat_, "vp heartbeat"},
  };
  for (const auto & check : feedback) {
    if (check.watch->stale(now, check.timeout)) {
      *reason_out = check.name;
      *age_out = check.watch->ageSec(now);
      return true;
    }
  }
  if (supervision_mode_ == supervision::Mode::VP_CONTROL) {
    if (watch_vp_cmd_.stale(now, source_timeout_candidate_)) {
      *reason_out = "vp command";
      *age_out = watch_vp_cmd_.ageSec(now);
      return true;
    }
  } else {
    if (watch_candidate_.stale(now, source_timeout_candidate_)) {
      *reason_out = "trajectory";
      *age_out = watch_candidate_.ageSec(now);
      return true;
    }
  }
  *reason_out = nullptr;
  return false;
}

bool Controller::allSourcesFresh(double now) const
{
  struct Check {
    const supervision::SourceWatch * watch;
    double timeout;
  };
  const Check checks[] = {
    {&watch_odom_, source_timeout_ego_},
    {&watch_accel_, source_timeout_ego_},
    {&watch_steering_, source_timeout_steering_},
    {&watch_opmode_, source_timeout_opmode_},
  };
  for (const auto & check : checks) {
    if (!check.watch->ever() || check.watch->stale(now, check.timeout)) {
      return false;
    }
  }
  // Optional: a deployment without VisionPilot never sends a heartbeat.
  if (watch_heartbeat_.stale(now, source_timeout_heartbeat_)) {
    return false;
  }
#if defined(PLATFORM_FREERTOS_X5H)
  // A fault input still held at 1 has not recovered.
  if (si_channel_fault() != 0) {
    return false;
  }
#endif
  const supervision::SourceWatch * selected =
    supervision_mode_ == supervision::Mode::VP_CONTROL ? &watch_vp_cmd_ : &watch_candidate_;
  if (!selected->ever() ||
    selected->stale(now, source_timeout_candidate_))
  {
    return false;
  }
  return true;
}

ControlMsg Controller::vpToControl(const DrivingCommandMsg & cmd) const
{
  ControlMsg out{};
  out.stamp = Clock::toRosTime(Clock::now());
  out.lateral.stamp = out.stamp;
  out.lateral.steering_tire_angle = static_cast<float>(cmd.steering_tire_angle_rad);
  out.lateral.steering_tire_rotation_rate = 0.0f;
  out.lateral.is_defined_steering_tire_rotation_rate = false;
  out.longitudinal.stamp = out.stamp;
  out.longitudinal.velocity = static_cast<float>(cmd.target_speed_mps);
  out.longitudinal.acceleration = static_cast<float>(cmd.acceleration_mps2);
  out.longitudinal.jerk = 0.0f;
  out.longitudinal.is_defined_acceleration = true;
  out.longitudinal.is_defined_jerk = false;
  return out;
}

void Controller::publishApprovedRequest(
  const supervision::Decision decision, const supervision::SelectedSource source,
  const ControlMsg & control)
{
  if (decision == supervision::Decision::NORMAL) {
    last_approved_ = control;
    has_approved_ = true;
  }
  const TimeMsg approved_stamp = Clock::toRosTime(Clock::now());

  // Transitional bridge-compatibility surface: the gate-approved payload on
  // the legacy control_cmd topic so the current CARLA bridge keeps
  // actuating until the single-writer CARLA actuator (E2E #2) consumes
  // ApprovedRequest directly. This is the approved payload, never the raw
  // follower output: SI_STOP and HOLD also flow here.
  ControlMsg legacy = control;
  legacy.stamp = approved_stamp;
  legacy.lateral.stamp = approved_stamp;

  // Legacy DDS surface policy: STOP_ONLY publishes only SI_STOP here, so a
  // consumer that lets a fresh SI command win over its own driver hears SI
  // only when SI overrides. It never affects ApprovedRequest or CAN below.
  const bool legacy_dds_enabled =
    startup_config_.legacy_control_cmd == StartupConfig::LegacyControlCmd::ALWAYS ||
    decision == supervision::Decision::SI_STOP;
  if (legacy_dds_enabled && common::can::output_mode_uses_dds(output_mode_)) {
    if (control_cmd_pub_) {
      if (!control_cmd_pub_->publish(legacy)) {
        log_error("Legacy control_cmd publication failed (approved still counted)");
      }
    }
  }

  if (common::can::output_mode_uses_can(output_mode_)) {
    if (!can_output_ || !can_output_->send(legacy, output_mode_)) {
      if (output_mode_ == common::can::ControlCommandOutputMode::CAN_ONLY) {
        log_error("Control command not sent over CAN in CAN_ONLY mode");
      } else {
        log_warn_throttle("Control command not sent over CAN; DDS output remains active");
      }
    }
  }

  // The supervised output: SI-only authoring with identity and decision.
  ApprovedRequestMsg approved{};
  approved.stamp = approved_stamp;
  approved.session = si_session_;
  approved.output_sequence = ++out_seq_;
  approved.decision = static_cast<uint8_t>(decision);
  approved.mode = static_cast<uint8_t>(supervision_mode_);
  approved.selected_source = static_cast<uint8_t>(source);
  approved.source_session = accepted_source_session_;
  approved.source_cycle = accepted_source_cycle_;
  approved.fault_id = supervision_.latched ? supervision_.fault_id : 0;
  approved.control = control;

  if (approved_request_pub_ && approved_request_pub_->publish(approved)) {
    log_debug(
      "Approved request published: seq %llu decision %u source %u fault %u",
      (unsigned long long)approved.output_sequence, approved.decision,
      approved.selected_source, approved.fault_id);
  } else {
    log_error("ApprovedRequest publication failed");
  }
}

ControlMsg Controller::stoppedPayload(double now) const
{
  // Never command a steer reset: hold the last approved steering, and before
  // any command was approved the vehicle's own steering report.
  ControlMsg seed{};
  const ControlMsg * known = nullptr;
  if (has_approved_) {
    known = &last_approved_;
  } else if (has_steering_) {
    seed.lateral.steering_tire_angle = current_steering_.steering_tire_angle;
    known = &seed;
  }
  ControlMsg out = supervision::StopControl{}(known, stop_decel_mps2_);
  if (supervision_.latched) {
    out.longitudinal.velocity = static_cast<float>(
      supervision::stopRampVelocity(stop_v0_mps_, stop_decel_mps2_, now - stop_t0_));
  }
  out.stamp = Clock::toRosTime(now);
  out.lateral.stamp = out.stamp;
  out.longitudinal.stamp = out.stamp;
  return out;
}

bool Controller::feedbackSeen() const
{
  return watch_odom_.ever() && watch_accel_.ever() && watch_steering_.ever() &&
         watch_opmode_.ever();
}

void Controller::publishSiStop(double now)
{
  publishApprovedRequest(
    supervision::Decision::SI_STOP,
    supervision_mode_ == supervision::Mode::VP_CONTROL ?
    supervision::SelectedSource::VP_COMMAND : supervision::SelectedSource::FOLLOWER,
    stoppedPayload(now));
}

void Controller::publishHold()
{
  // HOLD means "no new approved decision this cycle" (nothing to follow yet,
  // or the follower is not ready). The ApprovedRequest consumer keeps its
  // previous state; the payload only reaches the legacy control_cmd/CAN
  // surface, where an all-zero command is a zero acceleration, i.e. a brake
  // release. Carry the conservative stopped payload instead so a not-ready
  // cycle never releases the brake on that surface.
  publishApprovedRequest(
    supervision::Decision::HOLD,
    supervision_mode_ == supervision::Mode::VP_CONTROL ?
    supervision::SelectedSource::VP_COMMAND : supervision::SelectedSource::FOLLOWER,
    stoppedPayload(Clock::now()));
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
