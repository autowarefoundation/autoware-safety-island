// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STARTUP_CONFIG_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STARTUP_CONFIG_HPP_

#include <cstdlib>
#include <stdexcept>
#include <string>

// Build-time selection used when neither environment variable is set. The
// defaults keep the upstream behaviour (SI follows Autoware), so targets
// without an environment (Zephyr, bare-metal FreeRTOS) boot as before.
#ifndef SI_DEFAULT_SUPERVISION_MODE
#define SI_DEFAULT_SUPERVISION_MODE "si"
#endif
#ifndef SI_DEFAULT_TRAJECTORY_SOURCE
#define SI_DEFAULT_TRAJECTORY_SOURCE "autoware"
#endif
// Legacy DDS control_cmd policy when SI_LEGACY_CONTROL_CMD is unset. "always"
// keeps today's behaviour for every existing rig.
#ifndef SI_DEFAULT_LEGACY_CONTROL_CMD
#define SI_DEFAULT_LEGACY_CONTROL_CMD "always"
#endif

namespace autoware::motion::control::trajectory_follower_node
{

// The selection is read once, before any subscription is created. It is not
// exposed as a mutable ROS parameter: an operator must restart SI to change
// mode/source, and a selected-source failure must never trigger a fallback.
struct StartupConfig
{
  enum class Mode { SI_CONTROL, VP_CONTROL };
  enum class TrajectorySource { AUTOWARE, VP };
  // Which decisions reach the legacy DDS control_cmd topic (a transitional
  // compatibility surface; ApprovedRequest stays the only authoritative
  // output and is unaffected). ALWAYS: every approved payload, as before.
  // STOP_ONLY: SI_STOP only, so a consumer that arbitrates "fresh SI command
  // wins" (the X5H CES demo's bench arbiter) sees SI only when it overrides.
  // CAN output is not affected by this policy.
  enum class LegacyControlCmd { ALWAYS, STOP_ONLY };

  Mode mode;
  TrajectorySource trajectory_source;
  bool build_default = false;
  LegacyControlCmd legacy_control_cmd = LegacyControlCmd::ALWAYS;

  // nullptr selects the build default; an invalid value is fatal.
  static LegacyControlCmd parseLegacyControlCmd(const char * value)
  {
    const std::string v = value ? value : SI_DEFAULT_LEGACY_CONTROL_CMD;
    if (v == "always") {
      return LegacyControlCmd::ALWAYS;
    }
    if (v == "stop_only") {
      return LegacyControlCmd::STOP_ONLY;
    }
    throw std::invalid_argument("SI_LEGACY_CONTROL_CMD must be 'always' or 'stop_only'");
  }

  static StartupConfig parse(const std::string & mode, const std::string & source)
  {
    if (mode != "si" && mode != "vp") {
      throw std::invalid_argument("SI_SUPERVISION_MODE must be 'si' or 'vp'");
    }
    if (source != "autoware" && source != "vp") {
      throw std::invalid_argument("SI_TRAJECTORY_SOURCE must be 'autoware' or 'vp'");
    }
    if (mode == "vp" && source != "vp") {
      throw std::invalid_argument("VP_CONTROL requires SI_TRAJECTORY_SOURCE=vp");
    }
    return {
      mode == "vp" ? Mode::VP_CONTROL : Mode::SI_CONTROL,
      source == "vp" ? TrajectorySource::VP : TrajectorySource::AUTOWARE,
    };
  }

  // Both unset: the build default. Exactly one set is ambiguous and fatal;
  // an invalid value or combination is fatal as in parse().
  static StartupConfig fromValues(
    const char * mode, const char * source, const char * legacy_control_cmd = nullptr)
  {
    // Validated first so a bad value is fatal whichever mode/source path runs.
    const LegacyControlCmd legacy = parseLegacyControlCmd(legacy_control_cmd);
    if (!mode && !source) {
      StartupConfig config = parse(SI_DEFAULT_SUPERVISION_MODE, SI_DEFAULT_TRAJECTORY_SOURCE);
      config.build_default = true;
      config.legacy_control_cmd = legacy;
      return config;
    }
    if (!mode || !source) {
      throw std::invalid_argument(
        "set both SI_SUPERVISION_MODE and SI_TRAJECTORY_SOURCE, or neither");
    }
    StartupConfig config = parse(mode, source);
    config.legacy_control_cmd = legacy;
    return config;
  }

  static StartupConfig fromEnvironment()
  {
    return fromValues(
      std::getenv("SI_SUPERVISION_MODE"), std::getenv("SI_TRAJECTORY_SOURCE"),
      std::getenv("SI_LEGACY_CONTROL_CMD"));
  }
};

}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STARTUP_CONFIG_HPP_
