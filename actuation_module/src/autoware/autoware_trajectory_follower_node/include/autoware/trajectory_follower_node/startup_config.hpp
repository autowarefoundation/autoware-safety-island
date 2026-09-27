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

namespace autoware::motion::control::trajectory_follower_node
{

// The selection is read once, before any subscription is created. It is not
// exposed as a mutable ROS parameter: an operator must restart SI to change
// mode/source, and a selected-source failure must never trigger a fallback.
struct StartupConfig
{
  enum class Mode { SI_CONTROL, VP_CONTROL };
  enum class TrajectorySource { AUTOWARE, VP };

  Mode mode;
  TrajectorySource trajectory_source;
  bool build_default = false;

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
  static StartupConfig fromValues(const char * mode, const char * source)
  {
    if (!mode && !source) {
      StartupConfig config = parse(SI_DEFAULT_SUPERVISION_MODE, SI_DEFAULT_TRAJECTORY_SOURCE);
      config.build_default = true;
      return config;
    }
    if (!mode || !source) {
      throw std::invalid_argument(
        "set both SI_SUPERVISION_MODE and SI_TRAJECTORY_SOURCE, or neither");
    }
    return parse(mode, source);
  }

  static StartupConfig fromEnvironment()
  {
    return fromValues(std::getenv("SI_SUPERVISION_MODE"), std::getenv("SI_TRAJECTORY_SOURCE"));
  }
};

}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STARTUP_CONFIG_HPP_
