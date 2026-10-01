// Copyright 2021 Tier IV, Inc. All rights reserved.
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

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__CONTROLLER_NODE_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__CONTROLLER_NODE_HPP_

#include "autoware/trajectory_follower_base/control_horizon.hpp"
#include "autoware/trajectory_follower_base/lateral_controller_base.hpp"
#include "autoware/trajectory_follower_base/longitudinal_controller_base.hpp"
#include "autoware/trajectory_follower_node/supervision.hpp"
#include "autoware/trajectory_follower_node/candidate_identity.hpp"
#include "autoware/trajectory_follower_node/vp_reference.hpp"
#include "autoware/trajectory_follower_node/startup_config.hpp"
#include "autoware/trajectory_follower_node/visibility_control.hpp"
#include "autoware/universe_utils/system/stop_watch.hpp"
#include "autoware_vehicle_info_utils/vehicle_info_utils.hpp"
#include "common/can/control_command_can_output.hpp"
#include "common/can/control_command_output_mode.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/node/node.hpp"
#include "autoware/autoware_msgs/messages.hpp"

// SI_STOP deceleration when the stop_decel_mps2 parameter is unset. A target
// that loads no node parameters (the X5H) selects its value at build time.
#ifndef SI_DEFAULT_STOP_DECEL_MPS2
#define SI_DEFAULT_STOP_DECEL_MPS2 1.5f
#endif

namespace autoware::motion::control
{
using trajectory_follower::LateralOutput;
using trajectory_follower::LongitudinalOutput;

namespace trajectory_follower_node
{

using autoware::universe_utils::StopWatch;

namespace trajectory_follower = ::autoware::motion::control::trajectory_follower;

/// \classController
/// \brief The node class used for generating longitudinal control commands (velocity/acceleration)
class TRAJECTORY_FOLLOWER_PUBLIC Controller : public Node
{
public:
  explicit Controller(const StartupConfig & config);
  virtual ~Controller() {}

private:
  void reset_data_flags()
  {
    has_accel_ = false;
    has_steering_ = false;
    has_odometry_ = false;
    has_trajectory_ = false;
    has_operation_mode_ = false;
  }

  double timeout_thr_sec_;
  const StartupConfig startup_config_;

  std::optional<LongitudinalOutput> longitudinal_output_{std::nullopt};

  std::shared_ptr<trajectory_follower::LongitudinalControllerBase> longitudinal_controller_;
  std::shared_ptr<trajectory_follower::LateralControllerBase> lateral_controller_;

  // Subscribers
  static void callbackSteeringStatus(const SteeringReportMsg* msg, void* arg);
  static void callbackOperationModeState(const OperationModeStateMsg* msg, void* arg);
  static void callbackOdometry(const OdometryMsg* msg, void* arg);
  static void callbackAcceleration(const AccelWithCovarianceStampedMsg* msg, void* arg);
  static void callbackTrajectory(const TrajectoryMsg_Raw* msg, void* arg);
  static void callbackDrivingReference(const DrivingReferenceMsg* msg, void* arg);
  static void callbackDrivingCommand(const DrivingCommandMsg* msg, void* arg);
  static void callbackReenable(const BoolMsg* msg, void* arg);
  static void callbackHeartbeat(const Float64StampedMsg* msg, void* arg);

  // Current Data
  TrajectoryMsg current_trajectory_;
  OdometryMsg current_odometry_;
  SteeringReportMsg current_steering_;
  AccelWithCovarianceStampedMsg current_accel_;
  /*
    mode: 1,
    is_autoware_control_enabled: true,
    is_in_transition: false,
    is_stop_mode_available: true,
    is_autonomous_mode_available: true,
    is_local_mode_available: true,
    is_remote_mode_available: true
  */
  OperationModeStateMsg current_operation_mode_ = {.mode = 1, .is_autoware_control_enabled = true, .is_in_transition = false, .is_stop_mode_available = true, .is_autonomous_mode_available = true, .is_local_mode_available = true, .is_remote_mode_available = true};

  bool has_trajectory_ = false;
  bool has_odometry_ = false;
  bool has_steering_ = false;
  bool has_accel_ = false;
  bool has_operation_mode_ = false;

  // Publishers
  std::shared_ptr<Publisher<ControlMsg>> control_cmd_pub_;
  common::can::ControlCommandOutputMode output_mode_{common::can::configured_control_command_output_mode()};
  std::shared_ptr<common::can::ControlCommandCanOutput> can_output_;
  std::shared_ptr<Publisher<Float64StampedMsg>> pub_processing_time_lat_ms_;
  std::shared_ptr<Publisher<Float64StampedMsg>> pub_processing_time_lon_ms_;
  std::shared_ptr<Publisher<ApprovedRequestMsg>> approved_request_pub_;

  // -------------------------------------------------------------------
  // Supervision (vp_si_control_contract, Safety Island issues #63/#64/#65)
  //
  // SI is the single actuation authority. Every actuator-facing byte on DDS
  // and CAN is produced by publishApprovedRequest() below: the follower's
  // raw output and the VP command are candidate inputs to the gate, never
  // outputs. One supervised output per control cycle; on a fault the state
  // latches to SI_STOP and only an explicit re-enable topic resumes NORMAL.
  //
  supervision::Mode supervision_mode_{supervision::Mode::SI_CONTROL};

  // Per-source arrival-age timeouts (SI system clock; never message stamps —
  // the selected sources carry CARLA sim time, and the VP command carries the
  // VP host clock). Defaults follow the measured proposal in SI #62:
  //  - candidate (trajectory in SI_CONTROL, DrivingCommand in VP_CONTROL):
  //    1.0 s — the VP output maximum gap is 627–682 ms measured on the CARLA
  //    rig, so 0.5 s would false-trip;
  //  - ego feedback (odometry + acceleration at 20 Hz): 0.4 s default,
  //    within the agreed 0.3–0.5 s band (measured max gap 116 ms);
  //  - steering report at 10 Hz: 0.5 s;
  //  - operation mode: no age limit. It is published only when the mode
  //    changes and the deployment retains it (transient_local), so its age
  //    says nothing about liveness: after the last change it would go stale
  //    while everything is healthy. Only "seen at least once" applies; a dead
  //    Autoware is caught by the trajectory watchdog.
  double source_timeout_candidate_ = 1.0;
  double source_timeout_ego_ = 0.4;
  double source_timeout_steering_ = 0.5;
  double source_timeout_opmode_ = std::numeric_limits<double>::infinity();
  //  - VisionPilot heartbeat (/safety_island/vp_heartbeat, one sample per
  //    VP command): 0.5 s. Not stale before its first sample, so a board
  //    that boots without VisionPilot never stops a car nobody drives.
  double source_timeout_heartbeat_ = 0.5;

  // Actuation sanity bounds applied to the VP command in VP_CONTROL: the
  // supervisor never recomputes VP's decision, but it must reject a request
  // it could not actuate (vp_si_control_contract: reject invalid requests
  // instead of forwarding them).
  double max_steering_rad_ = 0.6;
  double max_abs_accel_mps2_ = 6.0;
  double max_abs_velocity_mps_ = 60.0;
  // Steering bound for the follower's output: the vehicle's own steering limit,
  // the value the MPC saturates at (vehicle_info max_steer_angle). It is
  // deliberately not max_steering_rad_: a healthy MPC reaches its saturation,
  // which is above the tighter bound used for VisionPilot's request.
  double follower_max_steer_rad_ = 0.70;

  // SI_STOP acceleration demand (m/s^2, signed negative requested via
  // StopControl): the actuator realizes the explicit stop without deciding.
  float stop_decel_mps2_ = SI_DEFAULT_STOP_DECEL_MPS2;
  // Ego speed and time at the latch: the origin of the SI_STOP speed ramp
  // in stoppedPayload().
  double stop_v0_mps_ = 0.0;
  double stop_t0_ = 0.0;

  supervision::SourceWatch watch_heartbeat_;
  supervision::SourceWatch watch_steering_;
  supervision::SourceWatch watch_odom_;
  supervision::SourceWatch watch_accel_;
  supervision::SourceWatch watch_candidate_;
  supervision::SourceWatch watch_vp_cmd_;
  supervision::SourceWatch watch_opmode_;
  VpCandidateIdentity vp_candidate_identity_;
  // SI_CONTROL + VP: same-frame ego poses that place VP's reference path,
  // and the count of references ignored for each reason (logged throttled).
  vp_reference::EgoHistory ego_history_;
  uint64_t vp_reference_rejects_ = 0;
  AutowareTrajectoryIdentity autoware_trajectory_identity_;

  // Fault latch state (SupervisionState::reason for operator visibility).
  supervision::SupervisionState supervision_{};

  // Operator re-enable request (Bool topic). It stays pending while a source
  // is stale, but expires after reenable_window_s_ (see ReenableRequest).
  supervision::ReenableRequest reenable_;
  double reenable_window_s_ = 5.0;

  // Last approved control payload (for HOLD and for steering hold in SI_STOP).
  ControlMsg last_approved_{};
  bool has_approved_ = false;

  // Output identity: one SI session per process, strictly increasing
  // sequence per published ApprovedRequest; the observer can join every
  // CARLA-applied frame to a decision and fault id from these.
  uint32_t si_session_ = 0;
  uint64_t out_seq_ = 0;

  // VP command supervision state (VP_CONTROL).
  DrivingCommandMsg current_vp_cmd_{};
  bool has_vp_cmd_ = false;
  uint32_t accepted_source_session_ = 0;
  uint64_t accepted_source_cycle_ = 0;

  // First fault of the current control cycle (tentative): the SI_STOP is
  // published by the control timer, so detection→brake is measured from the
  // tick that declared the fault.
  void latchFault(const std::string & why);
  bool supervisorStaleReason(double now, const char ** reason_out, double * age_out) const;
  bool allSourcesFresh(double now) const;
  void publishApprovedRequest(
    supervision::Decision decision, supervision::SelectedSource source,
    const ControlMsg & control);
  void publishSiStop(double now);
  void publishHold();
  // The conservative stopped payload shared by SI_STOP and HOLD: the stop
  // deceleration, the last known steering (from the last approved command,
  // else the vehicle's steering report, else 0), and velocity 0, or under
  // SI_STOP the speed ramp from the ego speed at the latch.
  ControlMsg stoppedPayload(double now) const;
  // True once odometry, acceleration, steering and operation mode have each
  // arrived at least once.
  bool feedbackSeen() const;
  ControlMsg vpToControl(const DrivingCommandMsg & cmd) const;
  
  enum class LateralControllerMode {
    INVALID = 0,
    MPC = 1,
    PURE_PURSUIT = 2,
  };
  enum class LongitudinalControllerMode {
    INVALID = 0,
    PID = 1,
  };

  /**
   * @brief compute control command, and publish periodically
   */
  std::optional<trajectory_follower::InputData> createInputData();

  //
  void callbackTimerControl();

  //
  bool processData();

  //
  bool isTimeOut(const LongitudinalOutput & lon_out, const LateralOutput & lat_out);

  //
  LateralControllerMode getLateralControllerMode(const std::string & algorithm_name) const;

  //
  LongitudinalControllerMode getLongitudinalControllerMode(
    const std::string & algorithm_name) const;

  //
  void publishControlCommand(const trajectory_follower::LongitudinalOutput & lon_out, const trajectory_follower::LateralOutput & lat_out);

  //
  void publishProcessingTime(
    const double t_ms, const std::shared_ptr<Publisher<Float64StampedMsg>> pub);

  //
  StopWatch<std::chrono::milliseconds> stop_watch_;
};

}  // namespace trajectory_follower_node
}  // namespace autoware::motion::control

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__CONTROLLER_NODE_HPP_
