// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_

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

}  // namespace supervision
}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_LATCH_HPP_
