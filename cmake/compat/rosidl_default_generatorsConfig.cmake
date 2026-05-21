set(rosidl_default_generators_FOUND TRUE)
function(rosidl_generate_interfaces target)
  # Will be overridden at Stage 2-2 wiring time.
  message(STATUS "AutowarePackageCompat: rosidl_generate_interfaces(${target}) — handled out-of-band")
endfunction()
