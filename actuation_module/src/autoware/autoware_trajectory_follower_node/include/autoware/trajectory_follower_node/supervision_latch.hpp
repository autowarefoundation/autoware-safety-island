// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_

#include <atomic>
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

/// Arrival-age freshness of one selected source.
///
/// Ages are measured against Clock::now() at callback time, never against
/// message stamps: the selected sources carry CARLA simulated time (and VP
/// produced stamps in its own host clock), so a stamp must never be
/// subtracted from SI's clock (vp_si_control_contract: "A CARLA episode time
/// in seconds must not be subtracted from SI's Unix/system clock").
///
/// Threading: note() runs on CycloneDDS callback threads and stale()/ageSec()
/// on the controller thread, so the arrival time crosses threads. A plain
/// 64-bit double store is not single-copy-atomic on ARMv8-R AArch32 (it can be
/// preempted between the halves), and a torn read would fabricate a wild age
/// and latch a spurious SI_STOP that only an explicit re-enable clears. The
/// arrival time is therefore one lock-free std::atomic<double>, the same
/// reasoning as input_staleness_gate.hpp's last_input_sec_. "Never seen" is
/// the -1.0 sentinel in that same atomic, so ever() and the age can never
/// disagree; every reader takes exactly one load.
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

}  // namespace supervision
}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_
