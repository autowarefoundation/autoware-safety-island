// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__CANDIDATE_IDENTITY_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__CANDIDATE_IDENTITY_HPP_

#include <array>
#include <cstdint>

namespace autoware::motion::control::trajectory_follower_node
{

struct SourceStamp
{
  int32_t sec;
  uint32_t nanosec;
};

enum class CandidateCheck { ACCEPT, DUPLICATE, REGRESSION, INVALID_STAMP };

// Compares source/camera stamps to other *source* stamps, not to SI wall time.
// The latter is a different clock in CARLA; watchdog arrival time is tracked
// separately. Keep state across VP session changes: an old session/message
// must not become fresh just because an adapter republishes it after restart.
class VpCandidateIdentity
{
public:
  CandidateCheck check(uint32_t session, uint64_t cycle, SourceStamp stamp) const
  {
    if (stamp.sec < 0 || stamp.nanosec >= 1000000000u) {
      return CandidateCheck::INVALID_STAMP;
    }
    if (!seen_) {
      return CandidateCheck::ACCEPT;
    }
    for (uint32_t i = 0; i < retired_count_; ++i) {
      if (retired_sessions_[i] == session) {
        return CandidateCheck::REGRESSION;
      }
    }
    if (session == session_ && cycle <= cycle_) {
      if (cycle == cycle_ && equal(stamp, stamp_)) {
        return CandidateCheck::DUPLICATE;
      }
      return CandidateCheck::REGRESSION;
    }
    if (equal(stamp, stamp_)) {
      return CandidateCheck::DUPLICATE;  // repeated camera frame: not fresh
    }
    return later(stamp, stamp_) ? CandidateCheck::ACCEPT : CandidateCheck::REGRESSION;
  }

  void note(uint32_t session, uint64_t cycle, SourceStamp stamp)
  {
    if (seen_ && session != session_) {
      // A retired producer cannot become authoritative again even when an
      // old DDS packet carries a source stamp later than the new producer's.
      // Keep this bounded for the SI's fixed-memory runtime. An old session
      // beyond the window still needs to pass the monotonic source stamp.
      retired_sessions_[retired_next_] = session_;
      retired_next_ = (retired_next_ + 1) % retired_sessions_.size();
      if (retired_count_ < retired_sessions_.size()) {
        ++retired_count_;
      }
    }
    session_ = session;
    cycle_ = cycle;
    stamp_ = stamp;
    seen_ = true;
  }

private:
  static bool equal(SourceStamp a, SourceStamp b)
  {
    return a.sec == b.sec && a.nanosec == b.nanosec;
  }
  static bool later(SourceStamp a, SourceStamp b)
  {
    return a.sec > b.sec || (a.sec == b.sec && a.nanosec > b.nanosec);
  }

  bool seen_ = false;
  uint32_t session_ = 0;
  uint64_t cycle_ = 0;
  SourceStamp stamp_{};
  std::array<uint32_t, 16> retired_sessions_{};
  uint32_t retired_count_ = 0;
  uint32_t retired_next_ = 0;
};

// Native Autoware Trajectory has no producer session/cycle. Its header stamp
// is the strongest source-progress signal available without changing Autoware.
class AutowareTrajectoryIdentity
{
public:
  CandidateCheck check(SourceStamp stamp) const
  {
    if (stamp.sec < 0 || stamp.nanosec >= 1000000000u) {
      return CandidateCheck::INVALID_STAMP;
    }
    if (!seen_ || stamp.sec > stamp_.sec ||
      (stamp.sec == stamp_.sec && stamp.nanosec > stamp_.nanosec))
    {
      return CandidateCheck::ACCEPT;
    }
    return stamp.sec == stamp_.sec && stamp.nanosec == stamp_.nanosec ?
      CandidateCheck::DUPLICATE : CandidateCheck::REGRESSION;
  }

  void note(SourceStamp stamp) {stamp_ = stamp; seen_ = true;}

private:
  bool seen_ = false;
  SourceStamp stamp_{};
};

}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__CANDIDATE_IDENTITY_HPP_
