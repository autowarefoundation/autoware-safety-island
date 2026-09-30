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
