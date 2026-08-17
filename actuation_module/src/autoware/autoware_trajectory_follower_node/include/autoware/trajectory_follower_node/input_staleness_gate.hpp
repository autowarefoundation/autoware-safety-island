// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__INPUT_STALENESS_GATE_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__INPUT_STALENESS_GATE_HPP_

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
/// freshest input. update() compares that against "now": if even the
/// freshest input is older than the threshold, every input has stopped and
/// publication is suspended. It resumes as soon as any input arrives again.
/// update() returns a Transition exactly once per state change so the
/// caller can log each edge once — never per cycle, because the UART
/// console duty budget is tight.
///
/// Clock discipline: this class never reads a clock. The caller samples
/// one clock for both noteInput() and update() readings. On the
/// freertos-x5h target that is Clock::now(), which since the Task-20
/// _gettimeofday() fix is backed by xTaskGetTickCount() — monotonic
/// time-since-boot — and is the single time convention this codebase uses.
/// Both readings come from the same clock in the same process, so only
/// monotonicity matters, not the epoch.
///
/// Pure logic, no OS/DDS/logger dependencies, so it is host-testable
/// (test/test_input_staleness_gate.cpp) the same way rpmsg_netif_core is.
class InputStalenessGate
{
public:
  enum class Transition { kNone, kBecameStale, kBecameFresh };

  explicit InputStalenessGate(double threshold_sec) : threshold_sec_(threshold_sec) {}

  /// Record that a required input arrived at time \p now_sec.
  void noteInput(double now_sec) { last_input_sec_ = now_sec; }

  /// Re-evaluate staleness at time \p now_sec. Returns kBecameStale /
  /// kBecameFresh exactly once per state change, kNone otherwise.
  ///
  /// Before the first noteInput() this never transitions and publishing
  /// stays allowed: that phase is unreachable from the publish path anyway
  /// (the controller's has_* flags already withhold publication until every
  /// required input has arrived at least once, and those arrivals call
  /// noteInput()), so the gate stays silent rather than logging a spurious
  /// "stale" edge at boot.
  Transition update(double now_sec)
  {
    if (last_input_sec_ < 0.0) {
      return Transition::kNone;
    }
    const bool now_stale = (now_sec - last_input_sec_) > threshold_sec_;
    if (now_stale == stale_) {
      return Transition::kNone;
    }
    stale_ = now_stale;
    return stale_ ? Transition::kBecameStale : Transition::kBecameFresh;
  }

  /// True unless the last update() found every input stale.
  bool publishAllowed() const { return !stale_; }

  /// Age of the freshest input at time \p now_sec; negative sentinel while
  /// nothing has been received yet.
  double ageSec(double now_sec) const
  {
    return last_input_sec_ < 0.0 ? -1.0 : now_sec - last_input_sec_;
  }

private:
  double threshold_sec_;
  // Arrival time of the most recent required input, in the caller's clock.
  // Negative sentinel = nothing received yet. Clock::now() on every target
  // is non-negative (time since boot / epoch), so the sentinel is
  // unreachable as a real reading.
  double last_input_sec_ = -1.0;
  bool stale_ = false;
};

}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__INPUT_STALENESS_GATE_HPP_
