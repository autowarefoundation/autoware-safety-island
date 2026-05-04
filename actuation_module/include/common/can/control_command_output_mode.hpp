#ifndef COMMON__CAN__CONTROL_COMMAND_OUTPUT_MODE_HPP_
#define COMMON__CAN__CONTROL_COMMAND_OUTPUT_MODE_HPP_

#include <cstdint>

#include "platform/platform_config.h"

namespace common::can
{

enum class ControlCommandOutputMode : uint8_t
{
  DDS_ONLY = 0U,
  CAN_ONLY = 1U,
  DDS_AND_CAN = 2U,
};

constexpr bool output_mode_uses_dds(const ControlCommandOutputMode mode)
{
  return mode == ControlCommandOutputMode::DDS_ONLY || mode == ControlCommandOutputMode::DDS_AND_CAN;
}

constexpr bool output_mode_uses_can(const ControlCommandOutputMode mode)
{
  return mode == ControlCommandOutputMode::CAN_ONLY || mode == ControlCommandOutputMode::DDS_AND_CAN;
}

constexpr const char * output_mode_name(const ControlCommandOutputMode mode)
{
  switch (mode) {
    case ControlCommandOutputMode::DDS_ONLY:
      return "DDS_ONLY";
    case ControlCommandOutputMode::CAN_ONLY:
      return "CAN_ONLY";
    case ControlCommandOutputMode::DDS_AND_CAN:
      return "DDS_AND_CAN";
  }
  return "DDS_ONLY";
}

constexpr ControlCommandOutputMode configured_control_command_output_mode()
{
#if defined(CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY) && CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY
  return ControlCommandOutputMode::CAN_ONLY;
#elif defined(CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN) && CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN
  return ControlCommandOutputMode::DDS_AND_CAN;
#else
  return ControlCommandOutputMode::DDS_ONLY;
#endif
}

}  // namespace common::can

#endif  // COMMON__CAN__CONTROL_COMMAND_OUTPUT_MODE_HPP_
