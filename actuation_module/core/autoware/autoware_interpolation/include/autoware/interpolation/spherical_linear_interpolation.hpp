// Copyright 2022 Tier IV, Inc.
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

#ifndef AUTOWARE__INTERPOLATION__SPHERICAL_LINEAR_INTERPOLATION_HPP_
#define AUTOWARE__INTERPOLATION__SPHERICAL_LINEAR_INTERPOLATION_HPP_

#include <vector>

#include "autoware/interpolation/interpolation_utils.hpp"
#include "autoware/autoware_msgs/messages.hpp"

#include <Eigen/Geometry>

namespace autoware::interpolation
{
QuaternionMsg slerp(
  const QuaternionMsg & src_quat, const QuaternionMsg & dst_quat,
  const double ratio);

std::vector<QuaternionMsg> slerp(
  const std::vector<double> & base_keys,
  const std::vector<QuaternionMsg> & base_values,
  const std::vector<double> & query_keys);

QuaternionMsg lerpOrientation(
  const QuaternionMsg & o_from, const QuaternionMsg & o_to,
  const double ratio);
}  // namespace autoware::interpolation

#endif  // AUTOWARE__INTERPOLATION__SPHERICAL_LINEAR_INTERPOLATION_HPP_
