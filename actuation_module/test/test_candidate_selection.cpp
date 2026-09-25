// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#include "autoware/trajectory_follower_node/candidate_identity.hpp"
#include "autoware/trajectory_follower_node/startup_config.hpp"

#include <cassert>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>

using namespace autoware::motion::control::trajectory_follower_node;

int main()
{
  assert(StartupConfig::parse("si", "autoware").trajectory_source ==
    StartupConfig::TrajectorySource::AUTOWARE);
  assert(StartupConfig::parse("si", "vp").trajectory_source ==
    StartupConfig::TrajectorySource::VP);
  assert(StartupConfig::parse("vp", "vp").mode == StartupConfig::Mode::VP_CONTROL);
  for (const auto & bad : {"", "unknown", "SI"}) {
    try {
      StartupConfig::parse(bad, "vp");
      assert(false && "invalid mode accepted");
    } catch (const std::invalid_argument &) {}
  }
  for (const auto & bad : {"", "unknown", "VP"}) {
    try {
      StartupConfig::parse("si", bad);
      assert(false && "invalid source accepted");
    } catch (const std::invalid_argument &) {}
  }
  try {
    StartupConfig::parse("vp", "autoware");
    assert(false && "unsupported mode/source accepted");
  } catch (const std::invalid_argument &) {}
  unsetenv("SI_SUPERVISION_MODE");
  unsetenv("SI_TRAJECTORY_SOURCE");
  try {
    StartupConfig::fromEnvironment();
    assert(false && "missing startup configuration accepted");
  } catch (const std::invalid_argument &) {}
  setenv("SI_SUPERVISION_MODE", "si", 1);
  setenv("SI_TRAJECTORY_SOURCE", "vp", 1);
  assert(StartupConfig::fromEnvironment().trajectory_source ==
    StartupConfig::TrajectorySource::VP);

  VpCandidateIdentity state;
  SourceStamp first{42, 100};
  assert(state.check(7, 1, first) == CandidateCheck::ACCEPT);
  state.note(7, 1, first);
  assert(state.check(7, 1, first) == CandidateCheck::DUPLICATE);
  assert(state.check(7, 0, first) == CandidateCheck::REGRESSION);
  assert(state.check(7, 2, first) == CandidateCheck::DUPLICATE);
  assert(state.check(7, 1, {42, 101}) == CandidateCheck::REGRESSION);
  assert(state.check(7, 2, {42, 101}) == CandidateCheck::ACCEPT);
  state.note(7, 2, {42, 101});
  assert(state.check(8, 1, {42, 101}) == CandidateCheck::DUPLICATE);
  assert(state.check(8, 1, {42, 100}) == CandidateCheck::REGRESSION);
  assert(state.check(8, 1, {42, 102}) == CandidateCheck::ACCEPT);
  state.note(8, 1, {42, 102});
  assert(state.check(7, 3, {42, 101}) == CandidateCheck::REGRESSION);
  assert(state.check(7, 4, {42, 200}) == CandidateCheck::REGRESSION);
  assert(state.check(8, 2, {42, 103}) == CandidateCheck::ACCEPT);
  assert(state.check(8, 2, {42, 1000000000u}) == CandidateCheck::INVALID_STAMP);

  AutowareTrajectoryIdentity autoware;
  assert(autoware.check({2, 10}) == CandidateCheck::ACCEPT);
  autoware.note({2, 10});
  assert(autoware.check({2, 10}) == CandidateCheck::DUPLICATE);
  assert(autoware.check({2, 9}) == CandidateCheck::REGRESSION);
  assert(autoware.check({3, 0}) == CandidateCheck::ACCEPT);
}
