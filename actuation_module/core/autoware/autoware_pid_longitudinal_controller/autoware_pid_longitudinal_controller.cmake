
# Add source files
list(APPEND APP_SOURCES
  core/autoware/autoware_pid_longitudinal_controller/src/pid.cpp
  core/autoware/autoware_pid_longitudinal_controller/src/smooth_stop.cpp
  core/autoware/autoware_pid_longitudinal_controller/src/longitudinal_controller_utils.cpp
  core/autoware/autoware_pid_longitudinal_controller/src/pid_longitudinal_controller.cpp
)

# Add include directories
list(APPEND APP_INCLUDE_DIRS
  core/autoware/autoware_pid_longitudinal_controller/include
)
