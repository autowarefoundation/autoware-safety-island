// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__LOGGING_HPP_
#define RCLCPP__LOGGING_HPP_
#include <cstdio>
#include "rclcpp/logger.hpp"

#define RCLCPP_LOG_(level, logger, fmt, ...) \
  do { std::fprintf(stderr, "[" level "][%s] " fmt "\n", \
                    (logger).get_name().c_str(), ##__VA_ARGS__); } while (0)

#define RCLCPP_INFO(logger, fmt, ...)   RCLCPP_LOG_("INFO",  logger, fmt, ##__VA_ARGS__)
#define RCLCPP_WARN(logger, fmt, ...)   RCLCPP_LOG_("WARN",  logger, fmt, ##__VA_ARGS__)
#define RCLCPP_ERROR(logger, fmt, ...)  RCLCPP_LOG_("ERROR", logger, fmt, ##__VA_ARGS__)
#define RCLCPP_DEBUG(logger, fmt, ...)  RCLCPP_LOG_("DEBUG", logger, fmt, ##__VA_ARGS__)

#define RCLCPP_INFO_THROTTLE(logger, clock, period, fmt, ...) \
  RCLCPP_INFO(logger, fmt, ##__VA_ARGS__)
#define RCLCPP_WARN_THROTTLE(logger, clock, period, fmt, ...) \
  RCLCPP_WARN(logger, fmt, ##__VA_ARGS__)
#define RCLCPP_ERROR_THROTTLE(logger, clock, period, fmt, ...) \
  RCLCPP_ERROR(logger, fmt, ##__VA_ARGS__)
#define RCLCPP_DEBUG_THROTTLE(logger, clock, period, fmt, ...) \
  RCLCPP_DEBUG(logger, fmt, ##__VA_ARGS__)

#define RCLCPP_INFO_STREAM(logger, msg) RCLCPP_INFO((logger), "%s", \
    (static_cast<std::ostringstream &>(std::ostringstream() << msg)).str().c_str())

#endif  // RCLCPP__LOGGING_HPP_
