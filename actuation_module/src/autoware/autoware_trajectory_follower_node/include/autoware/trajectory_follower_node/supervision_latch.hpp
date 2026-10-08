// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace autoware::motion::control::trajectory_follower_node
{
namespace supervision
{

/// SI boot/session identity written into every ApprovedRequest: epoch
/// milliseconds, low 31 bits, never zero. The epoch-millisecond value
/// (~1.8e12) is out of uint32_t range, so the conversion goes through
/// uint64_t: a direct double->uint32_t conversion is undefined and the ARM
/// targets' saturating conversion (AArch32 __aeabi_d2uiz, AArch64 FCVTZU)
/// collapses it to the constant 0xFFFFFFFF, which the mask would turn into
/// 0x7FFFFFFF on every boot. Pure logic (the caller passes the time), so it is
/// host-tested.
inline uint32_t siSessionFromClock(double now_seconds)
{
  const uint64_t ms = static_cast<uint64_t>(now_seconds * 1000.0);
  const uint32_t session = static_cast<uint32_t>(ms & 0x7fffffffu);
  return session == 0 ? 1u : session;
}

/// SI fault latch. Once latched, publication stays SI_STOP until an explicit
/// operator re-enable arrives AND every selected-source check is fresh again
/// (vp_si_control_contract: "a fault never silently changes mode or source;
/// resuming normal driving requires explicit re-enable").
///
/// Pure logic (no clock, no DDS) so it is regression-tested on the host.
struct SupervisionState
{
  bool latched = false;
  uint32_t fault_id = 0;
  std::string reason;

  /// Latch (or re-latch) on a fault; the fault id increments only when the
  /// previous fault had been cleared, so one ongoing fault keeps one id.
  void latch(const std::string & why)
  {
    if (!latched) {
      ++fault_id;
    }
    latched = true;
    reason = why;
  }

  /// One control tick of the latch. Returns true when this tick must publish
  /// SI_STOP. A pending re-enable request clears the latch only when every
  /// selected source is fresh; the request is then consumed and the SAME tick
  /// continues with NORMAL supervision (returns false). Publishing one more
  /// SI_STOP on the clearing tick was a one-cycle artifact (a stop carrying no
  /// active fault). A request that meets a stale source stays pending.
  bool stopThisTick(bool & reenable_requested, bool all_sources_fresh)
  {
    if (!latched) {
      return false;
    }
    if (reenable_requested && all_sources_fresh) {
      latched = false;
      reason.clear();
      reenable_requested = false;
      return false;
    }
    return true;
  }
};

/// Speed target of an SI_STOP: v0 - decel * t from the ego speed at the
/// latch, floored at 0. CARLA's Ackermann controller follows the speed
/// target, so a step to 0 would brake as hard as that controller allows
/// rather than at decel. A negative or NaN v0 gives 0, never a negative
/// target. A v0 above max_v0_mps or infinite (a bad odometry sample) is not
/// a speed the vehicle can have, so the stop starts from 0 instead: an
/// infinite target would never decay and the CAN encoder rejects it. The
/// acceleration demand stays -decel for the whole stop (StopControl), so the
/// ramp reaching 0 is never a brake release.
inline double stopRampVelocity(
  double v0_mps, double decel_mps2, double elapsed_s, double max_v0_mps)
{
  if (!(v0_mps <= max_v0_mps)) {
    return 0.0;
  }
  const double v = v0_mps - decel_mps2 * elapsed_s;
  return v > 0.0 ? v : 0.0;
}

/// Operator re-enable request with a validity window.
///
/// A request that meets a stale source stays pending so one explicit press is
/// enough once every source is fresh, but only for `window_s`: a press nobody
/// acts on must not resume driving minutes later when a source happens to
/// recover. Pure logic (the caller passes the time), so it is host-tested.
struct ReenableRequest
{
  bool pending = false;
  double requested_at = 0.0;

  void request(double now)
  {
    pending = true;
    requested_at = now;
  }

  void clear() {pending = false;}

  /// Drops a pending request older than `window_s`. True when it expired on
  /// this call.
  bool expireIfOld(double now, double window_s)
  {
    if (pending && (now - requested_at) > window_s) {
      pending = false;
      return true;
    }
    return false;
  }
};

/// Arrival-age freshness of one selected source.
///
/// Ages are measured against Clock::now() at callback time, never against
/// message stamps: the selected sources carry CARLA simulated time (and VP
/// produced stamps in its own host clock), so a stamp must never be
/// subtracted from SI's clock (vp_si_control_contract: "A CARLA episode time
/// in seconds must not be subtracted from SI's Unix/system clock").
///
/// Threading: today note() and stale()/ageSec() run on the same thread.
/// Node::main_thread_entry_ polls the DDS subscriptions (callbacks run inside
/// execute_subscriptions()) and then the control timer, both on the one
/// controller thread, and no DDS listener is registered. The arrival time is
/// nevertheless one lock-free std::atomic<double> so the class stays correct
/// if a callback ever moves to another thread (e.g. a DDS listener): a plain
/// 64-bit double store is not single-copy-atomic on ARMv8-R AArch32, and a
/// torn read would fabricate a wild age and latch a spurious SI_STOP that only
/// an explicit re-enable clears. It costs nothing on this target. "Never seen"
/// is the -1.0 sentinel in that same atomic, so ever() and the age cannot
/// disagree and every reader takes exactly one load.
class SourceWatch
{
public:
  void note(double now) {last_arrival_.store(now, std::memory_order_relaxed);}

  bool ever() const {return last_arrival_.load(std::memory_order_relaxed) >= 0.0;}

  double ageSec(double now) const
  {
    const double last = last_arrival_.load(std::memory_order_relaxed);
    return last >= 0.0 ? now - last : -1.0;
  }

  /// True when the source has been seen before but is older than the timeout.
  /// A source that was never seen is NOT stale here: unavailability before
  /// the first sample is "not ready" (the controller's processData path) and
  /// must not fabricate a fault before driving ever started.
  bool stale(double now, double timeout_sec) const
  {
    const double last = last_arrival_.load(std::memory_order_relaxed);
    return last >= 0.0 && (now - last) > timeout_sec;
  }

private:
  std::atomic<double> last_arrival_{-1.0};
  static_assert(
    std::atomic<double>::is_always_lock_free,
    "SourceWatch::last_arrival_ must be a lock-free atomic: a locking "
    "fallback would drag a mutex into DDS callback context on the FreeRTOS "
    "target");
};

/// Actuation envelope an approved command must stay inside.
struct Envelope
{
  double max_steering_rad;
  double max_abs_velocity_mps;
  double max_abs_accel_mps2;
};

/// Why a command is outside the envelope, or nullptr when it is inside. The
/// comparisons are written "not within" so a NaN fails every one of them; a
/// plain "x > max" is false for NaN and would wave it through.
inline const char * envelopeViolation(
  double steering_rad, double velocity_mps, double accel_mps2, const Envelope & limits)
{
  if (!(std::fabs(steering_rad) <= limits.max_steering_rad)) {
    return "steering out of range";
  }
  if (!(std::fabs(velocity_mps) <= limits.max_abs_velocity_mps)) {
    return "speed out of range";
  }
  if (!(std::fabs(accel_mps2) <= limits.max_abs_accel_mps2)) {
    return "accel out of range";
  }
  return nullptr;
}

}  // namespace supervision
}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_
