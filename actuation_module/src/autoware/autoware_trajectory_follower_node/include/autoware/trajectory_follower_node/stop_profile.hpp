// SPDX-License-Identifier: Apache-2.0
#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STOP_PROFILE_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STOP_PROFILE_HPP_

/// Decides, once per control cycle, whether the Safety Island overrides the
/// driving stack and what velocity it commands while it does.
///
/// Arming: nothing trips until the first fresh heartbeat has been seen, so a
/// board that boots without VisionPilot does not brake a car that nobody is
/// driving. Tripping: heartbeat age above the threshold, or the fault latch.
/// Clearing: both healthy again. The ramp is v0 - decel * t, floored at 0,
/// with v0 the ego speed at the trip. Header-only, no DDS or RTOS types, so it
/// is host-tested in test/test_stop_profile.cpp.
class StopProfile
{
public:
  StopProfile(double stale_after_sec, double decel_mps2)
  : stale_after_(stale_after_sec), decel_(decel_mps2) {}

  bool update(double now, bool heartbeat_seen, double heartbeat_age_sec, bool fault, double ego_speed_mps)
  {
    const bool fresh = heartbeat_seen && heartbeat_age_sec <= stale_after_;
    if (!armed_) {
      if (fresh) armed_ = true;
      return false;
    }
    if (active_) {
      if (fresh && !fault) { active_ = false; reason_ = ""; }
      return active_;
    }
    if (fault) { trip(now, ego_speed_mps, "fault"); }
    else if (!fresh) { trip(now, ego_speed_mps, "hb_stale"); }
    return active_;
  }

  double targetVelocity(double now) const
  {
    if (!active_) return 0.0;
    const double v = v0_ - decel_ * (now - trip_time_);
    return v > 0.0 ? v : 0.0;
  }

  bool active() const { return active_; }
  bool armed() const { return armed_; }
  const char * reason() const { return reason_; }
  double decel() const { return decel_; }
  // Ego speed actually used for the ramp -- the ego speed at the trip,
  // clamped to 0 (see trip() below). May differ from the raw speed a caller
  // observed if that raw reading was negative or NaN.
  double v0() const { return v0_; }

private:
  void trip(double now, double v0, const char * why)
  {
    active_ = true; trip_time_ = now; v0_ = v0 > 0.0 ? v0 : 0.0; reason_ = why;
  }
  double stale_after_;
  double decel_;
  bool armed_{false};
  bool active_{false};
  double trip_time_{0.0};
  double v0_{0.0};
  const char * reason_{""};
};

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STOP_PROFILE_HPP_
