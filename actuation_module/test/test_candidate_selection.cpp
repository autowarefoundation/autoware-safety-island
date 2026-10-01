// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#include "autoware/trajectory_follower_node/candidate_identity.hpp"
#include "autoware/trajectory_follower_node/startup_config.hpp"

#include <cassert>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#include <utility>

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
  const auto fallback = StartupConfig::fromEnvironment();
  assert(fallback.build_default);
  assert(fallback.mode == StartupConfig::Mode::SI_CONTROL);
  assert(fallback.trajectory_source == StartupConfig::TrajectorySource::AUTOWARE);
  for (const auto & half : {std::pair<const char *, const char *>{"vp", nullptr},
      std::pair<const char *, const char *>{nullptr, "vp"}})
  {
    try {
      StartupConfig::fromValues(half.first, half.second);
      assert(false && "half-set startup configuration accepted");
    } catch (const std::invalid_argument &) {}
  }
  setenv("SI_SUPERVISION_MODE", "si", 1);
  setenv("SI_TRAJECTORY_SOURCE", "vp", 1);
  assert(StartupConfig::fromEnvironment().trajectory_source ==
    StartupConfig::TrajectorySource::VP);
  assert(!StartupConfig::fromEnvironment().build_default);

  // Legacy control_cmd policy: build default "always", explicit values, and
  // a fatal invalid value on both the mode/source and the default path.
  unsetenv("SI_LEGACY_CONTROL_CMD");
  assert(StartupConfig::fromEnvironment().legacy_control_cmd ==
    StartupConfig::LegacyControlCmd::ALWAYS);
  setenv("SI_LEGACY_CONTROL_CMD", "stop_only", 1);
  assert(StartupConfig::fromEnvironment().legacy_control_cmd ==
    StartupConfig::LegacyControlCmd::STOP_ONLY);
  setenv("SI_LEGACY_CONTROL_CMD", "always", 1);
  assert(StartupConfig::fromEnvironment().legacy_control_cmd ==
    StartupConfig::LegacyControlCmd::ALWAYS);
  assert(StartupConfig::fromValues("si", "autoware", "stop_only").legacy_control_cmd ==
    StartupConfig::LegacyControlCmd::STOP_ONLY);
  assert(StartupConfig::fromValues(nullptr, nullptr, "stop_only").legacy_control_cmd ==
    StartupConfig::LegacyControlCmd::STOP_ONLY);
  for (const auto & bad : {"", "STOP_ONLY", "never"}) {
    for (const auto & modes : {std::pair<const char *, const char *>{nullptr, nullptr},
        std::pair<const char *, const char *>{"si", "vp"}})
    {
      try {
        StartupConfig::fromValues(modes.first, modes.second, bad);
        assert(false && "invalid legacy control_cmd policy accepted");
      } catch (const std::invalid_argument &) {}
    }
  }
  unsetenv("SI_LEGACY_CONTROL_CMD");

  // Operation-mode durability: the build default is transient_local (the
  // in-repo demo bridge retains the on-change state for late-joining SIs),
  // volatile is selectable for bridges that publish volatile, and an invalid
  // value is fatal on both the mode/source and the build-default paths.
  unsetenv("SI_OPERATION_MODE_DURABILITY");
  assert(StartupConfig::fromEnvironment().operation_mode_durability ==
    StartupConfig::OpModeDurability::TRANSIENT_LOCAL);
  setenv("SI_OPERATION_MODE_DURABILITY", "volatile", 1);
  assert(StartupConfig::fromEnvironment().operation_mode_durability ==
    StartupConfig::OpModeDurability::VOLATILE);
  setenv("SI_OPERATION_MODE_DURABILITY", "transient_local", 1);
  assert(StartupConfig::fromEnvironment().operation_mode_durability ==
    StartupConfig::OpModeDurability::TRANSIENT_LOCAL);
  assert(StartupConfig::fromValues("si", "autoware", nullptr, "volatile")
           .operation_mode_durability == StartupConfig::OpModeDurability::VOLATILE);
  assert(StartupConfig::fromValues(nullptr, nullptr, nullptr, "volatile")
           .operation_mode_durability == StartupConfig::OpModeDurability::VOLATILE);
  for (const auto & bad : {"", "TRANSIENT_LOCAL", "both"}) {
    for (const auto & modes : {std::pair<const char *, const char *>{nullptr, nullptr},
        std::pair<const char *, const char *>{"si", "vp"}})
    {
      try {
        StartupConfig::fromValues(modes.first, modes.second, nullptr, bad);
        assert(false && "invalid operation-mode durability accepted");
      } catch (const std::invalid_argument &) {}
    }
  }
  unsetenv("SI_OPERATION_MODE_DURABILITY");

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
