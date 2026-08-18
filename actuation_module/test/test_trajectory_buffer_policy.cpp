// test_trajectory_buffer_policy.cpp
//
// Host unit test for MPC's trajectory-buffer receipt-history policy
// (trajectory_buffer_policy.hpp) — the decisions that bound
// m_trajectory_buffer's growth in mpc_lateral_controller.cpp. Both growth
// defects in that buffer's history were decision bugs that killed the
// controller on the board through heap exhaustion, and the first fix (the
// duplicate-stamp skip) landed with no test; this file is the regression
// coverage for both:
//  - duplicate stamps: the sticky current trajectory re-entering every
//    control cycle after the peer stops publishing (the task-36 leak);
//  - negative stamp differences: a peer whose clock steps backwards, which
//    the unguarded trim exit read as "inside the window" forever.
//
// Pure-logic test: the policy takes explicit stamps and durations, so no
// clock, no DDS, no FreeRTOS, and no controller construction is needed —
// same convention as test_input_staleness_gate.cpp next to this file. The
// scenario sections drive a plain deque of stamps through the SAME policy
// calls, in the SAME shape (skip-or-push, then pop-while), as the
// controller's own loop.
//
// Like its siblings, this relies on assert(); a build that defines NDEBUG
// would silently pass without testing anything, so fail loudly instead.
#ifdef NDEBUG
#error "test_trajectory_buffer_policy.cpp relies on assert(); build it without NDEBUG"
#endif

#include <assert.h>

#include <cstdint>
#include <deque>

#include "autoware/mpc_lateral_controller/trajectory_buffer_policy.hpp"

namespace policy =
  autoware::motion::control::mpc_lateral_controller::trajectory_buffer_policy;

namespace
{

struct Stamp
{
  int32_t sec;
  uint32_t nanosec;
  double toSec() const { return static_cast<double>(sec) + static_cast<double>(nanosec) * 1e-9; }
};

// One receipt event, run exactly the way mpc_lateral_controller.cpp's
// setTrajectory() runs it: skip a duplicate of the newest stamp, otherwise
// push and pop-from-the-front while the policy says the window is invalid.
void receive(std::deque<Stamp> & buf, Stamp stamp, double duration_sec)
{
  const bool already_buffered = !buf.empty() &&
    policy::isDuplicateStamp(buf.back().sec, buf.back().nanosec, stamp.sec, stamp.nanosec);
  if (already_buffered) {
    return;
  }
  buf.push_back(stamp);
  while (policy::shouldPopFront(buf.back().toSec() - buf.front().toSec(), duration_sec)) {
    buf.pop_front();
  }
}

}  // namespace

int main()
{
  constexpr double kDuration = 1.0;  // controller default new_traj_duration_time

  // 1. Baseline: advancing stamps at 10 Hz trim to the duration window —
  //    the buffer stays bounded at (window / period) + 1 samples.
  {
    std::deque<Stamp> buf;
    for (int i = 0; i < 1000; i++) {
      receive(buf, Stamp{i / 10, static_cast<uint32_t>(i % 10) * 100000000u}, kDuration);
      assert(!buf.empty());
      assert(buf.size() <= 11u);  // 1.0 s window / 0.1 s period + the newest
    }
    assert(buf.size() == 10u);  // steady state: ages 0.0 .. 0.9 inclusive
  }

  // 2. Duplicate stamps (the task-36 board leak): after arrivals stop, the
  //    sticky sample re-enters every cycle with a frozen stamp and must
  //    never grow the buffer again.
  {
    std::deque<Stamp> buf;
    receive(buf, Stamp{100, 0}, kDuration);
    receive(buf, Stamp{100, 100000000u}, kDuration);
    const auto size_at_freeze = buf.size();
    for (int cycle = 0; cycle < 10000; cycle++) {
      receive(buf, Stamp{100, 100000000u}, kDuration);  // same stamp, every cycle
      assert(buf.size() == size_at_freeze);
    }
  }

  // 3. Duplicate detection is exact on (sec, nanosec): a 1 ns difference is
  //    a distinct sample and IS pushed.
  {
    std::deque<Stamp> buf;
    receive(buf, Stamp{100, 0}, kDuration);
    receive(buf, Stamp{100, 1}, kDuration);
    assert(buf.size() == 2u);
  }

  // 4. Negative stamp difference (non-monotonic peer clock): stamps
  //    stepping backwards must not grow the buffer without bound. The
  //    unguarded trim exit (`time_diff < duration_time`) accepted every
  //    negative difference as "inside the window", so each smaller-stamped
  //    sample was pushed and none ever popped — one deep points-vector
  //    copy per cycle, the same fatal heap-exhaustion signature as the
  //    duplicate-stamp leak. With the guard, a backwards step flushes the
  //    now-meaningless older history and the buffer re-fills from the new
  //    timebase.
  {
    std::deque<Stamp> buf;
    // Healthy history first.
    for (int i = 0; i < 8; i++) {
      receive(buf, Stamp{200, static_cast<uint32_t>(i) * 100000000u}, kDuration);
    }
    assert(buf.size() == 8u);
    // Peer clock steps back ~100 s, then keeps ticking down each cycle —
    // the worst shape: every sample distinct, every difference negative.
    for (int i = 0; i < 10000; i++) {
      receive(buf, Stamp{100 - i / 10, static_cast<uint32_t>(9 - i % 10) * 100000000u},
        kDuration);
      // Bounded: never more than the steady-state window's worth.
      assert(buf.size() <= 11u);
    }
  }

  // 5. Single backwards step, then monotonic again: the buffer recovers to
  //    normal windowed behavior on the new timebase.
  {
    std::deque<Stamp> buf;
    for (int i = 0; i < 5; i++) {
      receive(buf, Stamp{300 + i, 0}, kDuration);
    }
    receive(buf, Stamp{250, 0}, kDuration);  // the step back
    assert(buf.size() == 1u);                // stale-timebase history flushed
    for (int i = 1; i < 5; i++) {
      receive(buf, Stamp{250 + i, 0}, kDuration);
    }
    // 1.0 s window over 1 s-spaced stamps: back-front >= duration pops all
    // but the newest each time.
    assert(buf.size() == 1u);
    assert(buf.back().sec == 254);
  }

  // 6. shouldPopFront's exact boundaries: 0 <= diff < duration keeps the
  //    window; diff == duration and any negative diff pop.
  {
    assert(!policy::shouldPopFront(0.0, kDuration));
    assert(!policy::shouldPopFront(0.5, kDuration));
    assert(policy::shouldPopFront(kDuration, kDuration));
    assert(policy::shouldPopFront(-0.000001, kDuration));
    assert(policy::shouldPopFront(-1000.0, kDuration));
  }

  return 0;
}
