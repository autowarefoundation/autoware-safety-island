// Copyright 2021 The Autoware Foundation
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

#include <algorithm>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

#include "autoware/mpc_lateral_controller/mpc_lateral_controller.hpp"
#include "autoware/motion_utils/trajectory/trajectory.hpp"
#include "autoware/mpc_lateral_controller/qp_solver/qp_solver_unconstraint_fast.hpp"
#include "autoware/mpc_lateral_controller/vehicle_model/vehicle_model_bicycle_dynamics.hpp"
#include "autoware/mpc_lateral_controller/vehicle_model/vehicle_model_bicycle_kinematics.hpp"
#include "autoware/mpc_lateral_controller/vehicle_model/vehicle_model_bicycle_kinematics_no_delay.hpp"
#include "autoware_vehicle_info_utils/vehicle_info_utils.hpp"

#include "common/logger/logger.hpp"
#include "autoware/autoware_msgs/messages.hpp"
using namespace common::logger;

namespace autoware::motion::control::mpc_lateral_controller
{

MpcLateralController::MpcLateralController(Node & node)
{
  const auto dp_int = [&](const std::string & s) { return node.declare_parameter<int>(s); };
  const auto dp_bool = [&](const std::string & s) { return node.declare_parameter<bool>(s); };
  const auto dp_double = [&](const std::string & s) { return node.declare_parameter<double>(s); };

  m_mpc = std::make_unique<MPC>(node);

  m_mpc->m_ctrl_period = node.get_parameter<double>("ctrl_period");

  auto & p_filt = m_trajectory_filtering_param;
  p_filt.enable_path_smoothing = node.declare_parameter<bool>("enable_path_smoothing", false);
  p_filt.path_filter_moving_ave_num = node.declare_parameter<int>("path_filter_moving_ave_num", 25);
  p_filt.curvature_smoothing_num_traj = node.declare_parameter<int>("curvature_smoothing_num_traj", 15);
  p_filt.curvature_smoothing_num_ref_steer = node.declare_parameter<int>("curvature_smoothing_num_ref_steer", 15);
  p_filt.traj_resample_dist = node.declare_parameter<double>("traj_resample_dist", 0.1);
  p_filt.extend_trajectory_for_end_yaw_control = node.declare_parameter<bool>("extend_trajectory_for_end_yaw_control", false);

  m_mpc->m_admissible_position_error = node.declare_parameter<double>("admissible_position_error", 5.0);
  m_mpc->m_admissible_yaw_error_rad = node.declare_parameter<double>("admissible_yaw_error_rad", 1.57);
  m_mpc->m_use_steer_prediction = node.declare_parameter<bool>("use_steer_prediction", false);
  m_mpc->m_param.steer_tau = node.declare_parameter<double>("vehicle_model_steer_tau", 0.27);

  /* stop state parameters */
  m_stop_state_entry_ego_speed = node.declare_parameter<double>("stop_state_entry_ego_speed", 0.001);
  m_stop_state_entry_target_speed = node.declare_parameter<double>("stop_state_entry_target_speed", 0.001);
  m_converged_steer_rad = node.declare_parameter<double>("converged_steer_rad", 0.1);
  m_keep_steer_control_until_converged = node.declare_parameter<bool>("keep_steer_control_until_converged", true);
  m_new_traj_duration_time = node.declare_parameter<double>("new_traj_duration_time", 1.0);            // [s]
  m_new_traj_end_dist = node.declare_parameter<double>("new_traj_end_dist", 0.3);                      // [m]
  m_mpc_converged_threshold_rps = node.declare_parameter<double>("mpc_converged_threshold_rps", 0.01);  // [rad/s]

  /* mpc parameters */
  const auto vehicle_info = autoware::vehicle_info_utils::VehicleInfoUtils(node).getVehicleInfo();
  const double wheelbase = vehicle_info.wheel_base_m;
  constexpr double deg2rad = static_cast<double>(M_PI) / 180.0;
  m_mpc->m_steer_lim = vehicle_info.max_steer_angle_rad;

  // steer rate limit depending on curvature
  const auto steer_rate_lim_dps_list_by_curvature =
    node.declare_parameter<std::vector<double>>("steer_rate_lim_dps_list_by_curvature", {40.0, 50.0, 60.0});
  const auto curvature_list_for_steer_rate_lim =
    node.declare_parameter<std::vector<double>>("curvature_list_for_steer_rate_lim", {0.001, 0.002, 0.01});
  for (size_t i = 0; i < steer_rate_lim_dps_list_by_curvature.size(); ++i) {
    m_mpc->m_steer_rate_lim_map_by_curvature.emplace_back(
      curvature_list_for_steer_rate_lim.at(i),
      steer_rate_lim_dps_list_by_curvature.at(i) * deg2rad);
  }

  // steer rate limit depending on velocity
  const auto steer_rate_lim_dps_list_by_velocity =
    node.declare_parameter<std::vector<double>>("steer_rate_lim_dps_list_by_velocity", {60.0, 50.0, 40.0});
  const auto velocity_list_for_steer_rate_lim =
    node.declare_parameter<std::vector<double>>("velocity_list_for_steer_rate_lim", {10.0, 15.0, 20.0});
  for (size_t i = 0; i < steer_rate_lim_dps_list_by_velocity.size(); ++i) {
    m_mpc->m_steer_rate_lim_map_by_velocity.emplace_back(
      velocity_list_for_steer_rate_lim.at(i), steer_rate_lim_dps_list_by_velocity.at(i) * deg2rad);
  }

  /* vehicle model setup */
  auto vehicle_model_ptr =
    createVehicleModel(wheelbase, m_mpc->m_steer_lim, m_mpc->m_param.steer_tau, node);
  m_mpc->setVehicleModel(vehicle_model_ptr);

  /* QP solver setup */
  auto qpsolver_ptr = createQPSolverInterface(node);
  m_mpc->setQPSolver(qpsolver_ptr);

  /* delay compensation */
  {
    const double delay_tmp = node.declare_parameter<double>("input_delay", 0.0);
    const double delay_step = std::round(delay_tmp / m_mpc->m_ctrl_period);
    m_mpc->m_param.input_delay = delay_step * m_mpc->m_ctrl_period;
    m_mpc->m_input_buffer = std::deque<double>(static_cast<size_t>(delay_step), 0.0);
  }

  /* steering offset compensation */
  enable_auto_steering_offset_removal_ =
    node.declare_parameter<bool>("steering_offset.enable_auto_steering_offset_removal", false);
  steering_offset_ = createSteerOffsetEstimator(wheelbase, node);

  /* initialize low-pass filter */
  {
    const double steering_lpf_cutoff_hz = node.declare_parameter<double>("steering_lpf_cutoff_hz", 10.0);
    const double error_deriv_lpf_cutoff_hz = node.declare_parameter<double>("error_deriv_lpf_cutoff_hz", 10.0);
    m_mpc->initializeLowPassFilters(steering_lpf_cutoff_hz, error_deriv_lpf_cutoff_hz);
  }

  // ego nearest index search
  const auto check_and_get_param = [&](const auto & param) {
    return node.has_parameter(param) ? node.get_parameter<double>(param) : node.declare_parameter<double>(param);
  };
  m_ego_nearest_dist_threshold = check_and_get_param("ego_nearest_dist_threshold");
  m_ego_nearest_yaw_threshold = check_and_get_param("ego_nearest_yaw_threshold");
  m_mpc->ego_nearest_dist_threshold = m_ego_nearest_dist_threshold;
  m_mpc->ego_nearest_yaw_threshold = m_ego_nearest_yaw_threshold;

  m_mpc->m_use_delayed_initial_state = node.declare_parameter<bool>("use_delayed_initial_state", true);

  m_mpc->m_publish_debug_trajectories = node.declare_parameter<bool>("publish_debug_trajectories", false);

  // m_pub_predicted_traj = node.create_publisher<TrajectoryMsg>("~/output/predicted_trajectory", &autoware_planning_msgs_msg_Trajectory_desc);
  // m_pub_steer_offset = node.create_publisher<Float32StampedMsg>("~/output/estimated_steer_offset", &tier4_debug_msgs_msg_Float32Stamped_desc);

  declareMPCparameters(node);

  m_mpc->initializeSteeringPredictor();
}

MpcLateralController::~MpcLateralController()
{
}

std::shared_ptr<VehicleModelInterface> MpcLateralController::createVehicleModel(
  const double wheelbase, const double steer_lim, const double steer_tau, Node & node)
{
  std::shared_ptr<VehicleModelInterface> vehicle_model_ptr;

  const std::string vehicle_model_type = node.declare_parameter<std::string>("vehicle_model_type", "kinematics");

  if (vehicle_model_type == "kinematics") {
    vehicle_model_ptr = std::make_shared<KinematicsBicycleModel>(wheelbase, steer_lim, steer_tau);
    return vehicle_model_ptr;
  }

  if (vehicle_model_type == "kinematics_no_delay") {
    vehicle_model_ptr = std::make_shared<KinematicsBicycleModelNoDelay>(wheelbase, steer_lim);
    return vehicle_model_ptr;
  }

  if (vehicle_model_type == "dynamics") {
    //TODO: it should not be get in here
    const double mass_fl = node.declare_parameter<double>("vehicle.mass_fl");
    const double mass_fr = node.declare_parameter<double>("vehicle.mass_fr");
    const double mass_rl = node.declare_parameter<double>("vehicle.mass_rl");
    const double mass_rr = node.declare_parameter<double>("vehicle.mass_rr");
    const double cf = node.declare_parameter<double>("vehicle.cf");
    const double cr = node.declare_parameter<double>("vehicle.cr");

    // vehicle_model_ptr is only assigned in ctor, so parameter value have to be passed at init time
    vehicle_model_ptr =
      std::make_shared<DynamicsBicycleModel>(wheelbase, mass_fl, mass_fr, mass_rl, mass_rr, cf, cr);
    return vehicle_model_ptr;
  }

  log_error("MPC: vehicle_model_type is undefined");
  return vehicle_model_ptr;
}

std::shared_ptr<QPSolverInterface> MpcLateralController::createQPSolverInterface(
  Node & node)
{
  std::shared_ptr<QPSolverInterface> qpsolver_ptr;

  const std::string qp_solver_type = node.declare_parameter<std::string>("qp_solver_type", "unconstraint_fast");

  if (qp_solver_type == "unconstraint_fast") {
    qpsolver_ptr = std::make_shared<QPSolverEigenLeastSquareLLT>();
    return qpsolver_ptr;
  }

  log_error("MPC: qp_solver_type is undefined");
  return qpsolver_ptr;
}

std::shared_ptr<SteeringOffsetEstimator> MpcLateralController::createSteerOffsetEstimator(
  const double wheelbase, Node & node)
{
  const std::string ns = "steering_offset.";
  const auto vel_thres = node.declare_parameter<double>(ns + "update_vel_threshold", 5.56);
  const auto steer_thres = node.declare_parameter<double>(ns + "update_steer_threshold", 0.035);
  const auto limit = node.declare_parameter<double>(ns + "steering_offset_limit", 0.02);
  const auto num = node.declare_parameter<int>(ns + "average_num", 1000);
  steering_offset_ =
    std::make_shared<SteeringOffsetEstimator>(wheelbase, num, vel_thres, steer_thres, limit);
  return steering_offset_;
}

trajectory_follower::LateralOutput MpcLateralController::run(
  trajectory_follower::InputData const & input_data)
{
  log_debug("MPC: Running");

  // set input data
  setTrajectory(input_data.current_trajectory, input_data.current_odometry);

  m_current_kinematic_state = input_data.current_odometry;
  m_current_steering = input_data.current_steering;
  if (enable_auto_steering_offset_removal_) {
    m_current_steering.steering_tire_angle -= steering_offset_->getOffset();
  }

  LateralMsg ctrl_cmd;
  TrajectoryMsg predicted_traj;
  Float32MultiArrayStampedMsg debug_values;

  const bool is_under_control = input_data.current_operation_mode.is_autoware_control_enabled &&
                                input_data.current_operation_mode.mode == OPERATION_MODE_STATE_AUTONOMOUS;

  if (!m_is_ctrl_cmd_prev_initialized || !is_under_control) {
    m_ctrl_cmd_prev = getInitialControlCommand();
    log_debug("MPC: Initial control command");
    m_is_ctrl_cmd_prev_initialized = true;
  }

  log_debug("MPC: Calculating");

  trajectory_follower::LateralHorizon ctrl_cmd_horizon{};
  const auto mpc_solved_status = m_mpc->calculateMPC(
    m_current_steering, m_current_kinematic_state, ctrl_cmd, predicted_traj, ctrl_cmd_horizon);

  log_debug("MPC: Calculated");

  if (
    (m_mpc_solved_status.result == true && mpc_solved_status.result == false) ||
    (!mpc_solved_status.result && mpc_solved_status.reason != m_mpc_solved_status.reason)) {
    log_error("MPC: failed due to %s", mpc_solved_status.reason.c_str());
  }
  m_mpc_solved_status = mpc_solved_status;  // for diagnostic updater

  log_debug("MPC: Solved status: %s", mpc_solved_status.result ? "true" : "false");

  // reset previous MPC result
  // Note: When a large deviation from the trajectory occurs, the optimization stops and
  // the vehicle will return to the path by re-planning the trajectory or external operation.
  // After the recovery, the previous value of the optimization may deviate greatly from
  // the actual steer angle, and it may make the optimization result unstable.
  if (!mpc_solved_status.result || !is_under_control) {
    m_mpc->resetPrevResult(m_current_steering);
  } else {
    setSteeringToHistory(ctrl_cmd);
  }

  log_debug("MPC: Steering Offset Removed");

  if (enable_auto_steering_offset_removal_) {
    steering_offset_->updateOffset(
      m_current_kinematic_state.twist.twist,
      input_data.current_steering.steering_tire_angle);  // use unbiased steering
    ctrl_cmd.steering_tire_angle += steering_offset_->getOffset();
  }

  log_debug("MPC: Steering Offset Added");

  // publishPredictedTraj(predicted_traj);  // TODO: REMOVED FOR SIMPLIFICATION
  // publishDebugValues(debug_values);  //TODO: removed sake of simplicity

  const auto createLateralOutput =
    [this](
      const auto & cmd, const bool is_mpc_solved,
      const auto & cmd_horizon) -> trajectory_follower::LateralOutput {
    trajectory_follower::LateralOutput output;
    output.control_cmd = createCtrlCmdMsg(cmd);
    output.control_cmd_horizon = createCtrlCmdHorizonMsg(cmd_horizon);
    // To be sure current steering of the vehicle is desired steering angle, we need to check
    // following conditions.
    // 1. At the last loop, mpc should be solved because command should be optimized output.
    // 2. The mpc should be converged.
    // 3. The steer angle should be converged.
    output.sync_data.is_steer_converged =
      is_mpc_solved && isMpcConverged() && isSteerConverged(cmd);

    return output;
  };

  if (isStoppedState()) {
    // Reset input buffer
    for (auto & value : m_mpc->m_input_buffer) {
      value = m_ctrl_cmd_prev.steering_tire_angle;
    }
    log_debug("MPC: is stopped");
    // Use previous command value as previous raw steer command
    m_mpc->m_raw_steer_cmd_prev = m_ctrl_cmd_prev.steering_tire_angle;
    return createLateralOutput(m_ctrl_cmd_prev, false, ctrl_cmd_horizon);
  }

  log_debug("MPC: is not stopped");

  if (!mpc_solved_status.result) {
    ctrl_cmd = getStopControlCommand();
  }

  log_debug("MPC: Control Command Created");

  m_ctrl_cmd_prev = ctrl_cmd;
  return createLateralOutput(ctrl_cmd, mpc_solved_status.result, ctrl_cmd_horizon);
}

bool MpcLateralController::isSteerConverged(const LateralMsg & cmd) const
{
  // wait for a while to propagate the trajectory shape to the output command when the trajectory
  // shape is changed.
  if (!m_has_received_first_trajectory || isTrajectoryShapeChanged()) {
    log_info("MPC: trajectory shaped is changed");
    return false;
  }

  const bool is_converged =
    std::abs(cmd.steering_tire_angle - m_current_steering.steering_tire_angle) <
    static_cast<float>(m_converged_steer_rad);

  return is_converged;
}

bool MpcLateralController::isReady(const trajectory_follower::InputData & input_data)
{
  setTrajectory(input_data.current_trajectory, input_data.current_odometry);
  m_current_kinematic_state = input_data.current_odometry;
  m_current_steering = input_data.current_steering;

  if (!m_mpc->hasVehicleModel()) {
    log_info_throttle("MPC does not have a vehicle model");
    return false;
  }
  if (!m_mpc->hasQPSolver()) {
    log_info_throttle("MPC does not have a QP solver");
    return false;
  }
  if (m_mpc->m_reference_trajectory.empty()) {
    log_info_throttle("trajectory size is zero.");
    return false;
  }

  return true;
}

void MpcLateralController::setTrajectory(
  const TrajectoryMsg & msg, const OdometryMsg & current_kinematics)
{
  m_current_trajectory = msg;

  if (msg.points.size() < 3) {
    log_error("MPC: received path size is < 3, not enough.");
    return;
  }

  if (!isValidTrajectory(msg)) {
    log_error("MPC: Trajectory is invalid!! stop computing.");
    return;
  }

  m_mpc->setReferenceTrajectory(msg, m_trajectory_filtering_param, current_kinematics);

  // update trajectory buffer to check the trajectory shape change.
  m_trajectory_buffer.push_back(m_current_trajectory);
  while (1) { //TODO: a good replacement for rclcpp::ok() ?
    const auto time_diff = Clock::toDouble(m_trajectory_buffer.back().header.stamp) -
                           Clock::toDouble(m_trajectory_buffer.front().header.stamp);

    const double first_trajectory_duration_time = 5.0;
    const double duration_time =
      m_has_received_first_trajectory ? m_new_traj_duration_time : first_trajectory_duration_time;
    if (time_diff < duration_time) {
      m_has_received_first_trajectory = true;
      break;
    }
    m_trajectory_buffer.pop_front();
  }
}

LateralMsg MpcLateralController::getStopControlCommand() const
{
  LateralMsg cmd;
  cmd.steering_tire_angle = static_cast<decltype(cmd.steering_tire_angle)>(m_steer_cmd_prev);
  cmd.steering_tire_rotation_rate = 0.0;
  return cmd;
}

LateralMsg MpcLateralController::getInitialControlCommand() const
{
  LateralMsg cmd;
  cmd.steering_tire_angle = m_current_steering.steering_tire_angle;
  cmd.steering_tire_rotation_rate = 0.0;
  return cmd;
}

bool MpcLateralController::isStoppedState() const
{
  // If the nearest index is not found, return false
  if (m_current_trajectory.points.empty()) {
    return false;
  }

  // Note: This function used to take into account the distance to the stop line
  // for the stop state judgement. However, it has been removed since the steering
  // control was turned off when approaching/exceeding the stop line on a curve or
  // emergency stop situation and it caused large tracking error.
  const size_t nearest = autoware::motion_utils::findFirstNearestIndexWithSoftConstraints(
    m_current_trajectory.points, m_current_kinematic_state.pose.pose, m_ego_nearest_dist_threshold,
    m_ego_nearest_yaw_threshold);

  const double current_vel = m_current_kinematic_state.twist.twist.linear.x;
  const double target_vel = m_current_trajectory.points.at(nearest).longitudinal_velocity_mps;

  const auto latest_published_cmd = m_ctrl_cmd_prev;  // use prev_cmd as a latest published command
  if (m_keep_steer_control_until_converged && !isSteerConverged(latest_published_cmd)) {
    return false;  // not stopState: keep control
  }

  if (
    std::fabs(current_vel) < m_stop_state_entry_ego_speed &&
    std::fabs(target_vel) < m_stop_state_entry_target_speed) {
    return true;
  } else {
    return false;
  }
}

LateralMsg MpcLateralController::createCtrlCmdMsg(const LateralMsg & ctrl_cmd)
{
  auto out = ctrl_cmd;
  out.stamp = Clock::toRosTime(Clock::now());
  m_steer_cmd_prev = out.steering_tire_angle;
  return out;
}

LateralHorizon MpcLateralController::createCtrlCmdHorizonMsg(
  const LateralHorizon & ctrl_cmd_horizon) const
{
  auto out = ctrl_cmd_horizon;
  const auto now = Clock::toRosTime(Clock::now());
  for (auto & cmd : out.controls) {
    cmd.stamp = now;
  }
  return out;
}

void MpcLateralController::publishPredictedTraj(TrajectoryMsg & predicted_traj) const
{
  predicted_traj.header.stamp = Clock::toRosTime(Clock::now());
  predicted_traj.header.frame_id = m_current_trajectory.header.frame_id;
  m_pub_predicted_traj->publish(predicted_traj);
}

void MpcLateralController::publishDebugValues(Float32MultiArrayStampedMsg & debug_values) const
{
  debug_values.stamp = Clock::toRosTime(Clock::now());
  m_pub_debug_values->publish(debug_values);

  Float32StampedMsg offset;
  offset.stamp = Clock::toRosTime(Clock::now());
  offset.data = steering_offset_->getOffset();
  m_pub_steer_offset->publish(offset);
}

void MpcLateralController::setSteeringToHistory(const LateralMsg & steering)
{
  const auto time = Clock::now();
  if (m_mpc_steering_history.empty()) {
    m_mpc_steering_history.emplace_back(steering, time);
    m_is_mpc_history_filled = false;
    return;
  }

  m_mpc_steering_history.emplace_back(steering, time);

  // Check the history is filled or not.
  if ((time - m_mpc_steering_history.begin()->second) >= 1.0) {
    m_is_mpc_history_filled = true;
    // remove old data that is older than 1 sec
    for (auto itr = m_mpc_steering_history.begin(); itr != m_mpc_steering_history.end(); ++itr) {
      if ((time - itr->second) >= 1.0) {
        m_mpc_steering_history.erase(m_mpc_steering_history.begin());
      } else {
        break;
      }
    }
  } else {
    m_is_mpc_history_filled = false;
  }
}

bool MpcLateralController::isMpcConverged()
{
  // If the number of variable below the 2, there is no enough data so MPC is not converged.
  if (m_mpc_steering_history.size() < 2) {
    return false;
  }

  // If the history is not filled, return false.

  if (!m_is_mpc_history_filled) {
    return false;
  }

  // Find the maximum and minimum values of the steering angle in the past 1 second.
  double min_steering_value = m_mpc_steering_history[0].first.steering_tire_angle;
  double max_steering_value = min_steering_value;
  for (size_t i = 1; i < m_mpc_steering_history.size(); i++) {
    if (m_mpc_steering_history.at(i).first.steering_tire_angle < min_steering_value) {
      min_steering_value = m_mpc_steering_history.at(i).first.steering_tire_angle;
    }
    if (m_mpc_steering_history.at(i).first.steering_tire_angle > max_steering_value) {
      max_steering_value = m_mpc_steering_history.at(i).first.steering_tire_angle;
    }
  }
  return (max_steering_value - min_steering_value) < m_mpc_converged_threshold_rps;
}

void MpcLateralController::declareMPCparameters(Node & node)
{
  m_mpc->m_param.prediction_horizon = node.declare_parameter<int>("mpc_prediction_horizon", 50);
  m_mpc->m_param.prediction_dt = node.declare_parameter<double>("mpc_prediction_dt", 0.1);

  // const auto dp = [&](const auto & param) { return node.declare_parameter<double>(param); };

  auto & nw = m_mpc->m_param.nominal_weight;
  nw.lat_error = node.declare_parameter<double>("mpc_weight_lat_error", 1.0);
  nw.heading_error = node.declare_parameter<double>("mpc_weight_heading_error", 0.0);
  nw.heading_error_squared_vel = node.declare_parameter<double>("mpc_weight_heading_error_squared_vel", 0.3);
  nw.steering_input = node.declare_parameter<double>("mpc_weight_steering_input", 1.0);
  nw.steering_input_squared_vel = node.declare_parameter<double>("mpc_weight_steering_input_squared_vel", 0.25);
  nw.lat_jerk = node.declare_parameter<double>("mpc_weight_lat_jerk", 0.1);
  nw.steer_rate = node.declare_parameter<double>("mpc_weight_steer_rate", 0.0);
  nw.steer_acc = node.declare_parameter<double>("mpc_weight_steer_acc", 0.000001);
  nw.terminal_lat_error = node.declare_parameter<double>("mpc_weight_terminal_lat_error", 1.0);
  nw.terminal_heading_error = node.declare_parameter<double>("mpc_weight_terminal_heading_error", 0.1);

  auto & lcw = m_mpc->m_param.low_curvature_weight;
  lcw.lat_error = node.declare_parameter<double>("mpc_low_curvature_weight_lat_error", 0.1);
  lcw.heading_error = node.declare_parameter<double>("mpc_low_curvature_weight_heading_error", 0.0);
  lcw.heading_error_squared_vel = node.declare_parameter<double>("mpc_low_curvature_weight_heading_error_squared_vel", 0.3);
  lcw.steering_input = node.declare_parameter<double>("mpc_low_curvature_weight_steering_input", 1.0);
  lcw.steering_input_squared_vel = node.declare_parameter<double>("mpc_low_curvature_weight_steering_input_squared_vel", 0.25);
  lcw.lat_jerk = node.declare_parameter<double>("mpc_low_curvature_weight_lat_jerk", 0.0);
  lcw.steer_rate = node.declare_parameter<double>("mpc_low_curvature_weight_steer_rate", 0.0);
  lcw.steer_acc = node.declare_parameter<double>("mpc_low_curvature_weight_steer_acc", 0.000001);
  m_mpc->m_param.low_curvature_thresh_curvature = node.declare_parameter<double>("mpc_low_curvature_thresh_curvature", 0.0);

  m_mpc->m_param.zero_ff_steer_deg = node.declare_parameter<double>("mpc_zero_ff_steer_deg", 0.5);
  m_mpc->m_param.acceleration_limit = node.declare_parameter<double>("mpc_acceleration_limit", 2.0);
  m_mpc->m_param.velocity_time_constant = node.declare_parameter<double>("mpc_velocity_time_constant", 0.3);
  m_mpc->m_param.min_prediction_length = node.declare_parameter<double>("mpc_min_prediction_length", 5.0);
}

bool MpcLateralController::isTrajectoryShapeChanged() const
{
  // TODO(Horibe): update implementation to check trajectory shape around ego vehicle.
  // Now temporally check the goal position.
  for (const auto & trajectory : m_trajectory_buffer) {
    const auto change_distance = autoware::universe_utils::calcDistance2d(
      trajectory.points.back().pose, m_current_trajectory.points.back().pose);
    if (change_distance > m_new_traj_end_dist) {
      return true;
    }
  }
  return false;
}

bool MpcLateralController::isValidTrajectory(const TrajectoryMsg & traj) const
{
  for (const auto & p : traj.points) {
    if (
      !std::isfinite(p.pose.position.x) || !std::isfinite(p.pose.position.y) ||
      !std::isfinite(p.pose.orientation.w) || !std::isfinite(p.pose.orientation.x) ||
      !std::isfinite(p.pose.orientation.y) || !std::isfinite(p.pose.orientation.z) ||
      !std::isfinite(p.longitudinal_velocity_mps) || !std::isfinite(p.lateral_velocity_mps) ||
      !std::isfinite(p.heading_rate_rps) || !std::isfinite(p.front_wheel_angle_rad) ||
      !std::isfinite(p.rear_wheel_angle_rad)) {
      return false;
    }
  }
  return true;
}

}  // namespace autoware::motion::control::mpc_lateral_controller
