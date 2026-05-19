// Copyright 2015-2021 Autoware Foundation
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

#include "autoware_vehicle_info_utils/vehicle_info_utils.hpp"

namespace autoware::vehicle_info_utils
{
VehicleInfoUtils::VehicleInfoUtils(Node & node)
{
  const auto wheel_radius_m = node.declare_parameter<double>("wheel_radius", 0.39);
  const auto wheel_width_m = node.declare_parameter<double>("wheel_width", 0.42);
  const auto wheel_base_m = node.declare_parameter<double>("wheel_base", 2.74);
  const auto wheel_tread_m = node.declare_parameter<double>("wheel_tread", 1.63);
  const auto front_overhang_m = node.declare_parameter<double>("front_overhang", 1.0);
  const auto rear_overhang_m = node.declare_parameter<double>("rear_overhang", 1.03);
  const auto left_overhang_m = node.declare_parameter<double>("left_overhang", 0.1);
  const auto right_overhang_m = node.declare_parameter<double>("right_overhang", 0.1);
  const auto vehicle_height_m = node.declare_parameter<double>("vehicle_height", 2.5);
  const auto max_steer_angle_rad = node.declare_parameter<double>("max_steer_angle", 0.70);

  vehicle_info_ = createVehicleInfo(
    wheel_radius_m, wheel_width_m, wheel_base_m, wheel_tread_m, front_overhang_m, rear_overhang_m,
    left_overhang_m, right_overhang_m, vehicle_height_m, max_steer_angle_rad);
}

VehicleInfo VehicleInfoUtils::getVehicleInfo() const
{
  return vehicle_info_;
}
}  // namespace autoware::vehicle_info_utils
