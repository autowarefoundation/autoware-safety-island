
# Add source files
list(APPEND APP_SOURCES
  core/autoware/autoware_mpc_lateral_controller/src/mpc_lateral_controller.cpp
  core/autoware/autoware_mpc_lateral_controller/src/mpc.cpp
  core/autoware/autoware_mpc_lateral_controller/src/mpc_trajectory.cpp
  core/autoware/autoware_mpc_lateral_controller/src/mpc_utils.cpp
  core/autoware/autoware_mpc_lateral_controller/src/steering_predictor.cpp
  core/autoware/autoware_mpc_lateral_controller/src/lowpass_filter.cpp
  core/autoware/autoware_mpc_lateral_controller/src/qp_solver/qp_solver_unconstraint_fast.cpp
  core/autoware/autoware_mpc_lateral_controller/src/steering_offset/steering_offset.cpp
  core/autoware/autoware_mpc_lateral_controller/src/vehicle_model/vehicle_model_bicycle_dynamics.cpp
  core/autoware/autoware_mpc_lateral_controller/src/vehicle_model/vehicle_model_bicycle_kinematics.cpp
  core/autoware/autoware_mpc_lateral_controller/src/vehicle_model/vehicle_model_bicycle_kinematics_no_delay.cpp
  core/autoware/autoware_mpc_lateral_controller/src/vehicle_model/vehicle_model_interface.cpp
)

# Add include directories
list(APPEND APP_INCLUDE_DIRS
  core/autoware/autoware_mpc_lateral_controller/include
)
