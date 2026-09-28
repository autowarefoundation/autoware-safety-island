// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#include "autoware/trajectory_follower_node/supervision_latch.hpp"

#include <cassert>
#include <thread>

using autoware::motion::control::trajectory_follower_node::supervision::SourceWatch;
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

  // Concurrent note()/ageSec(): the reader must only ever see one of the two
  // written values, never a torn mix that would look like a wild age. This is
  // a smoke test of the single-load contract only: x86-64 stores 64-bit
  // doubles atomically anyway, so it cannot prove the ARMv8-R AArch32 claim
  // (that rests on std::atomic<double> being lock-free, static_assert'ed in
  // the header).
  SourceWatch shared;
  shared.note(t0);
  std::thread writer([&shared, t0]() {
    for (int i = 0; i < 200000; ++i) {
      shared.note(i % 2 ? t0 : t0 + 1.0e6);
    }
  });
  for (int i = 0; i < 200000; ++i) {
    const double age = shared.ageSec(t0 + 2.0e6);
    assert(age == 2.0e6 || age == 1.0e6);
  }
  writer.join();
  return 0;
}
