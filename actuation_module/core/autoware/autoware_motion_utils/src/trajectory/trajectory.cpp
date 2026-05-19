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

#include "autoware/motion_utils/trajectory/trajectory.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace autoware::motion_utils
{

// validateNonEmpty
template void validateNonEmpty<TrajectoryPointSeq>(
  const TrajectoryPointSeq &);

// findNearestIndex
template size_t findNearestIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PointMsg & point);

// findNearestIndex
template std::optional<size_t>
findNearestIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PoseMsg & pose, const double max_dist, const double max_yaw);

// isDrivingForward
template std::optional<bool>
isDrivingForward<TrajectoryPointSeq>(
  const TrajectoryPointSeq &);

// removeOverlapPoints
template TrajectoryPointSeq
removeOverlapPoints<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points, const size_t start_idx);

// calcLongitudinalOffsetToSegment
template double
calcLongitudinalOffsetToSegment<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points, const size_t seg_idx,
  const PointMsg & p_target, const bool throw_exception);

// calcCurvature
template std::vector<double>
calcCurvature<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points);

// findFirstNearestIndexWithSoftConstraints
template size_t
findFirstNearestIndexWithSoftConstraints<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PoseMsg & pose, const double dist_threshold, const double yaw_threshold);

// findFirstNearestSegmentIndexWithSoftConstraints
template size_t findFirstNearestSegmentIndexWithSoftConstraints<
  TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PoseMsg & pose, const double dist_threshold, const double yaw_threshold);

// searchZeroVelocityIndex
template std::optional<size_t>
searchZeroVelocityIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points_with_twist,
  const size_t src_idx, const size_t dst_idx);
template std::optional<size_t>
searchZeroVelocityIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points_with_twist,
  const size_t src_idx);
template std::optional<size_t>
searchZeroVelocityIndex<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points_with_twist);
  
// calcSignedArcLength
template double calcSignedArcLength<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points, const size_t src_idx,
  const size_t dst_idx);
template double calcSignedArcLength<TrajectoryPointSeq>(
  const TrajectoryPointSeq & points,
  const PointMsg & src_point, const size_t src_seg_idx,
  const PointMsg & dst_point, const size_t dst_seg_idx);

}  // namespace autoware::motion_utils
