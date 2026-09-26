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
  HOLD = 2,     ///< no new approved decision; actuate the previous payload
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

/// Arrival-age freshness of one selected source.
///
/// Ages are measured against Clock::now() at callback time, never against
/// message stamps: the selected sources carry CARLA simulated time (and VP
/// produced stamps in its own host clock), so a stamp must never be
/// subtracted from SI's clock (vp_si_control_contract: "A CARLA episode time
/// in seconds must not be subtracted from SI's Unix/system clock").
class SourceWatch
{
public:
  void note(double now) {last_arrival_ = now; ever_ = true;}

  bool ever() const {return ever_;}

  double ageSec(double now) const {return ever_ ? now - last_arrival_ : -1.0;}

  /// True when the source has been seen before but is older than the timeout.
  /// A source that was never seen is NOT stale here: unavailability before
  /// the first sample is "not ready" (the controller's processData path) and
  /// must not fabricate a fault before driving ever started.
  bool stale(double now, double timeout_sec) const
  {
    return ever_ && (now - last_arrival_) > timeout_sec;
  }

private:
  double last_arrival_ = 0.0;
  bool ever_ = false;
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
