// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
//
// Before/after actuation parameter profile for the MRM demo.
//
// "after" (the default, no define needed) is the tuned profile and matches
// the values this tree carried before the profile switch existed, so an
// unconfigured build is byte-identical to its predecessor on every target.
// "before" (-DACTUATION_PARAM_PROFILE_BEFORE) is the CES 2026 untuned
// baseline: weak deceleration limits, so the follower cannot track the
// MRM's decelerating trajectory closely and the vehicle stops long.
//
// ACTUATION_PARAM_PROFILE_NAME is printed in the FreeRTOS boot banner,
// which also places "actuation_param_profile=<name>" in .rodata -- that
// string is how check-elf-contract.sh (and the openadkit demo
// orchestrator's payload gate) verify which profile a binary carries.

#ifndef COMMON__ACTUATION_PARAM_PROFILE_H_
#define COMMON__ACTUATION_PARAM_PROFILE_H_

#if defined(ACTUATION_PARAM_PROFILE_BEFORE)
#define ACTUATION_PARAM_PROFILE_NAME "before"
#define ACTUATION_PARAM_STOPPED_ACC (-0.4)
#define ACTUATION_PARAM_EMERGENCY_ACC (-0.75)
#define ACTUATION_PARAM_EMERGENCY_JERK (-1.0)
#define ACTUATION_PARAM_MIN_ACC (-0.75)
#define ACTUATION_PARAM_MIN_JERK (-1.0)
#else
#define ACTUATION_PARAM_PROFILE_NAME "after"
#define ACTUATION_PARAM_STOPPED_ACC (-3.4)
#define ACTUATION_PARAM_EMERGENCY_ACC (-5.0)
#define ACTUATION_PARAM_EMERGENCY_JERK (-3.0)
#define ACTUATION_PARAM_MIN_ACC (-5.0)
#define ACTUATION_PARAM_MIN_JERK (-5.0)
#endif

#endif  // COMMON__ACTUATION_PARAM_PROFILE_H_
