# Copyright (c) 2026, Arm Limited and contributors.
# SPDX-License-Identifier: Apache-2.0
#
# FreeRTOS on Renesas R-Car X5H (Cortex-R52, "Ironhide" board, Core1) build
# entry point.
#
# Unlike freertos_s32z2, every input here is public: the R-Car BSP is a git
# submodule at rcar_bsp/ (rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52), and the
# arm-none-eabi toolchain is fetched by scripts/fetch-toolchain.sh -- no NXP
# Flexnet downloads, no S32 Design Studio, no environment variables to export.
#
# Preferred path from the repository root:
#   ./build.sh --platform freertos-x5h -d build/freertos-x5h
#
# Unlike freertos_s32z2, this file is NOT the top-level -S argument CMake is
# invoked with, and it is not add_subdirectory()'d either. build.sh points
# -S directly at the vendor's own rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52
# directory (matching Task 2's scripts/build-bsp-rpmsg-sample.sh) and passes
# -DCMAKE_PROJECT_INCLUDE=cmake/inject_actuation_x5h.cmake, which plain
# include()s this file -- deferred until after the vendor tree's
# freertos_bsp/openamp/libmetal targets already exist. See
# cmake/inject_actuation_x5h.cmake for the full rationale: in short, the
# vendor's own CMakeLists.txt derives its BSP_DIR/FREERTOS_DIR from
# CMAKE_SOURCE_DIR, which tracks the whole invocation's actual -S argument
# and cannot be overridden from a child scope, so this file cannot simply
# add_subdirectory() the vendor tree from underneath itself the way
# freertos_s32z2/CMakeLists.txt pulls in its own dependencies -- and
# cmake_language(DEFER ...), the mechanism used to delay pulling this file in
# until freertos_bsp/openamp/libmetal exist, refuses to create a new
# subdirectory scope at all ("Subdirectories may not be created during
# deferred execution"), which is why this file is include()'d in place
# rather than add_subdirectory()'d. Consequently this file does NOT call
# project() itself (cmake/inject_actuation_x5h.cmake calls
# enable_language(CXX) up front instead) and uses CMAKE_CURRENT_LIST_DIR,
# not CMAKE_CURRENT_SOURCE_DIR, for its own paths -- CMAKE_CURRENT_LIST_DIR
# tracks the currently-processing FILE even under a plain include(), whereas
# CMAKE_CURRENT_SOURCE_DIR tracks the enclosing DIRECTORY scope, which stays
# the vendor's own directory throughout since include() creates no new one.
#
# This is a Task 3 scaffold: it boots the BSP, prints a boot banner, and
# stops there. It does not start the actuation module (Task 4), bring up
# lwIP for real (Task 6), or wire an RPMsg transport endpoint (Task 7). The
# ELF's memory layout is frozen and verified by
# scripts/check-elf-contract.sh: .text at 0x11600000 (the Core1 vram2 slot)
# and .resource_table at 0x96650000 (the fixed remoteproc resource-table
# carveout), both produced by the BSP's own unmodified linker scripts
# referenced below (see AUDIT.md Section 5).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mirror the other FreeRTOS targets' anchor variables so a later task's
# actuation_core.cmake / autoware component .cmake files resolve their
# relative paths the same way. Not wired in yet -- Task 4 adds the actuation
# module itself and starts using these. CMAKE_CURRENT_LIST_DIR, not
# CMAKE_CURRENT_SOURCE_DIR -- see the header comment above.
set(ACTUATION_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
set(REPO_ROOT ${ACTUATION_DIR}/..)

# ---- R-Car FreeRTOS BSP (public git submodule) ----
# BOARD/RAM_REGION/MFIS_CHAN/UART_ID/CACHE/ENABLE_OPENAMP are NOT set here:
# by the time this file runs (deferred, see the header comment above), the
# vendor's own CMakeLists.txt has already consumed them at the top of ITS
# file. build.sh passes them as -D cache entries on the cmake command line
# instead (see build_freertos_x5h()), the same way Task 2's
# scripts/build-bsp-rpmsg-sample.sh does.
set(RCAR_BSP_DEMO_DIR ${CMAKE_CURRENT_LIST_DIR}/rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52)
if(NOT EXISTS "${RCAR_BSP_DEMO_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "R-Car FreeRTOS BSP not found at ${RCAR_BSP_DEMO_DIR}. Initialise the "
        "submodule with 'git submodule update --init "
        "actuation_module/freertos_x5h/rcar_bsp'.")
endif()

# ---- RPMsg resource-table / platform glue ----
# Structural decision for Task 7 (RPMsg transport endpoint): the three files
# below are the BSP sample's own trio (sample_apps/rpmsg_sample/), the
# correct variant per AUDIT.md. This is a DIFFERENT trio, despite identical
# file names, from drivers/virtio/{platform_rcar.c,remoteproc_rcar.c,
# rsc_table.c}, which freertos_bsp compiles into itself whenever
# ENABLE_OPENAMP=1. Both trios define the same global symbol names (e.g.
# platform_init, platform_poll); this is not a link conflict in practice --
# our copies are linked as regular (non-archive) object files, so they
# resolve those symbols before the linker ever needs to pull the shadowed
# copies out of libfreertos_bsp.a. (Task 2's build-bsp-rpmsg-sample.sh already
# builds this exact combination -- ENABLE_OPENAMP=1 plus rpmsg_sample --
# successfully today.)
#
# Task 7 swaps this one variable to local, actively-maintained copies; no
# other change in this file should be required.
set(X5H_BSP_RPMSG_SOURCES
    ${RCAR_BSP_DEMO_DIR}/sample_apps/rpmsg_sample/platform_rcar.c
    ${RCAR_BSP_DEMO_DIR}/sample_apps/rpmsg_sample/remoteproc_rcar.c
    ${RCAR_BSP_DEMO_DIR}/sample_apps/rpmsg_sample/rsc_table.c
)

# ${CMAKE_CURRENT_LIST_DIR}/freertos_main.cpp, not a bare relative
# "freertos_main.cpp": a plain include() (see the header comment above)
# resolves relative source paths against CMAKE_CURRENT_SOURCE_DIR, which is
# the vendor's own directory here, not this one.
add_executable(actuation_x5h
    ${CMAKE_CURRENT_LIST_DIR}/freertos_main.cpp
    ${X5H_BSP_RPMSG_SOURCES}
)

set_target_properties(actuation_x5h PROPERTIES
    SUFFIX ".elf"
    # Explicit, rather than relying on the default: pins the ELF's output
    # location to the top-level build dir (build/freertos-x5h/actuation_x5h.elf)
    # regardless of how this file was reached into the build.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_compile_definitions(actuation_x5h PRIVATE
    PLATFORM_FREERTOS=1
    PLATFORM_FREERTOS_X5H=1
    MFIS_CHAN=${MFIS_CHAN}
)

# Unlike freertos_s32z2, no target_compile_options()/target_link_libraries(dummy)/
# base target_link_options() here for the Cortex-R52 ABI flags, the vram2
# linker script, or the dummy weak-stub library: because CMakeLists.txt runs
# in the vendor's own directory scope (include()'d, not add_subdirectory()'d
# -- see the header comment above), we already inherit its directory-scoped
# add_compile_options() (-ffreestanding -mcpu=cortex-r52 ... -T is NOT part of
# that, see below), link_libraries(dummy), and add_link_options() (-lnosys
# --specs=nosys.specs -L.../common/linker -T .../lscript_vram2.ld
# -Wl,--no-warn-rwx-segments) the same way any other target defined later in
# that same file would. Repeating any of those here would pass the same
# linker script to `ld` twice, which it rejects outright ("linker script
# file ... appears multiple times") -- confirmed by hitting exactly that
# error while developing this target.
#
# PRIVATE include dirs replicated from sample_apps/rpmsg_sample/CMakeLists.txt's
# add_rpmsg_app() macro. These three are PRIVATE to freertos_bsp itself in the
# vendor tree, so they do not propagate to us via target_link_libraries.
target_include_directories(actuation_x5h PRIVATE
    ${ACTUATION_DIR}/include
    ${ACTUATION_DIR}/include/platform/freertos/x5h
    ${RCAR_BSP_DEMO_DIR}
    ${RCAR_BSP_DEMO_DIR}/CMSIS_5
    ${RCAR_BSP_DEMO_DIR}/CMSIS_5/CMSIS/Core_R/Include
)

target_link_libraries(actuation_x5h PRIVATE
    freertos_bsp
    openamp
    libmetal
    m
)

# The resource-table linker script (places .resource_table into the fixed
# remote_proc_rsc_table_1 region at 0x96650000) and the Map file: the only
# two link inputs NOT already provided by the inherited directory-scoped
# add_link_options() above. Reused byte-for-byte, unmodified, from the BSP
# -- see AUDIT.md Section 5.
target_link_options(actuation_x5h PRIVATE
    -T${RCAR_BSP_DEMO_DIR}/common/linker/lscript_rsc_table_vram2.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/actuation_x5h.map
)
