
# Add source files
list(APPEND APP_SOURCES
  core/autoware/autoware_interpolation/src/linear_interpolation.cpp
  core/autoware/autoware_interpolation/src/spline_interpolation.cpp
  core/autoware/autoware_interpolation/src/spline_interpolation_points_2d.cpp
  core/autoware/autoware_interpolation/src/spherical_linear_interpolation.cpp
)

# Add include directories
list(APPEND APP_INCLUDE_DIRS
  core/autoware/autoware_interpolation/include
)
