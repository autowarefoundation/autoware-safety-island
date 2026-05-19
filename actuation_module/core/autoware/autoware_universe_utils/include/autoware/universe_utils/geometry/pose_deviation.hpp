// Copyright 2020 Tier IV, Inc.
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

#ifndef AUTOWARE__UNIVERSE_UTILS__GEOMETRY__POSE_DEVIATION_HPP_
#define AUTOWARE__UNIVERSE_UTILS__GEOMETRY__POSE_DEVIATION_HPP_

// Autoware
#include "autoware/universe_utils/geometry/geometry.hpp"

#include "autoware/autoware_msgs/messages.hpp"

namespace autoware::universe_utils
{
struct PoseDeviation
{
  double lateral{0.0};
  double longitudinal{0.0};
  double yaw{0.0};
};

/**
 * @brief Calculate the yaw deviation between two poses
 * @param base_pose The base pose
 * @param target_pose The target pose
 * @return The yaw deviation
 */
double calcYawDeviation(
  const PoseMsg & base_pose, const PoseMsg & target_pose);

/**
 * @brief Calculate the lateral deviation between a pose and a point
 * @param base_pose The base pose
 * @param target_point The target point
 * @return The lateral deviation
 */
double calcLateralDeviation(
  const PoseMsg & base_pose, const PointMsg & target_point);

}  // namespace autoware::universe_utils

#endif  // AUTOWARE__UNIVERSE_UTILS__GEOMETRY__POSE_DEVIATION_HPP_
