// test_input_staleness_gate.cpp
//
// Host unit test for the controller's input-staleness gate (task-36 part B):
// once every required input has been seen at least once, the controller used
// to compute and publish forever on inputs that had stopped arriving. The
// gate suspends control_cmd publication when even the freshest input is
// older than the stale threshold, returns fresh only below the lower fresh
// threshold (hysteresis), and reports each state change at most once and at
// most once per rate-limit interval (the console duty budget cannot afford
// a per-cycle message, nor a per-edge one on a boundary-flapping link).
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

// Legacy-shape gate for the pre-hysteresis behavioral tests below: fresh
// threshold equal to the stale threshold collapses the hysteresis band to
// nothing (any age not stale is fresh enough), and a zero report interval
// disables edge rate limiting, so sections 1-7 keep testing exactly the
// original single-threshold, every-edge-reported contract.
static InputStalenessGate makeLegacyGate(double thr)
{
  return InputStalenessGate(thr, thr, 0.0);
}

int main()
{
  constexpr double kThr = 0.5;  // seconds; mirrors the controller's constant

  // 1. Before any input has been seen: no transitions, publishing not
  //    suppressed by THIS gate (the has_* readiness flags gate that phase).
  {
    InputStalenessGate g = makeLegacyGate(kThr);
    assert(g.update(100.0) == Transition::kNone);
    assert(g.publishAllowed());
    assert(g.update(1000.0) == Transition::kNone);
    assert(g.publishAllowed());
  }

  // 2. Fresh inputs: allowed, no transition.
  {
    InputStalenessGate g = makeLegacyGate(kThr);
    g.noteInput(10.0);
    assert(g.update(10.1) == Transition::kNone);
    assert(g.publishAllowed());
  }

  // 3. Inputs stop: exactly one kBecameStale transition, then silence while
  //    still stale (once-per-transition logging contract), publishing gated.
  {
    InputStalenessGate g = makeLegacyGate(kThr);
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
    InputStalenessGate g = makeLegacyGate(kThr);
    g.noteInput(10.0);
    assert(g.update(10.0 + kThr) == Transition::kNone);
    assert(g.publishAllowed());
  }

  // 5. Recovery: inputs resume -> exactly one kBecameFresh transition,
  //    publishing resumes, then silence again.
  {
    InputStalenessGate g = makeLegacyGate(kThr);
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
    InputStalenessGate g = makeLegacyGate(kThr);
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
    InputStalenessGate g = makeLegacyGate(kThr);
    assert(g.ageSec(5.0) < 0.0);  // sentinel: nothing received yet
    g.noteInput(10.0);
    assert(g.ageSec(12.5) > 2.49 && g.ageSec(12.5) < 2.51);
  }

  // The controller's real threshold pair: stale above 0.5 s, fresh below
  // 0.35 s (see controller_node.hpp for the derivations). No rate limit
  // here so the hysteresis sections observe every edge.
  constexpr double kFresh = 0.35;

  // 8. Hysteresis, stale side: an age inside the band (fresh threshold,
  //    stale threshold] must NOT flip a fresh gate stale.
  {
    InputStalenessGate g(kThr, kFresh, 0.0);
    g.noteInput(10.0);
    assert(g.update(10.4) == Transition::kNone);   // 0.40 s: in the band
    assert(g.publishAllowed());
    assert(g.update(10.5) == Transition::kNone);   // exactly kThr: still fresh
    assert(g.publishAllowed());
    assert(g.update(10.51) == Transition::kBecameStale);  // above kThr
    assert(!g.publishAllowed());
  }

  // 9. Hysteresis, fresh side: once stale, an age inside the band must NOT
  //    flip the gate fresh — only an age strictly below the fresh
  //    threshold may.
  {
    InputStalenessGate g(kThr, kFresh, 0.0);
    g.noteInput(10.0);
    assert(g.update(11.0) == Transition::kBecameStale);
    assert(!g.publishAllowed());
    g.noteInput(11.1);
    assert(g.update(11.5) == Transition::kNone);   // 0.40 s: in the band, stays stale
    assert(!g.publishAllowed());
    g.noteInput(12.0);
    assert(g.update(12.1) == Transition::kBecameFresh);  // 0.10 s < kFresh
    assert(g.publishAllowed());
  }

  // 9b. Fresh-side boundary: an age exactly equal to the fresh threshold
  //     keeps the stale state (strict <). Uses a fresh threshold of 0.25 —
  //     exactly representable in binary floating point — so the boundary
  //     comparison is exact rather than at the mercy of decimal rounding.
  {
    InputStalenessGate g(kThr, 0.25, 0.0);
    g.noteInput(10.0);
    assert(g.update(11.0) == Transition::kBecameStale);
    g.noteInput(11.0);
    assert(g.update(11.25) == Transition::kNone);  // age exactly 0.25: stays stale
    assert(!g.publishAllowed());
    g.noteInput(11.5);
    assert(g.update(11.625) == Transition::kBecameFresh);  // 0.125 < 0.25
    assert(g.publishAllowed());
  }

  // 10. Edge-report rate limiting: within the interval, the STATE still
  //     flips (publishAllowed() is authoritative and unlimited) but the
  //     returned edge is suppressed; the first edge and any edge outside
  //     the interval are reported.
  {
    constexpr double kInterval = 10.0;
    InputStalenessGate g(kThr, kFresh, kInterval);
    g.noteInput(0.0);
    assert(g.update(1.0) == Transition::kBecameStale);  // first edge: reported
    assert(!g.publishAllowed());
    g.noteInput(1.05);
    assert(g.update(1.1) == Transition::kNone);  // became fresh, suppressed
    assert(g.publishAllowed());                  // ...but the state DID flip
    assert(g.update(2.0) == Transition::kNone);  // became stale, suppressed
    assert(!g.publishAllowed());                 // ...and so did this one
    assert(g.update(10.9) == Transition::kNone);  // no state change: nothing to report
    assert(!g.publishAllowed());
    g.noteInput(11.6);
    assert(g.update(11.7) == Transition::kBecameFresh);  // 10.7 s > interval: reported
    assert(g.publishAllowed());
  }

  return 0;
}
