// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "autoware/trajectory_follower_node/stop_profile.hpp"

#ifdef NDEBUG
#error "test_stop_profile.cpp relies on assert(); build it without NDEBUG"
#endif

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main()
{
  StopProfile p(0.5, 3.0);

  // Before any heartbeat nothing is armed: a stale age or a fault must not trip.
  assert(!p.update(0.0, false, 1e9, false, 5.0));
  assert(!p.update(0.1, false, 1e9, true, 5.0));
  assert(!p.armed());

  // First fresh heartbeat arms.
  assert(!p.update(1.0, true, 0.05, false, 5.0));
  assert(p.armed());
  assert(!p.active());
  assert(std::strcmp(p.reason(), "") == 0);

  // Fresh stream keeps it idle.
  assert(!p.update(1.15, true, 0.10, false, 5.0));

  // Stale by exactly the threshold does not trip; beyond it does.
  assert(!p.update(1.6, true, 0.5, false, 5.0));
  assert(p.update(1.75, true, 0.6, false, 5.0));
  assert(p.active());
  assert(std::strcmp(p.reason(), "hb_stale") == 0);
  // v0 is the ego speed at the trip; the ramp is v0 - 3 t.
  assert(near(p.targetVelocity(1.75), 5.0));
  assert(near(p.targetVelocity(2.75), 2.0));
  assert(near(p.targetVelocity(3.75), 0.0));
  assert(near(p.targetVelocity(9.0), 0.0));
  // Commanded acceleration is -decel for as long as the override is active,
  // including after the ramp has floored at 0 -- releasing the brake there
  // would be a brake release, not a hold.
  assert(near(p.commandedAcceleration(), -3.0));

  // Latched: a fresh heartbeat alone does not clear the trip. Clearing
  // requires BOTH conditions healthy: heartbeat fresh and no fault.
  assert(p.update(2.0, true, 0.7, false, 3.0));      // still stale -> still active
  assert(!p.update(2.2, true, 0.05, false, 3.0));    // fresh again, no fault -> idle
  assert(!p.active());
  assert(p.armed());

  // Fault trips regardless of heartbeat freshness, and holds until cleared.
  assert(p.update(3.0, true, 0.05, true, 4.0));
  assert(std::strcmp(p.reason(), "fault") == 0);
  assert(near(p.targetVelocity(3.5), 2.5));
  assert(p.update(4.0, true, 0.05, true, 0.0));
  assert(!p.update(4.2, true, 0.05, false, 0.0));

  // A fault on an unarmed profile must not trip (VisionPilot never ran).
  StopProfile q(0.5, 3.0);
  assert(!q.update(0.0, false, 1e9, true, 6.0));

  // Idle (never tripped): targetVelocity() reports 0 regardless of "now".
  assert(near(q.targetVelocity(0.0), 0.0));
  assert(near(q.targetVelocity(123.0), 0.0));
  // Idle: commanded acceleration is 0.0, not a deceleration.
  assert(near(q.commandedAcceleration(), 0.0));

  // A negative (or otherwise bogus) ego speed at the trip clamps v0 to 0:
  // the ramp must never command a negative velocity.
  StopProfile r(0.5, 3.0);
  assert(!r.update(0.0, true, 0.05, false, -2.0));   // arms regardless of speed sign
  assert(r.armed());
  assert(r.update(1.0, true, 0.6, false, -2.0));     // trips on stale heartbeat
  assert(near(r.targetVelocity(1.0), 0.0));
  assert(near(r.targetVelocity(2.0), 0.0));

  std::puts("test_stop_profile: ok");
  return 0;
}
