// Copyright 2023 TIER IV, Inc.
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

// Autoware
#include "autoware/universe_utils/geometry/pose_deviation.hpp"
#include "autoware/universe_utils/math/normalization.hpp"

// Libs
#define EIGEN_MPL2_ONLY
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace autoware::universe_utils
{

double calcLateralDeviation(
  const PoseMsg & base_pose, const PointMsg & target_point)
{
  const auto & base_point = base_pose.position;

  const auto yaw = getYaw(base_pose.orientation); //TODO: changed from tf2::getYaw to autoware::universe_utils::getYaw
  const Eigen::Vector3d base_unit_vec{std::cos(yaw), std::sin(yaw), 0};

  const auto dx = target_point.x - base_point.x;
  const auto dy = target_point.y - base_point.y;
  const Eigen::Vector3d diff_vec{dx, dy, 0};

  const Eigen::Vector3d cross_vec = base_unit_vec.cross(diff_vec);

  return cross_vec.z();
}

double calcYawDeviation(
  const PoseMsg & base_pose, const PoseMsg & target_pose)
{
  const auto base_yaw = getYaw(base_pose.orientation);
  const auto target_yaw = getYaw(target_pose.orientation);
  return normalizeRadian(target_yaw - base_yaw);
}

}  // namespace autoware::universe_utils
