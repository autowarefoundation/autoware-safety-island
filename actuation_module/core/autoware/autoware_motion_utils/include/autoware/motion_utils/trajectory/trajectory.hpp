// Copyright 2022 TIER IV, Inc.
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

#ifndef AUTOWARE__MOTION_UTILS__TRAJECTORY__TRAJECTORY_HPP_
#define AUTOWARE__MOTION_UTILS__TRAJECTORY__TRAJECTORY_HPP_

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <numeric>
#include <cmath>

#include <Eigen/Geometry>

#include "autoware/universe_utils/geometry/geometry.hpp"
#include "autoware/universe_utils/geometry/pose_deviation.hpp"
#include "autoware/universe_utils/math/constants.hpp"

#include "common/logger/logger.hpp"
#include "autoware/autoware_msgs/messages.hpp"
using namespace common::logger;

using TrajectoryPointSeq = std::vector<TrajectoryPointMsg>;

namespace autoware::motion_utils
{

/**
 * @brief validate if points container is empty or not
 * @param points points of trajectory, path, ...
 */
template <class T>
void validateNonEmpty(const T & points)
{
  if (points.empty()) {
    log_error("[autoware_motion_utils] validateNonEmpty(): Points is empty.");
    throw std::invalid_argument("[autoware_motion_utils] validateNonEmpty(): Points is empty.");
  }
}

extern template void validateNonEmpty<TrajectoryPointSeq>(
  const TrajectoryPointSeq &);

/**
 * @brief find nearest point index through points container for a given point.
 * Finding nearest point is determined by looping through the points container,
 * and calculating the 2D squared distance between each point in the container and the given point.
 * The index of the point with minimum distance and yaw deviation comparing to the given point will
 * be returned.
 * @param points points of trajectory, path, ...
 * @param point given point
 * @return index of nearest point
 */
template <class T>
size_t findNearestIndex(const T & points, const PointMsg & point)
{
  validateNonEmpty(points);

  double min_dist = std::numeric_limits<double>::max();
  size_t min_idx = 0;

  for (size_t i = 0; i < points.size(); ++i) {
    const auto dist = autoware::universe_utils::calcSquaredDistance2d(points.at(i), point);
    if (dist < min_dist) {
      min_dist = dist;
      min_idx = i;
    }
  }
  return min_idx;
}

extern template size_t findNearestIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PointMsg & point);

/**
 * @brief find nearest point index through points container for a given pose.
 * Finding nearest point is determined by looping through the points container,
 * and finding the nearest point to the given pose in terms of squared 2D distance and yaw
 * deviation. The index of the point with minimum distance and yaw deviation comparing to the given
 * pose will be returned.
 * @param points points of trajectory, path, ...
 * @param pose given pose
 * @param max_dist max distance used to get squared distance for finding the nearest point to given
 * pose
 * @param max_yaw max yaw used for finding nearest point to given pose
 * @return index of nearest point (index or none if not found)
 */
template <class T>
std::optional<size_t> findNearestIndex(
  const T & points, const PoseMsg & pose,
  const double max_dist = std::numeric_limits<double>::max(),
  const double max_yaw = std::numeric_limits<double>::max())
{
  try {
    validateNonEmpty(points);
  } catch (const std::exception & e) {
    log_error("Trajectory: Error: %s", e.what());
    return {};
  }

  const double max_squared_dist = max_dist * max_dist;

  double min_squared_dist = std::numeric_limits<double>::max();
  bool is_nearest_found = false;
  size_t min_idx = 0;

  for (size_t i = 0; i < points.size(); ++i) {
    const auto squared_dist = autoware::universe_utils::calcSquaredDistance2d(points.at(i), pose);
    if (squared_dist > max_squared_dist || squared_dist >= min_squared_dist) {
      continue;
    }

    const auto yaw = autoware::universe_utils::calcYawDeviation(
      autoware::universe_utils::getPose(points.at(i)), pose);
    if (std::fabs(yaw) > max_yaw) {
      continue;
    }

    min_squared_dist = squared_dist;
    min_idx = i;
    is_nearest_found = true;
  }

  if (is_nearest_found) {
    return min_idx;
  }
  return std::nullopt;
}


extern template std::optional<size_t>
findNearestIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PoseMsg & pose, const double max_dist = std::numeric_limits<double>::max(),
  const double max_yaw = std::numeric_limits<double>::max());

/**
 * @brief remove overlapping points through points container.
 * Overlapping is determined by calculating the distance between 2 consecutive points.
 * If the distance between them is less than a threshold, they will be considered overlapping.
 * @param points points of trajectory, path, ...
 * @param start_idx index to start the overlap remove calculation from through the points
 * container. Indices before that index will be considered non-overlapping. Default = 0
 * @return points container without overlapping points
 */
template <class T>
T removeOverlapPoints(const T & points, const size_t start_idx = 0)
{
  if (points.size() < start_idx + 1) {
    return points;
  }

  T dst;
  dst.reserve(points.size());

  for (size_t i = 0; i <= start_idx; ++i) {
    dst.push_back(points.at(i));
  }

  constexpr double eps = 1.0E-08;
  for (size_t i = start_idx + 1; i < points.size(); ++i) {
    const auto prev_p = autoware::universe_utils::getPoint(dst.back());
    const auto curr_p = autoware::universe_utils::getPoint(points.at(i));
    if (std::abs(prev_p.x - curr_p.x) < eps && std::abs(prev_p.y - curr_p.y) < eps) {
      continue;
    }
    dst.push_back(points.at(i));
  }

  return dst;
}

extern template TrajectoryPointSeq
removeOverlapPoints<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const size_t start_idx = 0);

/**
 * @brief calculate longitudinal offset (length along trajectory from seg_idx point to nearest point
 * to p_target on trajectory). If seg_idx point is after that nearest point, length is negative.
 * Segment is straight path between two continuous points of trajectory.
 * @param points points of trajectory, path, ...
 * @param seg_idx segment index of point at beginning of length
 * @param p_target target point at end of length
 * @param throw_exception flag to enable/disable exception throwing
 * @return signed length
 */
template <class T>
double calcLongitudinalOffsetToSegment(
  const T & points, const size_t seg_idx, const PointMsg & p_target,
  const bool throw_exception = false)
{
  if (seg_idx >= points.size() - 1) {
    const std::string error_message(
      "[autoware_motion_utils] " + std::string(__func__) +
      ": Failed to calculate longitudinal offset because the given segment index is out of the "
      "points size.");
    log_error("Trajectory: Error: %s", error_message.c_str());
    if (throw_exception) {
      std::exit(1);
    }
    log_error("Trajectory: Return NaN since no_throw option is enabled. The maintainer must check the code.");
    return std::nan("");
  }

  const auto overlap_removed_points = removeOverlapPoints(points, seg_idx);

  if (throw_exception) {
    validateNonEmpty(overlap_removed_points);
  } else {
    try {
      validateNonEmpty(overlap_removed_points);
    } catch (const std::exception & e) {
      log_error("Trajectory: Error: %s", e.what());
      return std::nan("");
    }
  }

  if (seg_idx >= overlap_removed_points.size() - 1) {
    const std::string error_message(
      "[autoware_motion_utils] " + std::string(__func__) +
      ": Longitudinal offset calculation is not supported for the same points.");
    if (throw_exception) {
      std::exit(1);
    }
    log_error("Trajectory: %s Return NaN since no_throw option is enabled. The maintainer must check the code.", error_message.c_str());
    return std::nan("");
  }

  const auto p_front = autoware::universe_utils::getPoint(overlap_removed_points.at(seg_idx));
  const auto p_back = autoware::universe_utils::getPoint(overlap_removed_points.at(seg_idx + 1));

  const Eigen::Vector3d segment_vec{p_back.x - p_front.x, p_back.y - p_front.y, 0};
  const Eigen::Vector3d target_vec{p_target.x - p_front.x, p_target.y - p_front.y, 0};

  return segment_vec.dot(target_vec) / segment_vec.norm();
}

extern template double
calcLongitudinalOffsetToSegment<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points, const size_t seg_idx,
  const PointMsg & p_target, const bool throw_exception = false);

/**
 * @brief calculate curvature through points container.
 * The method used for calculating the curvature is using 3 consecutive points through the points
 * container. Then the curvature is the reciprocal of the radius of the circle that passes through
 * these three points.
 * @details more details here : https://en.wikipedia.org/wiki/Menger_curvature
 * @param points points of trajectory, path, ...
 * @return calculated curvature container through points container
 */
template <class T>
std::vector<double> calcCurvature(const T & points)
{
  std::vector<double> curvature_vec(points.size(), 0.0);
  if (points.size() < 3) {
    return curvature_vec;
  }

  for (size_t i = 1; i < points.size() - 1; ++i) {
    const auto p1 = autoware::universe_utils::getPoint(points.at(i - 1));
    const auto p2 = autoware::universe_utils::getPoint(points.at(i));
    const auto p3 = autoware::universe_utils::getPoint(points.at(i + 1));
    curvature_vec.at(i) = (autoware::universe_utils::calcCurvature(p1, p2, p3));
  }
  curvature_vec.at(0) = curvature_vec.at(1);
  curvature_vec.at(curvature_vec.size() - 1) = curvature_vec.at(curvature_vec.size() - 2);

  return curvature_vec;
}

extern template std::vector<double>
calcCurvature<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points);

/**
 * @brief checks whether a path of trajectory has forward driving direction
 * @param points points of trajectory, path, ...
 * @return (forward / backward) driving (true / false)
 */
template <class T>
std::optional<bool> isDrivingForward(const T & points)
{
  if (points.size() < 2) {
    return std::nullopt;
  }

  // check the first point direction
  const auto & first_pose = autoware::universe_utils::getPose(points.at(0));
  const auto & second_pose = autoware::universe_utils::getPose(points.at(1));

  return autoware::universe_utils::isDrivingForward(first_pose, second_pose);
}

/**
 * @brief find first nearest point index through points container for a given pose with soft
 * distance and yaw constraints. Finding nearest point is determined by looping through the points
 * container, and finding the nearest point to the given pose in terms of squared 2D distance and
 * yaw deviation. The index of the point with minimum distance and yaw deviation comparing to the
 * given pose will be returned.
 * @param points points of trajectory, path, ...
 * @param pose given pose
 * @param dist_threshold distance threshold used for searching for first nearest index to given pose
 * @param yaw_threshold yaw threshold used for searching for first nearest index to given pose
 * @return index of nearest point (index or none if not found)
 */
template <class T>
size_t findFirstNearestIndexWithSoftConstraints(
  const T & points, const PoseMsg & pose,
  const double dist_threshold = std::numeric_limits<double>::max(),
  const double yaw_threshold = std::numeric_limits<double>::max())
{
  validateNonEmpty(points);

  {  // with dist and yaw thresholds
    const double squared_dist_threshold = dist_threshold * dist_threshold;
    double min_squared_dist = std::numeric_limits<double>::max();
    size_t min_idx = 0;
    bool is_within_constraints = false;
    for (size_t i = 0; i < points.size(); ++i) {
      const auto squared_dist =
        autoware::universe_utils::calcSquaredDistance2d(points.at(i), pose.position);
      const auto yaw = autoware::universe_utils::calcYawDeviation(
        autoware::universe_utils::getPose(points.at(i)), pose);

      if (squared_dist_threshold < squared_dist || yaw_threshold < std::abs(yaw)) {
        if (is_within_constraints) {
          break;
        }
        continue;
      }

      if (min_squared_dist <= squared_dist) {
        continue;
      }

      min_squared_dist = squared_dist;
      min_idx = i;
      is_within_constraints = true;
    }

    // nearest index is found
    if (is_within_constraints) {
      return min_idx;
    }
  }

  {  // with dist threshold
    const double squared_dist_threshold = dist_threshold * dist_threshold;
    double min_squared_dist = std::numeric_limits<double>::max();
    size_t min_idx = 0;
    bool is_within_constraints = false;
    for (size_t i = 0; i < points.size(); ++i) {
      const auto squared_dist =
        autoware::universe_utils::calcSquaredDistance2d(points.at(i), pose.position);

      if (squared_dist_threshold < squared_dist) {
        if (is_within_constraints) {
          break;
        }
        continue;
      }

      if (min_squared_dist <= squared_dist) {
        continue;
      }

      min_squared_dist = squared_dist;
      min_idx = i;
      is_within_constraints = true;
    }

    // nearest index is found
    if (is_within_constraints) {
      return min_idx;
    }
  }

  // without any threshold
  return findNearestIndex(points, pose.position);
}

extern template size_t
findFirstNearestIndexWithSoftConstraints<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PoseMsg & pose,
  const double dist_threshold = std::numeric_limits<double>::max(),
  const double yaw_threshold = std::numeric_limits<double>::max());

/**
 * @brief find nearest segment index to pose with soft constraints
 * Segment is straight path between two continuous points of trajectory
 * When pose is on a trajectory point whose index is nearest_idx, return nearest_idx - 1
 * @param points points of trajectory, path, ..
 * @param pose pose to which to find nearest segment index
 * @param dist_threshold distance threshold used for searching for first nearest index to given pose
 * @param yaw_threshold yaw threshold used for searching for first nearest index to given pose
 * @return nearest index
 */
template <class T>
size_t findFirstNearestSegmentIndexWithSoftConstraints(
  const T & points, const PoseMsg & pose,
  const double dist_threshold = std::numeric_limits<double>::max(),
  const double yaw_threshold = std::numeric_limits<double>::max())
{
  // find first nearest index with soft constraints (not segment index)
  const size_t nearest_idx =
    findFirstNearestIndexWithSoftConstraints(points, pose, dist_threshold, yaw_threshold);

  // calculate segment index
  if (nearest_idx == 0) {
    return 0;
  }
  if (nearest_idx == points.size() - 1) {
    return points.size() - 2;
  }

  const double signed_length = calcLongitudinalOffsetToSegment(points, nearest_idx, pose.position);

  if (signed_length <= 0) {
    return nearest_idx - 1;
  }

  return nearest_idx;
}

extern template size_t findFirstNearestSegmentIndexWithSoftConstraints<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PoseMsg & pose,
  const double dist_threshold = std::numeric_limits<double>::max(),
  const double yaw_threshold = std::numeric_limits<double>::max());

/**
 * @brief search through points container from specified start and end indices about first matching
 * index of a zero longitudinal velocity point.
 * @param points_with_twist points of trajectory, path, ... (with velocity)
 * @param src_idx start index of the search
 * @param dst_idx end index of the search
 * @return first matching index of a zero velocity point inside the points container.
 */
template <class T>
std::optional<size_t> searchZeroVelocityIndex(
  const T & points_with_twist, const size_t src_idx, const size_t dst_idx)
{
  try {
    validateNonEmpty(points_with_twist);
  } catch (const std::exception & e) {
    log_error("%s", e.what());
    return {};
  }

  constexpr double epsilon = 1e-3;
  for (size_t i = src_idx; i < dst_idx; ++i) {
    if (std::fabs(points_with_twist.at(i).longitudinal_velocity_mps) < epsilon) {
      return i;
    }
  }

  return {};
}

extern template std::optional<size_t>
searchZeroVelocityIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points_with_twist,
  const size_t src_idx, const size_t dst_idx);

/**
 * @brief search through points container from specified start index till end of points container
 * about first matching index of a zero longitudinal velocity point.
 * @param points_with_twist points of trajectory, path, ... (with velocity)
 * @param src_idx start index of the search
 * @return first matching index of a zero velocity point inside the points container.
 */
template <class T>
std::optional<size_t> searchZeroVelocityIndex(const T & points_with_twist, const size_t src_idx)
{
  try {
    validateNonEmpty(points_with_twist);
  } catch (const std::exception & e) {
    log_error("%s", e.what());
    return {};
  }

  return searchZeroVelocityIndex(points_with_twist, src_idx, points_with_twist.size());
}

extern template std::optional<size_t>
searchZeroVelocityIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points_with_twist,
  const size_t src_idx);

/**
 * @brief search through points container from its start to end about first matching index of a zero
 * longitudinal velocity point.
 * @param points_with_twist points of trajectory, path, ... (with velocity)
 * @return first matching index of a zero velocity point inside the points container.
 */
template <class T>
std::optional<size_t> searchZeroVelocityIndex(const T & points_with_twist)
{
  return searchZeroVelocityIndex(points_with_twist, 0, points_with_twist.size());
}

extern template std::optional<size_t>
searchZeroVelocityIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points_with_twist);

/**
 * @brief calculate length of 2D distance between two points, specified by start and end points
 * indicies through points container.
 * @param points points of trajectory, path, ...
 * @param src_idx index of start point
 * @param dst_idx index of end point
 * @return length of distance between two points.
 * Length is positive if dst_idx is greater that src_idx (i.e. after it in trajectory, path, ...)
 * and negative otherwise.
 */
template <class T>
double calcSignedArcLength(const T & points, const size_t src_idx, const size_t dst_idx)
{
  try {
    validateNonEmpty(points);
  } catch (const std::exception & e) {
    log_error("%s", e.what());
    return 0.0;
  }

  if (src_idx > dst_idx) {
    return -calcSignedArcLength(points, dst_idx, src_idx);
  }

  double dist_sum = 0.0;
  for (size_t i = src_idx; i < dst_idx; ++i) {
    dist_sum += autoware::universe_utils::calcDistance2d(points.at(i), points.at(i + 1));
  }
  return dist_sum;
}

extern template double
calcSignedArcLength<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points, const size_t src_idx,
  const size_t dst_idx);

/**
 * @brief calculate length of 2D distance between two points, specified by start point and end
 * point with their segment indices in points container
 * @param points points of trajectory, path, ...
 * @param src_point start point
 * @param src_seg_idx index of start point segment
 * @param dst_point end point
 * @param dst_seg_idx index of end point segment
 * @return length of distance between two points.
 * Length is positive if destination point is greater that source point (i.e. after it in
 * trajectory, path, ...) and negative otherwise.
 */
template <class T>
double calcSignedArcLength(
  const T & points, const PointMsg & src_point, const size_t src_seg_idx,
  const PointMsg & dst_point, const size_t dst_seg_idx)
{
  validateNonEmpty(points);

  const double signed_length_on_traj = calcSignedArcLength(points, src_seg_idx, dst_seg_idx);
  const double signed_length_src_offset =
    calcLongitudinalOffsetToSegment(points, src_seg_idx, src_point);
  const double signed_length_dst_offset =
    calcLongitudinalOffsetToSegment(points, dst_seg_idx, dst_point);

  return signed_length_on_traj - signed_length_src_offset + signed_length_dst_offset;
}

extern template double
calcSignedArcLength<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PointMsg & src_point, const size_t src_seg_idx,
  const PointMsg & dst_point, const size_t dst_seg_idx);

}  // namespace autoware::motion_utils

#endif  // AUTOWARE__MOTION_UTILS__TRAJECTORY__TRAJECTORY_HPP_
