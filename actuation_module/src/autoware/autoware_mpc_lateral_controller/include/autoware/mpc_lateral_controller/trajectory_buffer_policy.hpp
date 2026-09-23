// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__MPC_LATERAL_CONTROLLER__TRAJECTORY_BUFFER_POLICY_HPP_
#define AUTOWARE__MPC_LATERAL_CONTROLLER__TRAJECTORY_BUFFER_POLICY_HPP_

#include <cstdint>

namespace autoware::motion::control::mpc_lateral_controller
{

/// Pure decision logic for MPC's trajectory receipt-history buffer
/// (m_trajectory_buffer in mpc_lateral_controller.cpp, consumed only by
/// isTrajectoryShapeChanged()). Split out of the .cpp so the exact
/// decisions that bound the buffer's growth are host-testable
/// (test/test_trajectory_buffer_policy.cpp) without constructing the
/// controller — the same OS-free-core pattern as rpmsg_netif_core.{c,h}.
/// Both growth defects in this buffer's history were decision bugs of
/// exactly this shape, found on the board as heap exhaustion
/// (vApplicationMallocFailedHook) killing the controller:
///  - re-pushing the sticky current trajectory with a frozen (duplicate)
///    stamp, never popped because back - front stayed 0 (task-36 leak,
///    fixed by the duplicate-stamp skip below);
///  - any non-monotonic stamp step (a peer whose clock steps backwards),
///    never popped because back - front went negative and the trim loop's
///    plain `time_diff < duration_time` read that as "inside the window"
///    (guarded in shouldPopFront below).
namespace trajectory_buffer_policy
{

/// True when the just-received sample carries exactly the stamp of the
/// newest buffered one, in which case it must NOT be pushed: the buffer is
/// a receipt-history window, and a re-seen stamp adds no history — only an
/// unbounded deep copy of the points vector per control cycle once the
/// peer stops publishing and the same sticky sample returns forever.
/// Exact integer comparison on (sec, nanosec), not on a double conversion,
/// so no rounding can alias two distinct stamps.
inline bool isDuplicateStamp(
  int32_t back_sec, uint32_t back_nanosec, int32_t sample_sec, uint32_t sample_nanosec)
{
  return back_sec == sample_sec && back_nanosec == sample_nanosec;
}

/// Trim decision, evaluated after a push with the buffer's newest-minus-
/// oldest stamp difference: true means the oldest sample must be popped
/// and the decision re-evaluated; false means the window is valid and
/// trimming stops.
///
/// The window is valid only when 0 <= back - front < duration: a NEGATIVE
/// difference means the history straddles a backwards stamp step and is
/// meaningless as a time window, so it must keep popping (each pop
/// discards the oldest, largest-stamped sample; the loop terminates no
/// later than the buffer reaching the just-pushed sample alone, where the
/// difference is exactly 0). Without that guard a peer whose stamps step
/// backwards makes every trim exit immediately (back - front < 0 <
/// duration) while every new, distinct stamp is still pushed — one deep
/// points-vector copy per cycle, unbounded: the same fatal heap-exhaustion
/// signature as the duplicate-stamp leak, through the branch that had no
/// test.
inline bool shouldPopFront(double back_minus_front_sec, double duration_sec)
{
  if (back_minus_front_sec < 0.0) {
    return true;
  }
  return back_minus_front_sec >= duration_sec;
}

}  // namespace trajectory_buffer_policy
}  // namespace autoware::motion::control::mpc_lateral_controller

#endif  // AUTOWARE__MPC_LATERAL_CONTROLLER__TRAJECTORY_BUFFER_POLICY_HPP_
