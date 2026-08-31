// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__INPUT_STALENESS_GATE_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__INPUT_STALENESS_GATE_HPP_

#include <atomic>

namespace autoware::motion::control::trajectory_follower_node
{

/// \brief Decides WHETHER the controller may publish control_cmd, based on
/// input freshness. It changes nothing about WHAT is computed or published.
///
/// Why this exists (task-36 part B): the controller's has_* readiness flags
/// are set on the first receipt of each input and never cleared, so once
/// every input had been seen once the controller computed and published
/// forever — on inputs that had stopped arriving minutes earlier (observed
/// on the X5H board after the host peer processes exited). For a
/// safety-island component, emitting control output derived from stale
/// inputs is worse than emitting none.
///
/// Semantics: noteInput() records the receipt time of ANY required input;
/// the recorded value therefore always holds the arrival time of the
/// freshest input. update() compares that against "now" with a threshold
/// PAIR (hysteresis): the gate goes stale when even the freshest input is
/// older than stale_threshold_sec, and returns fresh only once the
/// freshest input is younger than the lower fresh_threshold_sec. An age in
/// the band between the two thresholds keeps whatever state the gate is
/// already in. The band means a single sample arriving into an
/// already-degraded window is not enough to re-declare fresh if, by the
/// time the gate looks, that sample is itself already fresh_threshold_sec
/// old — returning fresh takes evidence of an actually-fresh input, not an
/// age that merely dipped just under the stale bound (a real boundary flap
/// of exactly that shape — stale then fresh within 0.6 s — was observed on
/// the board).
///
/// Edge reporting: update() returns a Transition on a state change — never
/// per cycle — and additionally rate-limits the edges it REPORTS to at
/// most one per min_edge_report_interval_sec. Each reported edge becomes a
/// log line through a busy-polled 115200 UART (~10 ms per line); a link
/// degraded to just-above-threshold inter-arrival can legitimately produce
/// ~2 edges/s (each arrival really does reset the age, so hysteresis
/// cannot remove that flap — only make the fresh declaration demand a
/// genuinely fresh sample), and ~2 lines/s is ~2 % console duty on an
/// image whose known defect class is timing-sensitive. The rate limit
/// bounds that: the STATE still flips on every edge — publishAllowed() is
/// always authoritative and unlimited — only the returned Transition is
/// suppressed inside the window, so the log is an edge trace with bounded
/// duty, not a complete state history.
///
/// Clock discipline: this class never reads a clock. The caller samples
/// one clock for both noteInput() and update() readings. On the
/// freertos-x5h target that is Clock::now(), which since the Task-20
/// _gettimeofday() fix is backed by xTaskGetTickCount() — monotonic
/// time-since-boot — and is the single time convention this codebase uses.
/// Both readings come from the same clock in the same process, so only
/// monotonicity matters, not the epoch.
///
/// Threading: noteInput() runs on CycloneDDS callback threads (FreeRTOS
/// priority 2 on the x5h target) while update()/ageSec() run on the
/// controller pthread (priority 1), so last_input_sec_ crosses threads. A
/// plain 64-bit double store is not single-copy-atomic on ARMv8-R AArch32
/// (the compiler may emit it as two 32-bit stores, and the reader can be
/// preempted between the halves), so a torn read could fabricate a wild
/// age and a spurious edge. std::atomic<double> closes that: the pinned
/// arm-none-eabi 13.2.Rel1 toolchain inlines it lock-free on Cortex-R52
/// (ldrexd/strexd store, single-copy-atomic ldrd load — verified from the
/// generated code; the static_assert below re-checks lock-freedom on every
/// build). Relaxed ordering suffices: the timestamp is the entire message,
/// with no other memory published alongside it. All other state is touched
/// only by update() on the single controller thread.
///
/// Pure logic, no OS/DDS/logger dependencies, so it is host-testable
/// (test/test_input_staleness_gate.cpp) the same way rpmsg_netif_core is.
class InputStalenessGate
{
public:
  enum class Transition { kNone, kBecameStale, kBecameFresh };

  /// \p stale_threshold_sec: go stale when the freshest input is older.
  /// \p fresh_threshold_sec: return fresh only when it is younger; must be
  /// below stale_threshold_sec (the gap is the hysteresis band).
  /// \p min_edge_report_interval_sec: at most one returned edge per this
  /// interval (state itself is not rate-limited).
  /// Derivations for the values the controller passes live at
  /// controller_node.hpp's constants.
  InputStalenessGate(
    double stale_threshold_sec, double fresh_threshold_sec,
    double min_edge_report_interval_sec)
  : stale_threshold_sec_(stale_threshold_sec),
    fresh_threshold_sec_(fresh_threshold_sec),
    min_edge_report_interval_sec_(min_edge_report_interval_sec)
  {
  }

  /// Record that a required input arrived at time \p now_sec. Safe to call
  /// from input callback threads concurrently with update() — see the
  /// threading note above.
  void noteInput(double now_sec) { last_input_sec_.store(now_sec, std::memory_order_relaxed); }

  /// Re-evaluate staleness at time \p now_sec. Returns kBecameStale /
  /// kBecameFresh at most once per state change and at most once per
  /// min_edge_report_interval_sec, kNone otherwise. Must be called from
  /// the single controller thread only.
  ///
  /// Before the first noteInput() this never transitions and publishing
  /// stays allowed: that phase is unreachable from the publish path anyway
  /// (the controller's has_* flags already withhold publication until every
  /// required input has arrived at least once, and those arrivals call
  /// noteInput()), so the gate stays silent rather than logging a spurious
  /// "stale" edge at boot.
  Transition update(double now_sec)
  {
    const double last = last_input_sec_.load(std::memory_order_relaxed);
    if (last < 0.0) {
      return Transition::kNone;
    }
    const double age = now_sec - last;
    // Hysteresis: strict > to go stale (an age exactly equal to the stale
    // threshold is not stale, preserving the original single-threshold
    // boundary behavior) and strict < to return fresh; the band between
    // the thresholds — both boundaries included — keeps the current state.
    bool next_stale = stale_;
    if (!stale_ && age > stale_threshold_sec_) {
      next_stale = true;
    } else if (stale_ && age < fresh_threshold_sec_) {
      next_stale = false;
    }
    if (next_stale == stale_) {
      return Transition::kNone;
    }
    stale_ = next_stale;
    // Rate limit what is REPORTED, never the state change itself. A
    // suppressed edge is deliberately not replayed later: the next edge
    // outside the window reports the then-current direction, and
    // publishAllowed() carries the truth in between.
    if (last_edge_report_sec_ >= 0.0 &&
      (now_sec - last_edge_report_sec_) < min_edge_report_interval_sec_)
    {
      return Transition::kNone;
    }
    last_edge_report_sec_ = now_sec;
    return stale_ ? Transition::kBecameStale : Transition::kBecameFresh;
  }

  /// True unless the last update() found every input stale.
  bool publishAllowed() const { return !stale_; }

  /// Age of the freshest input at time \p now_sec; negative sentinel while
  /// nothing has been received yet.
  double ageSec(double now_sec) const
  {
    const double last = last_input_sec_.load(std::memory_order_relaxed);
    return last < 0.0 ? -1.0 : now_sec - last;
  }

private:
  double stale_threshold_sec_;
  double fresh_threshold_sec_;
  double min_edge_report_interval_sec_;
  // Arrival time of the most recent required input, in the caller's clock.
  // Negative sentinel = nothing received yet. Clock::now() on every target
  // is non-negative (time since boot / epoch), so the sentinel is
  // unreachable as a real reading. Atomic — see the threading note above.
  std::atomic<double> last_input_sec_{-1.0};
  static_assert(
    std::atomic<double>::is_always_lock_free,
    "last_input_sec_ must be a lock-free atomic: a locking fallback would "
    "drag a mutex into DDS callback context on the FreeRTOS target");
  bool stale_ = false;
  // Time of the last edge actually returned to the caller; negative
  // sentinel = none reported yet (the first edge is always reported).
  double last_edge_report_sec_ = -1.0;
};

}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__INPUT_STALENESS_GATE_HPP_
