// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#include "autoware/trajectory_follower_node/vp_reference.hpp"

#include <cassert>
#include <cmath>
#include <vector>

namespace vr = autoware::motion::control::trajectory_follower_node::vp_reference;

namespace
{
bool near(double a, double b, double tol = 1e-6) {return std::fabs(a - b) <= tol;}

std::vector<vr::Point> run(
  double c, double x_max, vr::Pose2D ego, const std::vector<double> & horizon)
{
  std::vector<vr::Point> out;
  assert(vr::convert(0.0, 0.0, c, x_max, ego, horizon, 0.05, out));
  return out;
}

std::vector<double> ramp(double v0, double step, int n)
{
  std::vector<double> h;
  for (int i = 0; i < n; ++i) {h.push_back(v0 + step * i);}
  return h;
}
}  // namespace

int main()
{
  using vr::Reject;

  // Usable references pass; anything VP marks unusable is ignored, not stopped on.
  assert(vr::referenceReason(true, true, true, 20, 0.05, 30.0) == Reject::NONE);
  assert(vr::referenceReason(false, true, true, 20, 0.05, 30.0) == Reject::INVALID_REFERENCE);
  assert(vr::referenceReason(true, true, false, 20, 0.05, 30.0) == Reject::NO_SOURCE_STAMP);
  assert(vr::referenceReason(true, false, true, 20, 0.05, 30.0) == Reject::NO_PATH);
  assert(vr::referenceReason(true, true, true, 20, 0.05, 0.0) == Reject::NO_PATH);
  assert(vr::referenceReason(true, true, true, 1, 0.05, 30.0) == Reject::NO_HORIZON);
  assert(vr::referenceReason(true, true, true, 20, 0.0, 30.0) == Reject::NO_HORIZON);

  // Straight lane ahead of an ego at the origin: the near-field arcs are kept
  // and the far field is shed to stay within 13 points (the former adapter's
  // exact layout).
  {
    const auto out = run(0.0, 40.0, {0.0, 0.0, 0.0}, std::vector<double>(20, 5.0));
    const double expected[] = {0.0, 0.25, 0.5, 1.0, 2.0, 4.25, 6.25, 8.25, 10.5, 12.5, 14.5,
      16.75, 18.75};
    assert(out.size() == vr::kMaxPoints);
    for (std::size_t i = 0; i < out.size(); ++i) {
      assert(near(out[i].x, expected[i]) && near(out[i].y, 0.0) && near(out[i].yaw, 0.0));
      assert(near(out[i].velocity_mps, 5.0) && near(out[i].acceleration_mps2, 0.0));
    }
  }

  // The path is placed with the ego pose: facing +y, the lane runs along +y.
  {
    const auto out = run(0.0, 20.0, {10.0, -5.0, M_PI / 2}, std::vector<double>(20, 3.0));
    assert(near(out.back().x, 10.0, 1e-9) && near(out.back().y, 8.25, 1e-9));
    assert(near(out.back().yaw, M_PI / 2));
  }

  // A lane 1.5 m to the left does not start at the ego: the ego is prepended.
  {
    const auto out = run(1.5, 20.0, {0.0, 0.0, 0.0}, std::vector<double>(20, 3.0));
    assert(near(out[0].x, 0.0) && near(out[0].y, 0.0));
    assert(near(out[1].y, 1.5));
  }

  // Launch 0 -> 1.425 m/s: reading 1 m ahead holds the schedule end, bounded by it.
  {
    const auto h = ramp(0.0, 0.075, 20);
    const auto out = run(0.0, 20.0, {0.0, 0.0, 0.0}, h);
    assert(near(out.front().velocity_mps, h.back()) && near(out.back().velocity_mps, h.back()));
    for (std::size_t i = 1; i < out.size(); ++i) {
      assert(out[i].time_from_start_s >= out[i - 1].time_from_start_s);
      assert(out[i].velocity_mps <= h.back() + 1e-9);
    }
  }

  // A stop schedule stays an exact stop; a slow creep never reads as one.
  for (const auto & p : run(0.0, 20.0, {0.0, 0.0, 0.0}, std::vector<double>(20, 0.0))) {
    assert(p.velocity_mps == 0.0 && p.acceleration_mps2 == 0.0);
  }
  for (const auto & p : run(0.0, 20.0, {0.0, 0.0, 0.0}, ramp(0.05, 0.015, 20))) {
    assert(p.velocity_mps > 0.0);
  }

  // Braking 8 -> 5 m/s: demand below the current speed, never a zero, decelerating.
  {
    const auto h = ramp(8.0, -0.15, 20);
    const auto out = run(0.0, 20.0, {0.0, 0.0, 0.0}, h);
    assert(out.front().velocity_mps < 8.0 && out.front().velocity_mps > h.back());
    assert(out.front().acceleration_mps2 < -1.0);
    for (const auto & p : out) {assert(p.velocity_mps > 0.0);}
  }

  // Negative schedule samples are clamped to zero, not driven backwards.
  for (const auto & p : run(0.0, 20.0, {0.0, 0.0, 0.0}, std::vector<double>(20, -2.0))) {
    assert(p.velocity_mps == 0.0);
  }

  // Shapes the follower cannot use are rejected.
  {
    std::vector<vr::Point> out;
    assert(!vr::convert(0.0, 0.0, 0.0, 0.0, {0, 0, 0}, std::vector<double>(20, 1.0), 0.05, out));
    assert(!vr::convert(0.0, 0.0, 0.0, 20.0, {0, 0, 0}, {1.0}, 0.05, out));
    assert(!vr::convert(0.0, 0.0, 0.0, 20.0, {0, 0, 0}, std::vector<double>(20, 1.0), 0.0, out));
  }

  // Ego poses match on the exact simulator-frame stamp only, newest first,
  // and the oldest fall out of the bounded history.
  {
    vr::EgoHistory history;
    vr::Pose2D pose{};
    assert(!history.find(1, 0, pose));
    history.note(1, 50000000u, {1.0, 2.0, 0.3});
    history.note(1, 100000000u, {4.0, 5.0, 0.6});
    assert(history.find(1, 50000000u, pose) && near(pose.x, 1.0) && near(pose.yaw, 0.3));
    assert(!history.find(1, 50000001u, pose));
    history.note(1, 50000000u, {7.0, 8.0, 0.9});  // the same frame again: newest wins
    assert(history.find(1, 50000000u, pose) && near(pose.x, 7.0));
    for (uint32_t i = 0; i < vr::kEgoHistory; ++i) {history.note(2, i, {0.0, 0.0, 0.0});}
    assert(!history.find(1, 100000000u, pose));
    assert(history.find(2, 0, pose) && history.find(2, vr::kEgoHistory - 1, pose));
  }

  assert(near(vr::yawFromQuaternion(0.0, 0.0, std::sin(0.4), std::cos(0.4)), 0.8));
  return 0;
}
