// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_HPP_

#include <cstdint>
#include <string>

#include "autoware/autoware_msgs/messages.hpp"
#include "autoware/trajectory_follower_node/supervision_latch.hpp"

namespace autoware::motion::control::trajectory_follower_node
{
namespace supervision
{

/// Decision carried on every ApprovedRequest (octet; IDL comments keep the
/// numeric meaning authoritative).
enum class Decision : uint8_t
{
  NORMAL = 0,   ///< approved request computed this cycle
  SI_STOP = 1,  ///< supervisor stop; fault_id identifies the fault event
  HOLD = 2,     ///< no new approved decision; consumer keeps its previous state
                ///< (payload is the conservative stopped command, never a brake release)
};

/// Startup-only supervision mode (SI_SUPERVISION_MODE, immutable for the run).
enum class Mode : uint8_t
{
  SI_CONTROL = 0,  ///< follower follows the selected trajectory source
  VP_CONTROL = 1,  ///< VisionPilot command is supervised, not recomputed
};

/// Where the NORMAL control payload of a cycle comes from.
enum class SelectedSource : uint8_t
{
  FOLLOWER = 0,
  VP_COMMAND = 1,
};

/// Build the explicit SI-stop control payload: hold the last known steering
/// (never command a steer reset during a stop) and command a standstill with
/// a signed negative acceleration so the vehicle stops regardless of its
/// current speed target. While stopped (SI_STOP hold) the follower's stop
/// machinery keeps the vehicle parked.
struct StopControl
{
  ControlMsg operator()(const ControlMsg * last_approved, float stop_accel_mps2) const
  {
    ControlMsg out{};
    if (last_approved) {
      out.lateral = last_approved->lateral;
    }
    out.longitudinal.velocity = 0.0f;
    out.longitudinal.acceleration = -stop_accel_mps2;
    out.longitudinal.is_defined_acceleration = true;
    return out;
  }
};

}  // namespace supervision
}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__SUPERVISION_HPP_
