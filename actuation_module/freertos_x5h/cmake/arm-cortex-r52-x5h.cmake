# Copyright (c) 2026, Arm Limited and contributors.
# SPDX-License-Identifier: Apache-2.0
#
# CMake toolchain file for the standalone CycloneDDS cross-build
# (scripts/build-cdds-target.sh). Used ONLY for that invocation: the main
# actuation_x5h target does not use a toolchain file at all -- it is built
# by including() into the R-Car BSP's own project() (see
# cmake/inject_actuation_x5h.cmake), which selects
# rcar_bsp/.../toolchain_arm_none_eabi.cmake and derives its Cortex-R52 ABI
# flags from the vendor's own directory-scoped add_compile_options().
#
# This file exists because CycloneDDS's own CMakeLists.txt is configured as
# an independent top-level project (cmake -S cyclonedds -B ...), so it never
# sees the vendor project's directory-scoped flags -- it needs its own
# complete cross-compile description.
#
# ABI: -mfloat-abi=softfp (NOT S32Z2's -mfloat-abi=hard) -- matches the
# R-Car BSP's own inherited compile flags for x5h (confirmed by reading
# rcar_bsp/.../CMakeLists.txt's add_compile_options() call). A CycloneDDS
# static library built with a different float ABI than the rest of the
# link would produce a silent VFP calling-convention mismatch, not a link
# error.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-r52)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

set(_X5H_CFLAGS
    "-mcpu=cortex-r52 -mtune=cortex-r52 -mfpu=neon-fp-armv8 -mfloat-abi=softfp "
    "-ffreestanding -ffunction-sections -fdata-sections -fno-common"
)
string(REPLACE ";" "" _X5H_CFLAGS "${_X5H_CFLAGS}")

# build-cdds-target.sh passes its own, more complete -DCMAKE_C_FLAGS /
# -DCMAKE_CXX_FLAGS on the cmake command line (include paths, LWIP_TIMEVAL_
# PRIVATE=0, etc.), which pre-populates the CMAKE_C_FLAGS/CMAKE_CXX_FLAGS
# cache entries before this file runs -- so these _INIT values are only
# used as a fallback for a hand-run `cmake --toolchain` invocation that
# doesn't set them itself. Kept for that reason and to document the ABI in
# one place, mirroring freertos_s32z2/cmake/arm-cortex-r52.cmake's structure.
set(CMAKE_C_FLAGS_INIT "${_X5H_CFLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_X5H_CFLAGS} -fexceptions -frtti")
set(CMAKE_ASM_FLAGS_INIT "${_X5H_CFLAGS}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
