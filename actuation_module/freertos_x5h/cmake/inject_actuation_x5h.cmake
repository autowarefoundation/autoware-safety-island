# Copyright (c) 2026, Arm Limited and contributors.
# SPDX-License-Identifier: Apache-2.0
#
# Injected into the R-Car BSP's own CMake invocation via
# -DCMAKE_PROJECT_INCLUDE=<this file>, passed by build.sh's
# build_freertos_x5h(). This is the mechanism that lets actuation_x5h reuse
# the vendored rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52 tree completely
# unmodified while still being buildable as its own CMake target.
#
# Why this indirection exists (and why it is NOT the simpler
# `add_subdirectory(actuation_module/freertos_x5h)` pattern freertos_s32z2
# uses): rcar_bsp's own top-level CMakeLists.txt derives BSP_DIR/
# FREERTOS_DIR/KERNEL_DIR/LOGGING_DIR from CMAKE_SOURCE_DIR --
#   set(BSP_DIR ${CMAKE_SOURCE_DIR})
#   file(REAL_PATH ${CMAKE_SOURCE_DIR}/../../.. FREERTOS_DIR)
# CMAKE_SOURCE_DIR is fixed for the entire cmake invocation to the actual -S
# argument; it is NOT a normal lexically-scoped variable (a local set()
# shadow in an ancestor add_subdirectory() scope has no effect on it -- this
# was verified empirically while developing this target). So if
# actuation_module/freertos_x5h/CMakeLists.txt were the top-level -S
# argument and add_subdirectory()'d the vendor tree as a child, the vendor's
# own path derivation above would resolve to the wrong directory entirely.
#
# The fix: build.sh points -S directly at the vendor's own directory (as
# Task 2's scripts/build-bsp-rpmsg-sample.sh already does), so
# CMAKE_SOURCE_DIR is correct throughout the vendor's file. This file is
# then injected via CMAKE_PROJECT_INCLUDE, which CMake include()s
# immediately after the outermost project() call -- too early to pull in
# actuation_module/freertos_x5h/CMakeLists.txt directly, because the
# freertos_bsp / openamp / libmetal targets our target links against do not
# exist yet at that point in the vendor file, and target_link_libraries()
# against a not-yet-existing target name silently degrades to a bare
# "-l<name>" linker flag instead of a real target dependency (no usage
# requirements, no ordering guarantee).
#
# cmake_language(DEFER ...) fixes the ordering by deferring execution until
# the end of processing of the vendor's top-level directory, i.e. after
# freertos_bsp/openamp/libmetal are already defined later in that file.
# CMake refuses to create a new source/binary directory during deferred
# execution ("Subdirectories may not be created during deferred
# execution"), so the deferred call here is a plain include() of
# actuation_module/freertos_x5h/CMakeLists.txt -- which runs the file's
# commands directly in the vendor's own (already fully set up) directory
# scope, rather than add_subdirectory()'ing it into a new child scope.
# Consequently actuation_module/freertos_x5h/CMakeLists.txt does not call
# project() itself (this file calls enable_language(CXX) up front instead,
# immediately/non-deferred, since C and ASM are already enabled by the
# vendor's own project() by this point) and uses CMAKE_CURRENT_LIST_DIR
# (tracks the currently-processing FILE, even under a plain include()) for
# its own paths rather than CMAKE_CURRENT_SOURCE_DIR (tracks the enclosing
# DIRECTORY scope, which remains the vendor's directory throughout since no
# new directory scope is created here).
enable_language(CXX)

# cmake_language(DEFER)'s CALL arguments are re-expanded at the moment the
# deferred call actually runs, not when this DEFER statement registers it
# (confirmed empirically: using ${CMAKE_CURRENT_LIST_DIR} directly in the
# CALL arguments below resolved, at execution time, to the vendor's own
# directory instead of this file's directory, since CMAKE_CURRENT_LIST_DIR
# is contextual and had moved on by then). Capture the path into a plain
# variable now, while CMAKE_CURRENT_LIST_DIR is still this file's own
# directory -- a plain variable's value does not change with context, so
# re-expanding it later still yields the correct, already-computed path.
set(X5H_ACTUATION_CMAKELISTS "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt")

cmake_language(DEFER CALL include "${X5H_ACTUATION_CMAKELISTS}")
