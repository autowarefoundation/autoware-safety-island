// test_input_staleness_gate.cpp
//
// Host unit test for the controller's input-staleness gate (task-36 part B):
// once every required input has been seen at least once, the controller used
// to compute and publish forever on inputs that had stopped arriving. The
// gate suspends control_cmd publication when even the freshest input is
// older than the threshold, logs each state change exactly once (the
// console duty budget cannot afford a per-cycle message), and resumes when
// inputs resume.
//
// Pure-logic test: the gate takes explicit "now" readings, so no clock, no
// DDS, and no FreeRTOS is needed — same convention as
// test_rpmsg_netif_core.c next to this file.
//
// Like that test, this relies on assert(); a build that defines NDEBUG
// would silently pass without testing anything, so fail loudly instead.
#ifdef NDEBUG
#error "test_input_staleness_gate.cpp relies on assert(); build it without NDEBUG"
#endif

#include <assert.h>

#include "autoware/trajectory_follower_node/input_staleness_gate.hpp"

using autoware::motion::control::trajectory_follower_node::InputStalenessGate;
using Transition = InputStalenessGate::Transition;

int main()
{
  constexpr double kThr = 0.5;  // seconds; mirrors the controller's constant

  // 1. Before any input has been seen: no transitions, publishing not
  //    suppressed by THIS gate (the has_* readiness flags gate that phase).
  {
    InputStalenessGate g(kThr);
    assert(g.update(100.0) == Transition::kNone);
    assert(g.publishAllowed());
    assert(g.update(1000.0) == Transition::kNone);
    assert(g.publishAllowed());
  }

  // 2. Fresh inputs: allowed, no transition.
  {
    InputStalenessGate g(kThr);
    g.noteInput(10.0);
    assert(g.update(10.1) == Transition::kNone);
    assert(g.publishAllowed());
  }

  // 3. Inputs stop: exactly one kBecameStale transition, then silence while
  //    still stale (once-per-transition logging contract), publishing gated.
  {
    InputStalenessGate g(kThr);
    g.noteInput(10.0);
    assert(g.update(10.2) == Transition::kNone);
    assert(g.update(10.0 + kThr + 0.01) == Transition::kBecameStale);
    assert(!g.publishAllowed());
    assert(g.update(11.0) == Transition::kNone);   // still stale: no re-log
    assert(g.update(300.0) == Transition::kNone);  // minutes later: still no re-log
    assert(!g.publishAllowed());
  }

  // 4. Boundary: age exactly equal to the threshold is NOT stale (strict >).
  {
    InputStalenessGate g(kThr);
    g.noteInput(10.0);
    assert(g.update(10.0 + kThr) == Transition::kNone);
    assert(g.publishAllowed());
  }

  // 5. Recovery: inputs resume -> exactly one kBecameFresh transition,
  //    publishing resumes, then silence again.
  {
    InputStalenessGate g(kThr);
    g.noteInput(10.0);
    assert(g.update(20.0) == Transition::kBecameStale);
    assert(!g.publishAllowed());
    g.noteInput(20.05);
    assert(g.update(20.1) == Transition::kBecameFresh);
    assert(g.publishAllowed());
    assert(g.update(20.2) == Transition::kNone);
    assert(g.publishAllowed());
  }

  // 6. Repeated loss/recovery cycles keep producing exactly one transition
  //    per state change.
  {
    InputStalenessGate g(kThr);
    double t = 0.0;
    for (int i = 0; i < 3; i++) {
      g.noteInput(t);
      assert(g.update(t + 0.1) == (i == 0 ? Transition::kNone : Transition::kBecameFresh));
      assert(g.publishAllowed());
      t += kThr + 1.0;
      assert(g.update(t) == Transition::kBecameStale);
      assert(g.update(t + 0.1) == Transition::kNone);
      assert(!g.publishAllowed());
      t += 0.2;
    }
  }

  // 7. ageSec reporting (used for the one-shot log line's detail).
  {
    InputStalenessGate g(kThr);
    assert(g.ageSec(5.0) < 0.0);  // sentinel: nothing received yet
    g.noteInput(10.0);
    assert(g.ageSec(12.5) > 2.49 && g.ageSec(12.5) < 2.51);
  }

  return 0;
}
