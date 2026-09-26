// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#include "autoware/trajectory_follower_node/supervision_latch.hpp"

#include <cassert>

using autoware::motion::control::trajectory_follower_node::supervision::SupervisionState;

int main()
{
  SupervisionState s;
  bool request = false;

  // Never latched: no stop, a stray request is left alone.
  assert(!s.stopThisTick(request, true));
  assert(s.fault_id == 0);

  // A fault latches with a new id and stops every tick until re-enabled.
  s.latch("trajectory");
  assert(s.latched && s.fault_id == 1 && s.reason == "trajectory");
  assert(s.stopThisTick(request, true));
  assert(s.stopThisTick(request, true));

  // A re-enable that meets a stale source keeps stopping and stays pending.
  request = true;
  assert(s.stopThisTick(request, false));
  assert(s.latched && request);

  // Once every source is fresh the request clears the latch, is consumed,
  // and the SAME tick continues NORMAL: no SI_STOP on the clearing tick
  // (the former one-cycle stop without an active fault).
  assert(!s.stopThisTick(request, true));
  assert(!s.latched && !request && s.reason.empty());
  assert(s.fault_id == 1);
  assert(!s.stopThisTick(request, true));

  // A later fault is a new event; an ongoing fault keeps its id.
  s.latch("vp command");
  assert(s.fault_id == 2);
  s.latch("odometry");
  assert(s.fault_id == 2 && s.reason == "odometry");
  assert(s.stopThisTick(request, true));  // no request: stays latched
  return 0;
}
