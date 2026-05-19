// Copyright 2023 The Autoware Foundation
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

#ifndef AUTOWARE__MPC_LATERAL_CONTROLLER__STEERING_PREDICTOR_HPP_
#define AUTOWARE__MPC_LATERAL_CONTROLLER__STEERING_PREDICTOR_HPP_

#include <memory>
#include <vector>
#include "common/clock/clock.hpp"
#include "autoware/autoware_msgs/messages.hpp"

namespace autoware::motion::control::mpc_lateral_controller
{

class SteeringPredictor
{
public:
  SteeringPredictor(const double steer_tau, const double steer_delay);
  ~SteeringPredictor() = default;

  /**
   * @brief Calculate the predicted steering based on the given vehicle model.
   * @return The predicted steering angle.
   */
  double calcSteerPrediction();

  /**
   * @brief Store the steering command in the buffer.
   * @param steer The steering command to be stored.
   */
  void storeSteerCmd(const double steer);

private:
  // The previously predicted steering value.
  double m_steer_prediction_prev = 0.0;

  // Previous computation time.
  double m_time_prev = Clock::now();

  // time constant for the steering dynamics
  double m_steer_tau;

  // time delay for the steering dynamics
  double m_input_delay;

  // Buffer of sent control commands.
  std::vector<LateralMsg> m_ctrl_cmd_vec;

  /**
   * @brief Get the sum of all steering commands over the given time range.
   * @param t_start The start time of the range.
   * @param t_end The end time of the range.
   * @param time_constant The time constant for the sum calculation.
   * @return The sum of the steering commands.
   */
  double getSteerCmdSum(
    const double t_start, const double t_end, const double time_constant) const;

  /**
   * @brief Set previously calculated steering
   */
  void setPrevResult(const double & steering);
};

}  // namespace autoware::motion::control::mpc_lateral_controller

#endif  // AUTOWARE__MPC_LATERAL_CONTROLLER__STEERING_PREDICTOR_HPP_
