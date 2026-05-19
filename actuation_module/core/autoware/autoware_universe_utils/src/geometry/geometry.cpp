// Copyright 2023-2024 TIER IV, Inc.
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
#include "autoware/universe_utils/geometry/geometry.hpp"

// Common
#include "common/logger/logger.hpp"
using namespace common::logger;

// Libs
#include <string>

#include <Eigen/Geometry>

namespace autoware::universe_utils
{
// TODO: replace with eigen
Vector3Msg getRPY(const QuaternionMsg & quat)
{
  Vector3Msg rpy;
  // tf2::Quaternion q(quat.x, quat.y, quat.z, quat.w);
  // tf2::Matrix3x3(q).getRPY(rpy.x, rpy.y, rpy.z);
  // TODO: validate this
  
  // Convert QuaternionMsg to Eigen::Quaterniond
  Eigen::Quaterniond eigen_quat(quat.w, quat.x, quat.y, quat.z);
  
  // Convert quaternion to rotation matrix
  Eigen::Matrix3d rotation_matrix = eigen_quat.toRotationMatrix();
  
  // Extract roll, pitch, yaw from rotation matrix
  // Using Eigen's eulerAngles with the XYZ convention
  // Note: Eigen uses different order than ROS, so we extract separately
  rpy.x = atan2(rotation_matrix(2,1), rotation_matrix(2,2));  // roll
  rpy.y = asin(-rotation_matrix(2,0));                        // pitch
  rpy.z = atan2(rotation_matrix(1,0), rotation_matrix(0,0));  // yaw
  
  return rpy;
}
Vector3Msg getRPY(const PoseMsg & pose)
{
  return getRPY(pose.orientation);
}
Vector3Msg getRPY(const PoseStampedMsg & pose)
{
  return getRPY(pose.pose);
}
Vector3Msg getRPY(const PoseWithCovarianceStampedMsg & pose)
{
  return getRPY(pose.pose.pose);
}

QuaternionMsg createQuaternion(
  const double x, const double y, const double z, const double w)
{
  QuaternionMsg q;
  q.x = x;
  q.y = y;
  q.z = z;
  q.w = w;
  return q;
}

QuaternionMsg createQuaternionFromRPY(
  const double roll, const double pitch, const double yaw)
{
  // Calculate rotation matrix elements
  const double cr = std::cos(roll * 0.5);
  const double sr = std::sin(roll * 0.5);
  const double cp = std::cos(pitch * 0.5);
  const double sp = std::sin(pitch * 0.5);
  const double cy = std::cos(yaw * 0.5);
  const double sy = std::sin(yaw * 0.5);

  // Convert to quaternion  the rotation matrix to quaternion formulas
  QuaternionMsg q;
  q.w = cr * cp * cy + sr * sp * sy;
  q.x = sr * cp * cy - cr * sp * sy;
  q.y = cr * sp * cy + sr * cp * sy;
  q.z = cr * cp * sy - sr * sp * cy;

  return q;
}

QuaternionMsg createQuaternionFromYaw(const double yaw)
{
  // Calculate half angles
  const double half_yaw = yaw * 0.5;
  const double cy = std::cos(half_yaw);
  const double sy = std::sin(half_yaw);

  // When roll and pitch are 0, the quaternion simplifies to:
  QuaternionMsg q;
  q.w = cy;  // cos(0/2)cos(0/2)cos(yaw/2) = cos(yaw/2)
  q.x = 0;   // sin(0/2)cos(0/2)cos(yaw/2) = 0
  q.y = 0;   // cos(0/2)sin(0/2)cos(yaw/2) = 0
  q.z = sy;  // cos(0/2)cos(0/2)sin(yaw/2) = sin(yaw/2)

  return q;
}

Vector3Msg createTranslation(const double x, const double y, const double z)
{
  Vector3Msg v;
  v.x = x;
  v.y = y;
  v.z = z;
  return v;
}

double calcElevationAngle(
  const PointMsg & p_from, const PointMsg & p_to)
{
  const double dz = p_to.z - p_from.z;
  const double dist_2d = calcDistance2d(p_from, p_to);
  return std::atan2(dz, dist_2d);
}

double calcAzimuthAngle(
  const PointMsg & p_from, const PointMsg & p_to)
{
  const double dx = p_to.x - p_from.x;
  const double dy = p_to.y - p_from.y;
  return std::atan2(dy, dx);
}

double getYaw(const QuaternionMsg & q)
{
  // Convert quaternion to Euler angles
  // yaw (z-axis rotation)
  double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
  double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
  return std::atan2(siny_cosp, cosy_cosp);
}

double calcCurvature(
  const PointMsg & p1, const PointMsg & p2,
  const PointMsg & p3)
{
  // Calculation details are described in the following page
  // https://en.wikipedia.org/wiki/Menger_curvature
  const double denominator =
    calcDistance2d(p1, p2) * calcDistance2d(p2, p3) * calcDistance2d(p3, p1);
  if (std::fabs(denominator) < 1e-10) {
    log_error("Geometry: points are too close for curvature calculation.");
    return 0.0;  // Return 0 curvature as fallback when points are too close
  }
  return 2.0 * ((p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x)) / denominator;
}

std::optional<PointMsg> intersect(
  const PointMsg & p1, const PointMsg & p2,
  const PointMsg & p3, const PointMsg & p4)
{
  // calculate intersection point
  const double det = (p1.x - p2.x) * (p4.y - p3.y) - (p4.x - p3.x) * (p1.y - p2.y);
  if (det == 0.0) {
    return std::nullopt;
  }

  const double t = ((p4.y - p3.y) * (p4.x - p2.x) + (p3.x - p4.x) * (p4.y - p2.y)) / det;
  const double s = ((p2.y - p1.y) * (p4.x - p2.x) + (p1.x - p2.x) * (p4.y - p2.y)) / det;
  if (t < 0 || 1 < t || s < 0 || 1 < s) {
    return std::nullopt;
  }

  PointMsg intersect_point;
  intersect_point.x = t * p1.x + (1.0 - t) * p2.x;
  intersect_point.y = t * p1.y + (1.0 - t) * p2.y;
  intersect_point.z = t * p1.z + (1.0 - t) * p2.z;
  return intersect_point;
}

/**
 * @brief Calculate offset pose. The offset values are defined in the local coordinate of the input
 * pose.
 */
PoseMsg calcOffsetPose(
  const PoseMsg & p, const double x, const double y, const double z,
  const double yaw)
{
  PoseMsg pose;
  TransformMsg transform;
  transform.translation = createTranslation(x, y, z);
  transform.rotation = createQuaternionFromYaw(yaw);
  // tf2::Transform tf_pose;
  // tf2::Transform tf_offset;
  // tf2::fromMsg(transform, tf_offset);
  // tf2::fromMsg(p, tf_pose);
  // tf2::toMsg(tf_pose * tf_offset, pose);

  // TODO: validate this
  // Convert pose to Eigen
  Eigen::Vector3d pose_translation(p.position.x, p.position.y, p.position.z);
  Eigen::Quaterniond pose_rotation(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
  
  // Convert transform to Eigen
  Eigen::Vector3d offset_translation(transform.translation.x, transform.translation.y, transform.translation.z);
  Eigen::Quaterniond offset_rotation(transform.rotation.w, transform.rotation.x, transform.rotation.y, transform.rotation.z);
  
  // Apply transformation (equivalent to tf_pose * tf_offset)
  // First rotate the offset translation by the pose rotation
  Eigen::Vector3d rotated_offset = pose_rotation * offset_translation;
  
  // Then combine translations
  Eigen::Vector3d result_translation = pose_translation + rotated_offset;
  
  // Combine rotations (quaternion multiplication)
  Eigen::Quaterniond result_rotation = pose_rotation * offset_rotation;
  result_rotation.normalize();  // Normalize to ensure unit quaternion
  
  // Set the result pose
  pose.position.x = result_translation.x();
  pose.position.y = result_translation.y();
  pose.position.z = result_translation.z();
  
  pose.orientation.x = result_rotation.x();
  pose.orientation.y = result_rotation.y();
  pose.orientation.z = result_rotation.z();
  pose.orientation.w = result_rotation.w();

  return pose;
}

Vector3Msg lerp(
  const Vector3Msg & src_vec, 
  const Vector3Msg & dst_vec, 
  const double ratio)
{
  Vector3Msg result;
  // Linear interpolation formula: result = src + ratio * (dst - src)
  result.x = src_vec.x + ratio * (dst_vec.x - src_vec.x);
  result.y = src_vec.y + ratio * (dst_vec.y - src_vec.y);
  result.z = src_vec.z + ratio * (dst_vec.z - src_vec.z);
  return result;
}

// QuaternionMsg operator+(QuaternionMsg a, QuaternionMsg b) noexcept
// {
//   // tf2::Quaternion quat_a;
//   // tf2::Quaternion quat_b;
//   // tf2::fromMsg(a, quat_a);
//   // tf2::fromMsg(b, quat_b);
//   // return tf2::toMsg(quat_a + quat_b);
// }

// QuaternionMsg operator-(QuaternionMsg a) noexcept
// {
//   // tf2::Quaternion quat_a;
//   // tf2::fromMsg(a, quat_a);
//   // return tf2::toMsg(quat_a * -1.0);
// }

// QuaternionMsg operator-(QuaternionMsg a, QuaternionMsg b) noexcept
// {
//   // tf2::Quaternion quat_a;
//   // tf2::Quaternion quat_b;
//   // tf2::fromMsg(a, quat_a);
//   // tf2::fromMsg(b, quat_b);
//   // return tf2::toMsg(quat_a * quat_b.inverse());
// }
}  // namespace autoware::universe_utils