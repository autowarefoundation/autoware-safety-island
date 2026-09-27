// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__VP_REFERENCE_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__VP_REFERENCE_HPP_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace autoware::motion::control::trajectory_follower_node
{
namespace vp_reference
{

// VisionPilot DrivingReference -> follower trajectory, inside the SI.
//
// VP publishes one reference per camera cycle: the lane polynomial
// y = a x^2 + b x + c in base_link plus its planned speed schedule v(t),
// stamped with that cycle's camera frame. The follower wants a trajectory in
// the odometry frame. The ego pose of the *same* simulator frame places the
// path, so the path and the pose always describe one world instant.
//
// A reference that cannot be used is rejected silently: the SI never
// synthesises a stop from it, because a made-up stop would refresh the
// selected input and hide the fault the source watchdog must see.
//
// Pure logic (no clock, no DDS) so it is regression-tested on the host.

constexpr double kExtentCapM = 25.0;       // trajectory length the follower gets
constexpr std::size_t kMaxPoints = 13;     // follower point budget
constexpr double kPathSpacingM = 1.0;      // polynomial sampling step
constexpr double kResampleStepM = 0.25;    // internal densification
// Points are read this far ahead on the speed schedule. The follower's
// longitudinal target sits decimetres ahead and the vehicle settles at its
// velocity demand, so commanding v(ego) stalls a launch; reading ahead turns
// a rising schedule into a rising demand, bounded by the schedule's end.
constexpr double kSpeedLeadM = 1.0;
// Near-field arcs always represented so the follower's target lands on the
// transcribed ramp/stop instead of a coarse 25 m chord.
constexpr std::array<double, 4> kNearArcsM{0.25, 0.5, 1.0, 2.0};
constexpr std::size_t kMaxHorizon = 200;   // sanity bound on speed_horizon_mps
constexpr std::size_t kEgoHistory = 400;   // ~10 s of 20 Hz odometry

struct Pose2D
{
  double x;
  double y;
  double yaw;
};

struct Point
{
  double x;
  double y;
  double yaw;
  double velocity_mps;
  double time_from_start_s;
  double acceleration_mps2;
};

enum class Reject { NONE, INVALID_REFERENCE, NO_SOURCE_STAMP, NO_PATH, NO_HORIZON };

inline const char * rejectName(Reject r)
{
  switch (r) {
    case Reject::INVALID_REFERENCE: return "invalid-reference";
    case Reject::NO_SOURCE_STAMP: return "no-source-stamp";
    case Reject::NO_PATH: return "no-path";
    case Reject::NO_HORIZON: return "no-horizon";
    default: return "";
  }
}

// Why a reference must be ignored, or NONE when usable.
inline Reject referenceReason(
  bool valid, bool path_valid, bool has_source_stamp, std::size_t horizon_len,
  double horizon_dt_s, double x_max_m)
{
  if (!valid) {return Reject::INVALID_REFERENCE;}
  if (!has_source_stamp) {return Reject::NO_SOURCE_STAMP;}
  if (!path_valid || !(x_max_m > 0.0)) {return Reject::NO_PATH;}
  if (horizon_len < 2 || !(horizon_dt_s > 0.0)) {return Reject::NO_HORIZON;}
  return Reject::NONE;
}

inline double wrapAngle(double yaw) {return std::atan2(std::sin(yaw), std::cos(yaw));}

inline double yawFromQuaternion(double x, double y, double z, double w)
{
  return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
}

// Ego poses keyed by the simulator-frame stamp on their odometry header.
class EgoHistory
{
public:
  void note(int32_t sec, uint32_t nanosec, Pose2D pose)
  {
    entries_[next_] = Entry{sec, nanosec, pose};
    next_ = (next_ + 1) % entries_.size();
    if (count_ < entries_.size()) {++count_;}
  }

  // Exact match only: the path and the pose must come from one frame.
  bool find(int32_t sec, uint32_t nanosec, Pose2D & out) const
  {
    for (std::size_t k = 0; k < count_; ++k) {
      const auto & e = entries_[(next_ + entries_.size() - 1 - k) % entries_.size()];
      if (e.sec == sec && e.nanosec == nanosec) {
        out = e.pose;
        return true;
      }
    }
    return false;
  }

private:
  struct Entry
  {
    int32_t sec;
    uint32_t nanosec;
    Pose2D pose;
  };
  std::array<Entry, kEgoHistory> entries_{};
  std::size_t next_ = 0;
  std::size_t count_ = 0;
};

namespace detail
{

inline std::vector<double> cumulativeArcLengths(const std::vector<Pose2D> & pts)
{
  std::vector<double> s(pts.size(), 0.0);
  for (std::size_t i = 1; i < pts.size(); ++i) {
    s[i] = s[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
  }
  return s;
}

inline std::size_t nearestIndex(const std::vector<double> & s, double target)
{
  const auto pos = static_cast<std::size_t>(
    std::lower_bound(s.begin(), s.end(), target) - s.begin());
  if (pos == 0) {return 0;}
  if (pos >= s.size()) {return s.size() - 1;}
  const double after = s[pos] - target;
  const double before = target - s[pos - 1];
  return after < before ? pos : pos - 1;
}

inline std::vector<std::size_t> selectIndices(
  const std::vector<double> & s, double extent_cap_m, std::size_t max_points)
{
  std::vector<std::size_t> out;
  if (s.empty()) {return out;}
  if (s.size() == 1 || max_points <= 1) {return {0};}
  const double extent = std::min(extent_cap_m, s.back());
  if (extent <= 0.0) {return {0};}
  const std::size_t wanted = std::min(max_points, s.size());
  for (std::size_t step = 0; step < wanted; ++step) {
    const double target = extent * static_cast<double>(step) / static_cast<double>(wanted - 1);
    const std::size_t index = nearestIndex(s, target);
    if (out.empty() || index > out.back()) {out.push_back(index);}
  }
  return out;
}

inline std::vector<Pose2D> resample(std::vector<Pose2D> pts, double step_m)
{
  if (pts.size() < 2) {return pts;}
  std::vector<Pose2D> out{pts.front()};
  for (std::size_t i = 1; i < pts.size(); ++i) {
    const Pose2D & a = pts[i - 1];
    const Pose2D & b = pts[i];
    const double seg = std::hypot(b.x - a.x, b.y - a.y);
    if (seg <= 1e-9) {continue;}
    const double dyaw = wrapAngle(b.yaw - a.yaw);
    const int n = std::max(1, static_cast<int>(std::ceil(seg / step_m)));
    for (int k = 1; k <= n; ++k) {
      const double f = static_cast<double>(k) / n;
      out.push_back({a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, wrapAngle(a.yaw + dyaw * f)});
    }
  }
  return out;
}

// Speed and schedule time at arc s of the transcribed schedule; beyond its
// extent, hold the last speed (VP says nothing there).
inline void sampleSpatial(
  const std::vector<double> & s_knots, const std::vector<double> & v_knots, double s,
  double hold, double dt, double & v_out, double & t_out)
{
  if (s <= 0.0) {
    v_out = std::max(0.0, v_knots.front());
    t_out = 0.0;
    return;
  }
  if (s >= s_knots.back()) {
    const double extra = s - s_knots.back();
    t_out = static_cast<double>(s_knots.size() - 1) * dt + extra / std::max(hold, 0.3);
    v_out = std::max(0.0, hold);
    return;
  }
  std::size_t lo = 0;
  std::size_t hi = s_knots.size() - 1;
  while (lo + 1 < hi) {
    const std::size_t mid = (lo + hi) / 2;
    if (s_knots[mid] < s) {lo = mid;} else {hi = mid;}
  }
  const double seg = s_knots[hi] - s_knots[lo];
  const double f = seg <= 1e-9 ? 0.0 : (s - s_knots[lo]) / seg;
  v_out = std::max(0.0, v_knots[lo] * (1.0 - f) + v_knots[hi] * f);
  t_out = (static_cast<double>(lo) + f) * dt;
}

}  // namespace detail

// The follower trajectory for one reference, or false when it has no usable
// shape. `horizon` is VP's speed schedule at `dt` spacing (>= 2 samples).
inline bool convert(
  double a, double b, double c, double x_max_m, const Pose2D & ego,
  const std::vector<double> & horizon, double dt, std::vector<Point> & out)
{
  out.clear();
  if (!(x_max_m > 0.0) || horizon.size() < 2 || !(dt > 0.0)) {return false;}

  // Sample the polynomial (tangent yaw = atan(2ax + b)) and place it with
  // the same-frame ego pose.
  std::vector<Pose2D> world;
  const double cy = std::cos(ego.yaw);
  const double sy = std::sin(ego.yaw);
  for (double x = 0.0; x <= x_max_m + 1e-9; x += kPathSpacingM) {
    const double y = a * x * x + b * x + c;
    const double yaw = std::atan(2.0 * a * x + b);
    world.push_back({ego.x + x * cy - y * sy, ego.y + x * sy + y * cy, wrapAngle(ego.yaw + yaw)});
  }
  world = detail::resample(std::move(world), kResampleStepM);
  const auto full = detail::cumulativeArcLengths(world);

  auto indices = detail::selectIndices(full, kExtentCapM, kMaxPoints);
  if (indices.empty()) {return false;}
  if (indices.front() != 0) {
    indices.insert(indices.begin(), 0);
    if (indices.size() > kMaxPoints) {indices.resize(kMaxPoints);}
  }
  const double extent = std::min(kExtentCapM, full.back());
  for (double want : kNearArcsM) {
    if (want > 0.0 && want < extent) {indices.push_back(detail::nearestIndex(full, want));}
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  if (indices.size() > kMaxPoints) {indices.resize(kMaxPoints);}  // shed the far field

  std::vector<Pose2D> selected;
  for (auto i : indices) {selected.push_back(world[i]);}
  if (std::hypot(selected.front().x - ego.x, selected.front().y - ego.y) > 0.5) {
    selected.insert(selected.begin(), ego);
    if (selected.size() > kMaxPoints) {selected.resize(kMaxPoints);}
  }
  const auto lengths = detail::cumulativeArcLengths(selected);

  // VP's schedule v(t) becomes v(s) via s(t) = integral of v dt (trapezoid);
  // exact zeros stay zeros, so a VP stop reaches the follower as a stop.
  std::vector<double> h(horizon.size());
  for (std::size_t i = 0; i < h.size(); ++i) {h[i] = std::max(0.0, horizon[i]);}
  std::vector<double> s_knots{0.0};
  for (std::size_t i = 1; i < h.size(); ++i) {
    s_knots.push_back(s_knots.back() + 0.5 * (h[i - 1] + h[i]) * dt);
  }
  const double hold = h.back();
  std::vector<double> speeds(selected.size());
  std::vector<double> times(selected.size());
  for (std::size_t i = 0; i < selected.size(); ++i) {
    detail::sampleSpatial(s_knots, h, lengths[i] + kSpeedLeadM, hold, dt, speeds[i], times[i]);
  }
  const double t_h = std::max(dt * static_cast<double>(h.size() - 1), dt);
  const double fallback = (hold - h.front()) / t_h;

  // Per-point dv/dt so the acceleration agrees with the speed profile.
  const std::size_t n = speeds.size();
  std::vector<double> accels(n, 0.0);
  if (n == 1) {
    accels[0] = fallback;
  } else {
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t i0 = i == 0 ? 0 : i - 1;
      const std::size_t i1 = i == n - 1 ? n - 1 : i + 1;
      const double span = times[i1] - times[i0];
      const double otherwise = i == 0 ? fallback : 0.0;
      accels[i] = span > 1e-6 ? (speeds[i1] - speeds[i0]) / span : otherwise;
    }
  }

  for (std::size_t i = 0; i < n; ++i) {
    out.push_back({selected[i].x, selected[i].y, selected[i].yaw, speeds[i], times[i], accels[i]});
  }
  return true;
}

}  // namespace vp_reference
}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__VP_REFERENCE_HPP_
