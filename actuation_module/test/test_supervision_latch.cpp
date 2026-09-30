// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#include "autoware/trajectory_follower_node/supervision_latch.hpp"

#include <cassert>
#include <cmath>
#include <limits>

using autoware::motion::control::trajectory_follower_node::supervision::Envelope;
using autoware::motion::control::trajectory_follower_node::supervision::ReenableRequest;
using autoware::motion::control::trajectory_follower_node::supervision::SourceWatch;
using autoware::motion::control::trajectory_follower_node::supervision::envelopeViolation;
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

  // ReenableRequest: pending until it is older than the window, then dropped;
  // a fresh press restarts the window; a press nobody acted on cannot resume
  // driving later (the latch only sees a pending request).
  ReenableRequest rq;
  assert(!rq.pending && !rq.expireIfOld(100.0, 5.0));
  rq.request(100.0);
  assert(rq.pending);
  assert(!rq.expireIfOld(104.9, 5.0) && rq.pending);   // inside the window
  assert(!rq.expireIfOld(105.0, 5.0) && rq.pending);   // boundary is inclusive
  assert(rq.expireIfOld(105.1, 5.0) && !rq.pending);   // expired on this call
  assert(!rq.expireIfOld(200.0, 5.0));                 // already gone: no second expiry
  rq.request(300.0);
  rq.request(303.0);                                   // a new press restarts the window
  assert(!rq.expireIfOld(307.0, 5.0) && rq.pending);
  rq.clear();
  assert(!rq.pending);
  // Through the latch: an expired press leaves the stop in place even when
  // every source is fresh later.
  SupervisionState late;
  late.latch("odometry");
  ReenableRequest stale_press;
  stale_press.request(10.0);
  (void)stale_press.expireIfOld(10.0 + 60.0, 5.0);
  assert(late.stopThisTick(stale_press.pending, true) && late.latched);

  // Envelope: inside passes; each bound fails on its own; NaN and inf fail.
  const Envelope lim{0.6, 60.0, 6.0};
  const double nan = std::nan("");
  const double inf = std::numeric_limits<double>::infinity();
  assert(envelopeViolation(0.6, 60.0, 6.0, lim) == nullptr);      // bounds are inclusive
  assert(envelopeViolation(-0.6, -60.0, -6.0, lim) == nullptr);   // and symmetric
  assert(envelopeViolation(0.0, 0.0, 0.0, lim) == nullptr);
  assert(envelopeViolation(0.61, 10.0, 1.0, lim) != nullptr);
  assert(envelopeViolation(0.0, 61.0, 1.0, lim) != nullptr);
  assert(envelopeViolation(0.0, 1.0e6, 1.0, lim) != nullptr);      // a runaway speed horizon
  assert(envelopeViolation(0.0, 10.0, 6.5, lim) != nullptr);
  assert(envelopeViolation(0.0, 10.0, -6.5, lim) != nullptr);
  assert(envelopeViolation(nan, 10.0, 1.0, lim) != nullptr);
  assert(envelopeViolation(0.0, nan, 1.0, lim) != nullptr);
  assert(envelopeViolation(0.0, 10.0, nan, lim) != nullptr);
  assert(envelopeViolation(0.0, inf, 1.0, lim) != nullptr);

  // SourceWatch: a never-seen source is not stale and has no age.
  SourceWatch w;
  assert(!w.ever());
  assert(!w.stale(1.0e9, 0.5));
  assert(w.ageSec(1.0e9) == -1.0);

  // Seen: fresh inside the timeout, stale strictly past it, age is now - last.
  const double t0 = 1.7e9;  // epoch-scale, the value Clock::now() hands in
  w.note(t0);
  assert(w.ever());
  assert(!w.stale(t0 + 0.5, 0.5));
  assert(w.stale(t0 + 0.5001, 0.5));
  assert(w.ageSec(t0 + 0.25) > 0.2499 && w.ageSec(t0 + 0.25) < 0.2501);

  // A newer arrival refreshes it.
  w.note(t0 + 10.0);
  assert(!w.stale(t0 + 10.4, 0.5));
  return 0;
}
