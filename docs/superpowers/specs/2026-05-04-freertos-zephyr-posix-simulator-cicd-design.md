# FreeRTOS/Zephyr POSIX Simulator CI/CD Integration Design (Issue #9)

Date: 2026-05-04
Repository: `autowarefoundation/autoware-safety-island`
Issue: `#9 FreeRTOS/Zephyr POSIX simulator CI/CD integration`

## 1. Problem Statement

The repository already builds both Zephyr and FreeRTOS POSIX simulator targets in CI. However, for FreeRTOS, CI currently proves only that the simulator binary compiles. It does not verify that the binary can start correctly at runtime in the GitHub Actions environment.

Issue #9 needs a practical CI/CD integration definition that strengthens signal quality without over-expanding scope.

## 2. Goals

1. Keep current Zephyr and FreeRTOS build coverage intact.
2. Add a bounded runtime smoke test for the FreeRTOS POSIX simulator binary.
3. Apply the same smoke gate to both pull request CI and release workflow.
4. Make failures easy to diagnose with explicit error reasons and logs.

## 3. Non-Goals

1. Zephyr runtime execution in CI (FVP run or hardware-in-loop) is out of scope.
2. Extended test matrices (build variants, sanitizers, configuration permutations) are out of scope.
3. Full DDS functional end-to-end integration tests are out of scope.

## 4. Current State Summary

1. `build-ci.yml` already has:
   - `Zephyr Build (FVP)`
   - `FreeRTOS POSIX Simulator Build`
2. `release.yml` already builds Zephyr artifacts and the FreeRTOS simulator artifact.
3. FreeRTOS simulator binary is produced at `build-freertos/app/actuation_freertos`.
4. Recent scheduled runs show both build jobs succeeding.

This indicates integration is architecturally valid and operational, but runtime startup is not currently asserted.

## 5. Chosen Approach

Chosen option: **extend existing FreeRTOS jobs inline** (Approach A).

Rationale:

1. Minimal workflow churn: no new jobs, no artifact handoff changes.
2. Fast implementation: add one phase after existing FreeRTOS build phase.
3. Preserves current pipeline readability and cache behavior.

## 6. Detailed Design

### 6.1 Workflow Changes

Add one new step after FreeRTOS build in each workflow:

1. `.github/workflows/build-ci.yml`
   - In job `build-freertos`, after `Phase 3 — FreeRTOS simulator binary`, add:
   - `Phase 4 — FreeRTOS runtime smoke`
2. `.github/workflows/release.yml`
   - In job `build-freertos`, after `Phase 3 — FreeRTOS simulator binary`, add:
   - `Phase 4 — FreeRTOS runtime smoke`

No structural changes to Zephyr jobs.

### 6.2 Smoke Execution Contract

The new smoke step will:

1. Verify executable exists: `build-freertos/app/actuation_freertos`.
2. Run executable under a hard timeout (target: `20s`) to prevent hangs.
3. Capture output to a log file.
4. Assert startup markers are present in the log:
   - `FreeRTOS POSIX simulator starting...`
   - `Starting Controller Node...`
5. Treat timeout exit code (`124`) as expected if markers are present.

Reason for timeout acceptance: the simulator is designed as a long-running process; bounded timeout is the correct smoke semantics in CI.

### 6.3 Exit and Failure Semantics

The smoke step fails when any of the following is true:

1. Executable is missing.
2. Process exits unexpectedly before timeout and before markers are observed.
3. Timeout occurs but required markers are absent.

On failure, print:

1. Exit code.
2. A short reason (`missing binary`, `unexpected exit`, `missing marker`).
3. Captured log content for triage.

On success, print a one-line confirmation (`Smoke OK: startup markers observed`).

### 6.4 CI/CD Gate Behavior

1. Pull request and scheduled CI:
   - FreeRTOS job now requires build + runtime smoke pass.
2. Release:
   - FreeRTOS runtime smoke is required before artifact upload/publish.

This turns release output into a stronger guarantee: artifact is not only buildable but startup-valid in host CI runtime.

## 7. Data Flow and Artifacts

1. Inputs:
   - Existing built binary (`build-freertos/app/actuation_freertos`).
2. Runtime:
   - One timeout-bounded process execution in containerized runner.
3. Outputs:
   - Pass/fail decision.
   - Smoke log in step output for diagnostics.
4. Existing published release artifacts remain unchanged.

## 8. Risks and Mitigations

1. **Risk: flaky marker detection due to formatting changes.**
   - Mitigation: match short stable substrings only.
2. **Risk: CI hangs.**
   - Mitigation: mandatory timeout with explicit exit-code handling.
3. **Risk: startup slows over time and misses timeout window.**
   - Mitigation: set practical timeout (20s) and adjust only if evidence shows consistent false negatives.

## 9. Validation Plan

1. Open PR with workflow updates.
2. Confirm `build-freertos` succeeds on PR with new smoke phase.
3. Confirm Zephyr job behavior unchanged.
4. Manually inspect one successful run log to verify marker checks and timeout behavior.

## 10. Acceptance Criteria

Issue #9 is complete when all are true:

1. FreeRTOS runtime smoke phase exists in both:
   - `.github/workflows/build-ci.yml`
   - `.github/workflows/release.yml`
2. Smoke phase uses bounded timeout and cannot hang runner indefinitely.
3. Smoke phase validates startup markers and has deterministic pass/fail logic.
4. Failure path emits actionable logs.
5. Zephyr path remains build-only for this issue.

## 11. Out-of-Scope Follow-Ups

1. Zephyr runtime smoke in CI (FVP execution support).
2. Expanded FreeRTOS runtime checks (DDS traffic validation, matrix variants).
3. Test script extraction into reusable helper if smoke logic grows.
