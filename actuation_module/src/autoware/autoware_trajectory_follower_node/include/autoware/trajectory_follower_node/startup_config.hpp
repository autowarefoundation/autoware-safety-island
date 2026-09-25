// Copyright (c) 2026, Open AD Kit contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STARTUP_CONFIG_HPP_
#define AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STARTUP_CONFIG_HPP_

#include <cstdlib>
#include <stdexcept>
#include <string>

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

  static StartupConfig fromEnvironment()
  {
    const char * mode = std::getenv("SI_SUPERVISION_MODE");
    const char * source = std::getenv("SI_TRAJECTORY_SOURCE");
    if (!mode || !source) {
      throw std::invalid_argument(
        "SI_SUPERVISION_MODE and SI_TRAJECTORY_SOURCE are required at startup");
    }
    return parse(mode, source);
  }
};

}  // namespace autoware::motion::control::trajectory_follower_node

#endif  // AUTOWARE__TRAJECTORY_FOLLOWER_NODE__STARTUP_CONFIG_HPP_
