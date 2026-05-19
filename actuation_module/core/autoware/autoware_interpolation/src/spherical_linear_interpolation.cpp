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

#include <vector>
#include <cstdio>
#include <cmath>

#include "autoware/interpolation/spherical_linear_interpolation.hpp"

namespace autoware::interpolation
{
QuaternionMsg slerp(
  const QuaternionMsg & src_quat, const QuaternionMsg & dst_quat,
  const double ratio)
{
  // tf2::Quaternion src_tf;
  // tf2::Quaternion dst_tf;
  // tf2::fromMsg(src_quat, src_tf);
  // tf2::fromMsg(dst_quat, dst_tf);
  // const auto interpolated_quat = tf2::slerp(src_tf, dst_tf, ratio);
  // return tf2::toMsg(interpolated_quat);
  // TODO: validate slerp implementation
  Eigen::Quaterniond src_q(
    src_quat.w,
    src_quat.x,
    src_quat.y,
    src_quat.z);
    
  Eigen::Quaterniond dst_q(
    dst_quat.w,
    dst_quat.x,
    dst_quat.y,
    dst_quat.z);

  // Perform spherical linear interpolation with Eigen
  Eigen::Quaterniond result_q = src_q.slerp(ratio, dst_q);
    
  // Convert back to ROS message type
  QuaternionMsg result_quat_msg;
  result_quat_msg.x = result_q.x();
  result_quat_msg.y = result_q.y();
  result_quat_msg.z = result_q.z();
  result_quat_msg.w = result_q.w();
  return result_quat_msg;
}

std::vector<QuaternionMsg> slerp(
  const std::vector<double> & base_keys,
  const std::vector<QuaternionMsg> & base_values,
  const std::vector<double> & query_keys)
{
  // throw exception for invalid arguments
  const auto validated_query_keys = validateKeys(base_keys, query_keys);
  validateKeysAndValues(base_keys, base_values);

  // calculate linear interpolation
  std::vector<QuaternionMsg> query_values;
  size_t key_index = 0;
  for (const auto query_key : validated_query_keys) {
    while (base_keys.at(key_index + 1) < query_key) {
      ++key_index;
    }

    const auto src_quat = base_values.at(key_index);
    const auto dst_quat = base_values.at(key_index + 1);
    const double ratio = (query_key - base_keys.at(key_index)) /
                         (base_keys.at(key_index + 1) - base_keys.at(key_index));

    const auto interpolated_quat = slerp(src_quat, dst_quat, ratio);
    query_values.push_back(interpolated_quat);
  }

  return query_values;
}

QuaternionMsg lerpOrientation(
  const QuaternionMsg & o_from, const QuaternionMsg & o_to,
  const double ratio)
{
  // tf2::Quaternion q_from, q_to;
  // tf2::fromMsg(o_from, q_from);
  // tf2::fromMsg(o_to, q_to);
  // const auto q_interpolated = q_from.slerp(q_to, ratio);
  // return tf2::toMsg(q_interpolated);
  // TODO: validate this implementation
  Eigen::Quaterniond src_q(
    o_from.w,
    o_from.x,
    o_from.y,
    o_from.z);
    
  Eigen::Quaterniond dst_q(
    o_to.w,
    o_to.x,
    o_to.y,
    o_to.z);

  Eigen::Quaterniond result_q = src_q.slerp(ratio, dst_q);

  QuaternionMsg result_quat_msg;
  result_quat_msg.x = result_q.x();
  result_quat_msg.y = result_q.y();
  result_quat_msg.z = result_q.z();
  result_quat_msg.w = result_q.w();
  return result_quat_msg;
}
}  // namespace autoware::interpolation
