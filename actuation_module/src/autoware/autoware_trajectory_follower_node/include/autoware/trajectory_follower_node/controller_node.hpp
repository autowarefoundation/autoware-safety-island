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
#include "autoware/trajectory_follower_node/input_staleness_gate.hpp"
#include "autoware/trajectory_follower_node/visibility_control.hpp"
#include "autoware/universe_utils/system/stop_watch.hpp"
#include "autoware_vehicle_info_utils/vehicle_info_utils.hpp"
#include "common/can/control_command_can_output.hpp"
#include "common/can/control_command_output_mode.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/node/node.hpp"
#include "autoware/autoware_msgs/messages.hpp"

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
  Controller();
  virtual ~Controller() {}

private:
  void reset_data_flags()
  {
    has_accel_ = false;
    has_steering_ = false;
    has_odometry_ = false;
    has_trajectory_ = false;
  }

  double timeout_thr_sec_;

  // Input-staleness threshold for the control_cmd publication gate.
  //
  // Derivation (do not retune without re-deriving): the slowest required
  // input is the trajectory at 10 Hz — expected period 0.1 s. That cadence
  // is this repo's own ground truth, not an assumption: the edge-ECU peer
  // publishes every input at PUBLISH_PERIOD_MS = 100 ("10 Hz — realistic
  // input cadence for the controller", test/dds_pub.cpp), the reader-history
  // sizing comment in common/dds/dds.hpp names the trajectory's "10 Hz
  // producer", and every other required input (odometry, acceleration,
  // steering, operation mode) arrives at the same 10 Hz or faster from the
  // Autoware side, so 0.1 s bounds all expected periods.
  //
  // Multiplier: 5x the slowest expected period. Rationale:
  //  - five consecutive missing samples of the slowest input is unambiguous
  //    input loss, not jitter — the transport's reliability window
  //    (max_blocking_time 30 ms, common/dds/dds.hpp) and observed link
  //    jitter sit far below one 0.1 s period;
  //  - 0.5 s spans at least three 0.15 s control cycles, so the gate's
  //    decision is stable against control-cycle phasing instead of flapping
  //    on a single borderline cycle;
  //  - it equals the existing timeout_thr_sec_ default (0.5 s), this
  //    codebase's only prior definition of "too old to act on" for control
  //    data, keeping one staleness convention rather than introducing a
  //    second number with a subtly different meaning.
  static constexpr double kInputStalenessThresholdSec = 0.5;

  // Gate deciding WHETHER control_cmd may be published (never WHAT is
  // computed); fed with Clock::now() readings — see input_staleness_gate.hpp
  // for the clock-discipline note.
  InputStalenessGate input_staleness_gate_{kInputStalenessThresholdSec};

  std::optional<LongitudinalOutput> longitudinal_output_{std::nullopt};

  std::shared_ptr<trajectory_follower::LongitudinalControllerBase> longitudinal_controller_;
  std::shared_ptr<trajectory_follower::LateralControllerBase> lateral_controller_;

  // Subscribers
  static void callbackSteeringStatus(const SteeringReportMsg* msg, void* arg);
  static void callbackOperationModeState(const OperationModeStateMsg* msg, void* arg);
  static void callbackOdometry(const OdometryMsg* msg, void* arg);
  static void callbackAcceleration(const AccelWithCovarianceStampedMsg* msg, void* arg);
  static void callbackTrajectory(const TrajectoryMsg_Raw* msg, void* arg);

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
  bool has_operation_mode_ = true;

  // Publishers
  std::shared_ptr<Publisher<ControlMsg>> control_cmd_pub_;
  common::can::ControlCommandOutputMode output_mode_{common::can::configured_control_command_output_mode()};
  std::shared_ptr<common::can::ControlCommandCanOutput> can_output_;
  std::shared_ptr<Publisher<Float64StampedMsg>> pub_processing_time_lat_ms_;
  std::shared_ptr<Publisher<Float64StampedMsg>> pub_processing_time_lon_ms_;
  
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
