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
# invoked with, and it is not add_subdirectory()'d either: build.sh points
# -S at the vendor's own rcar_bsp/.../CMakeLists.txt and reaches this file
# via -DCMAKE_PROJECT_INCLUDE=cmake/inject_actuation_x5h.cmake, deferred with
# cmake_language(DEFER CALL include ...). Read the header comment in
# cmake/inject_actuation_x5h.cmake FIRST -- it is the single source of truth
# for why this indirection exists, why it must be a plain include() rather
# than add_subdirectory(), and the ordering invariant DEFER protects. Do not
# duplicate that rationale here; extend it there if it changes.
#
# Two consequences that follow directly from running via that plain
# include() (not a sibling add_subdirectory() scope) and are worth keeping
# visible at the point they matter, rather than only in the other file:
#   - This file does not call project() itself (the injected file calls
#     enable_language(CXX) up front, non-deferred).
#   - Self-relative paths below use CMAKE_CURRENT_LIST_DIR (tracks the
#     currently-processing FILE, even under a plain include()), never
#     CMAKE_CURRENT_SOURCE_DIR (tracks the enclosing DIRECTORY scope, which
#     stays the vendor's own directory here since include() opens no new
#     one).
#
# Task 3 built the boot scaffold (boots the BSP, prints a boot banner,
# nothing else linked in). Task 4 (below, "Task 4: full-link...") adds the
# actuation module, CycloneDDS, and lwIP to that same executable so
# everything compiles and links inside the frozen memory budget -- it does
# not bring up lwIP for real (Task 6) or wire an RPMsg transport endpoint
# (Task 7); freertos_main.cpp's main_task() does not call into any of it
# yet. The ELF's memory layout is frozen and verified by
# scripts/check-elf-contract.sh: .text at 0x11600000 (the Core1 vram2 slot)
# and .resource_table at 0x96650000 (the fixed remoteproc resource-table
# carveout), both produced by the BSP's own unmodified linker scripts
# referenced below (see AUDIT.md Section 5). Total slot usage is verified
# against the 10 MiB budget by scripts/check-image-budget.sh.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mirror the other FreeRTOS targets' anchor variables so a later task's
# actuation_core.cmake / autoware component .cmake files resolve their
# relative paths the same way. Not wired in yet -- Task 4 adds the actuation
# module itself and starts using these. CMAKE_CURRENT_LIST_DIR, not
# CMAKE_CURRENT_SOURCE_DIR -- see the header comment above.
#
# X5H_-prefixed: this set() runs in the VENDOR's directory scope (see the
# header comment above), so an unprefixed name here would leak into that
# scope's variable namespace -- a growing collision surface as Tasks 4-8 add
# more variables. X5H_BSP_RPMSG_SOURCES below already followed this
# convention; the rest are brought in line with it here.
set(X5H_ACTUATION_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
set(X5H_REPO_ROOT ${X5H_ACTUATION_DIR}/..)

# ---- R-Car FreeRTOS BSP (public git submodule) ----
# BOARD/RAM_REGION/MFIS_CHAN/UART_ID/CACHE/ENABLE_OPENAMP are NOT set here:
# by the time this file runs (deferred, see the header comment above), the
# vendor's own CMakeLists.txt has already consumed them at the top of ITS
# file. build.sh passes them as -D cache entries on the cmake command line
# instead (see build_freertos_x5h()), the same way Task 2's
# scripts/build-bsp-rpmsg-sample.sh does.
set(X5H_RCAR_BSP_DEMO_DIR ${CMAKE_CURRENT_LIST_DIR}/rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52)
if(NOT EXISTS "${X5H_RCAR_BSP_DEMO_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "R-Car FreeRTOS BSP not found at ${X5H_RCAR_BSP_DEMO_DIR}. Initialise "
        "the submodule with 'git submodule update --init "
        "actuation_module/freertos_x5h/rcar_bsp'.")
endif()

# X5H_RCAR_BSP_DEMO_DIR is derived from CMAKE_CURRENT_LIST_DIR (this file's
# own location); the vendor's own BSP_DIR (used inside freertos_bsp and by
# the linker scripts we reference below) is derived from CMAKE_SOURCE_DIR
# (the cmake invocation's -S argument, see the header comment above). Under
# build.sh these always agree, since build.sh points -S directly at
# X5H_RCAR_BSP_DEMO_DIR. But this file can also be reached by a hand-run
# `cmake -S <some other BSP checkout> -DCMAKE_PROJECT_INCLUDE=<this dir>/...`,
# in which case they would silently disagree: rsc_table.c and the linker
# scripts would come from X5H_RCAR_BSP_DEMO_DIR while freertos_bsp/libmetal
# and the rest of the compile/link flags would come from the other
# checkout's CMAKE_SOURCE_DIR, with no build error pointing at the mismatch.
if(NOT X5H_RCAR_BSP_DEMO_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR
        "actuation_x5h must be configured with -S pointed at "
        "${X5H_RCAR_BSP_DEMO_DIR} (got CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}). "
        "Build via build.sh's build_freertos_x5h(), which sets -S correctly.")
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
    ${X5H_RCAR_BSP_DEMO_DIR}/sample_apps/rpmsg_sample/platform_rcar.c
    ${X5H_RCAR_BSP_DEMO_DIR}/sample_apps/rpmsg_sample/remoteproc_rcar.c
    ${X5H_RCAR_BSP_DEMO_DIR}/sample_apps/rpmsg_sample/rsc_table.c
)

# ${CMAKE_CURRENT_LIST_DIR}/freertos_main.cpp, not a bare relative
# "freertos_main.cpp": a plain include() (see the header comment above)
# resolves relative source paths against CMAKE_CURRENT_SOURCE_DIR, which is
# the vendor's own directory here, not this one.
add_executable(actuation_x5h
    ${CMAKE_CURRENT_LIST_DIR}/freertos_main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/newlib_stubs.c
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
    ${X5H_ACTUATION_DIR}/include
    ${X5H_ACTUATION_DIR}/include/platform/freertos/x5h
    ${X5H_RCAR_BSP_DEMO_DIR}
    ${X5H_RCAR_BSP_DEMO_DIR}/CMSIS_5
    ${X5H_RCAR_BSP_DEMO_DIR}/CMSIS_5/CMSIS/Core_R/Include
)

# ---- FPU context for every task (review-round fix; see CHANGELOG below) ----
#
# CORRECTED UNDERSTANDING (this replaces a false claim made when this
# section was first written -- see cdds_freertos_compat.h's and
# platform/freertos/x5h/pthread.h's own header comments, both corrected
# alongside this change): configUSE_TASK_FPU_SUPPORT is NOT defined anywhere
# in rcar_bsp/.../FreeRTOSConfig.h, so it silently took FreeRTOS.h's own
# #ifndef default of 1 (lazy, opt-in FPU context: pxPortInitialiseStack()
# gives a new task NO FPU register space at all, and the task must call
# vPortTaskUsesFPU() itself, before touching any FP instruction, to get one
# -- see common/ARM_CR52/port.c). Nothing anywhere in this codebase calls
# vPortTaskUsesFPU(), so under the default, EVERY task -- the Eigen/MPC
# controller math, every CycloneDDS thread, float printf -- runs with no
# preserved FPU context: any interrupt or context switch during FP work is
# free to corrupt its registers, silently, with no crash and no diagnostic.
# This is exactly the "for free" claim the original comments made, inverted.
#
# Fix: force configUSE_TASK_FPU_SUPPORT=2 here, which changes
# pxPortInitialiseStack()'s behaviour (in the SAME port.c) to unconditionally
# reserve FPU register space and set ulPortTaskHasFPUContext = pdTRUE at
# task-creation time for every task, with no per-task opt-in call required.
# The context-switch save/restore assembly itself is unchanged either way --
# only whether ulPortTaskHasFPUContext starts (and stays) true is different.
#
# Rejected alternative: enumerate every FP-using call site and insert
# vPortTaskUsesFPU() calls (a "trampoline" over xTaskCreate, mirroring
# S32Z2's xTaskCreateFpu()). Rejected for safety-island math specifically:
# an exhaustive enumeration is only ever as good as the last audit, and a
# single missed call site (a future thread, a library call that starts using
# FP internally) reproduces this exact silent-corruption bug. Forcing =2
# instead makes every task correct by construction, with no enumeration to
# keep in sync.
#
# freertos_bsp (declared by rcar_bsp's own CMakeLists.txt, NOT edited here)
# is the single CMake target both tasks.c and port.c compile into (see
# ${kernel_source}/${bsp_source} in that vendor file) -- reachable from our
# own deferred file because our cmake_language(DEFER CALL include ...)
# execution runs strictly after that add_library(freertos_bsp ...) call
# completes (same directory scope, later in processing order; see the
# header comment at the top of this file for the DEFER mechanism itself).
# This reaches both files without ever editing vendor/rcar_bsp source.
#
# Verified safe to flip from 1 to 2: vPortTaskUsesFPU()'s only DEFINITION in
# the whole rcar_bsp/ tree is inside port.c's own
# `#if (configUSE_TASK_FPU_SUPPORT == 1)` block; grepping the entire
# rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/ tree shows nothing else anywhere
# calls it, so that definition disappearing under =2 creates no dangling
# reference.
#
# portFPU_REGISTER_WORDS=65: a second, previously-latent gap this same fix
# uncovered at compile time (a genuine build error, not a guess -- this
# value is verified, not assumed; see below). common/ARM_CR52/port.c's own
# `#elif (configUSE_TASK_FPU_SUPPORT == 2)` branch (the one this setting now
# activates) references portFPU_REGISTER_WORDS to size the stack-frame
# reservation for the FPU register block, but -- unlike this file's two
# sibling ports in the very same rcar_bsp tree,
# Source/portable/GCC/ARM_CR5/port.c (`#define portFPU_REGISTER_WORDS
# ((16*2)+1)`, D16-only) and Source/portable/GCC/ARM_CA9/port.c (`#define
# portFPU_REGISTER_WORDS ((32*2)+1)`, D32) -- common/ARM_CR52/port.c never
# defines it itself anywhere. Confirmed by grepping the whole demo tree: the
# only two references are the two USES already quoted above; nothing
# defines it. This BSP's own =2 path was never previously exercised (nothing
# in this vendor tree sets configUSE_TASK_FPU_SUPPORT=2), so this gap never
# surfaced before.
#
# 65, not 33: this build's -mfpu=neon-fp-armv8 implies the D32 (32 double
# registers) FPU variant -- confirmed via CMSIS_5/.../core_cr52.h, which
# sets `#define __FPU_D32 1U` whenever __FPU_PRESENT is set for this
# configuration -- and this port's own context-switch assembly (port.c's
# portSAVE_CONTEXT/portRESTORE_CONTEXT macros) unconditionally pushes/pops
# D16-D31 in addition to D0-D15+FPSCR whenever __FPU_D32 is set (the same
# gate ARM_CA9, also a D32 port, uses). So this target needs the ARM_CA9
# value -- (32 D-regs * 2 words/D-reg) + 1 FPSCR word = 65 -- not ARM_CR5's
# 33 (a D16-only value that would under-reserve the stack frame by 32
# words here and corrupt whatever sits below it on first use).
#
# Supplied via -D on the same freertos_bsp target as
# configUSE_TASK_FPU_SUPPORT itself, not by editing port.c (vendor file,
# off-limits): the preprocessor cannot tell a #define supplied on the
# command line from one written in the source, so this is the same
# reach-into-a-vendor-target-from-our-own-file mechanism already used
# above, extended to cover the one additional macro this vendor port left
# out of its own (previously dead) =2 code path.
target_compile_definitions(freertos_bsp PRIVATE
    configUSE_TASK_FPU_SUPPORT=2
    portFPU_REGISTER_WORDS=65
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
    -T${X5H_RCAR_BSP_DEMO_DIR}/common/linker/lscript_rsc_table_vram2.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/actuation_x5h.map
)

# ---- Task 4: full-link the actuation module + CycloneDDS + lwIP ----
#
# Network bring-up stays stubbed (freertos_main.cpp's lwip_bring_up_blocking
# weak stub, replaced for real in Task 6); this section only has to make
# everything below compile and link within the frozen 10 MiB Core1 slot
# budget (scripts/check-image-budget.sh). None of it is called from
# freertos_main.cpp's main_task() yet -- these are plain target_sources() on
# actuation_x5h (not members of a static archive), and the base link options
# above do not use --gc-sections, so every translation unit added here is
# unconditionally included in the link regardless of whether anything calls
# into it. That is enough to prove the link fits; it does not run any of
# this code.

# ---- Configurable values ----
# Mirrors freertos_s32z2's CONFIG_* CACHE knobs so the shared
# platform_config.h / common/dds/config.hpp work unchanged. Values differ
# where the task brief specifies x5h-specific ones (DDS domain 2, the
# 172.16.52.2 network interface Task 6's RPMsg netif will claim); the rest
# (log level, control command mode) reuse freertos_s32z2's own defaults,
# which are themselves generic, not S32Z2-specific.
set(CONFIG_LOG_LEVEL 1 CACHE STRING "Log level (0=off, 1=info, 2=debug)")
set(CONFIG_LOG_THROTTLE_RATE 3 CACHE STRING "Log throttle rate in seconds")
set(CONFIG_DDS_DOMAIN_ID 2 CACHE STRING "DDS domain ID")
set(CONFIG_DDS_NETWORK_INTERFACE "172.16.52.2" CACHE STRING "DDS network interface name or IP")
set(CONFIG_DDS_LOG_LEVEL 0 CACHE STRING "DDS log level (0=off, 1=warn, 2=debug)")
# Controller node thread stack. autoware_mpc_lateral_controller's Eigen-heavy
# control cycle overflowed a 256 KiB stack by a few hundred bytes on S32Z2
# (same component, same Cortex-R52 + NEON ABI); 512 KiB gives ~2x margin.
# Unlike S32Z2's node_stack_in_sram.ld placement, x5h's K_THREAD_STACK_DEFINE
# is a plain aligned buffer (see include/platform/freertos/freertos_threading.h)
# that lands wherever the linker script puts .bss -- inside the same 10 MiB
# slot window this section's own link is budgeted against.
set(CONFIG_THREAD_STACK_SIZE 524288 CACHE STRING "Thread stack size in bytes")
set(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX 0 CACHE STRING "DHCP delay")
set(CONFIG_ENABLE_SNTP OFF CACHE BOOL "Enable SNTP time sync")

set(CONFIG_CONTROL_CMD_OUTPUT_MODE "DDS_ONLY" CACHE STRING "Control command output mode: DDS_ONLY, CAN_ONLY, DDS_AND_CAN")
set_property(CACHE CONFIG_CONTROL_CMD_OUTPUT_MODE PROPERTY STRINGS "DDS_ONLY" "CAN_ONLY" "DDS_AND_CAN")

set(CONFIG_CONTROL_CMD_OUTPUT_DDS_ONLY 0)
set(CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY 0)
set(CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN 0)
set(CONFIG_CONTROL_CMD_CAN_OUTPUT 0)
set(CONFIG_CONTROL_CMD_DDS_OUTPUT 0)

if(CONFIG_CONTROL_CMD_OUTPUT_MODE STREQUAL "DDS_ONLY")
    set(CONFIG_CONTROL_CMD_OUTPUT_DDS_ONLY 1)
    set(CONFIG_CONTROL_CMD_DDS_OUTPUT 1)
elseif(CONFIG_CONTROL_CMD_OUTPUT_MODE STREQUAL "CAN_ONLY")
    set(CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY 1)
    set(CONFIG_CONTROL_CMD_CAN_OUTPUT 1)
elseif(CONFIG_CONTROL_CMD_OUTPUT_MODE STREQUAL "DDS_AND_CAN")
    set(CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN 1)
    set(CONFIG_CONTROL_CMD_CAN_OUTPUT 1)
    set(CONFIG_CONTROL_CMD_DDS_OUTPUT 1)
else()
    message(FATAL_ERROR "Unsupported CONFIG_CONTROL_CMD_OUTPUT_MODE=${CONFIG_CONTROL_CMD_OUTPUT_MODE}. Use DDS_ONLY, CAN_ONLY, or DDS_AND_CAN.")
endif()

# ${X5H_ACTUATION_DIR}, not CMAKE_CURRENT_SOURCE_DIR: see the header comment
# at the top of this file -- CMAKE_CURRENT_SOURCE_DIR is the vendor's own
# directory under a plain include().
configure_file(
    ${X5H_ACTUATION_DIR}/include/platform/freertos/freertos_config_generated.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/freertos_config_generated.h
    @ONLY
)
target_include_directories(actuation_x5h PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# ---- lwIP (our own submodule + lwip_port/, not NXP-supplied) ----
# LWIP_DIR (not an X5H_-prefixed name) is the variable name lwip/src/
# Filelists.cmake's own contract requires -- see that file's header comment.
set(LWIP_DIR ${CMAKE_CURRENT_LIST_DIR}/lwip)
include(${LWIP_DIR}/src/Filelists.cmake)

target_sources(actuation_x5h PRIVATE
    ${lwipcore_SRCS}
    ${lwipcore4_SRCS}
    ${lwipapi_SRCS}
    # Cherry-picked from lwipnetif_SRCS: we only need the Ethernet netif
    # (Task 6's RPMsg-netif calls into ethernet_input/ethernet_output).
    # bridgeif.c/bridgeif_fdb.c and (LWIP_EXCLUDE_SLIPIF-gated) slipif.c
    # are not used by this transport and are left out rather than pulled in
    # and never called.
    ${LWIP_DIR}/src/netif/ethernet.c
    ${CMAKE_CURRENT_LIST_DIR}/lwip_port/sys_arch.c
)
target_include_directories(actuation_x5h PRIVATE
    ${LWIP_DIR}/src/include
    # Provides both lwipopts.h and arch/{cc.h,sys_arch.h} (lwip/arch.h
    # #includes "arch/cc.h" and lwip/sys.h #includes "arch/sys_arch.h";
    # both resolve as this directory's arch/ subdirectory).
    ${CMAKE_CURRENT_LIST_DIR}/lwip_port
)

# lwipopts.h sets LWIP_TIMEVAL_PRIVATE=0 (lwip/sockets.h then expects some
# other header to provide `struct timeval`) and lwip/sockets.h's own
# `#ifndef FD_SET` guard expects newlib's fd_set to already be visible if it
# is going to be used instead of lwip's own -- exactly the same
# fd_set/struct-timeval situation scripts/build-cdds-target.sh's own
# CMAKE_C_FLAGS already documents and fixes for CycloneDDS's *standalone*
# cross-build. lwIP's own sources (src/api/sockets.c and friends) need the
# identical fix here, since they are compiled directly into actuation_x5h in
# this main build, not through that separate script.
set_source_files_properties(
    ${lwipapi_SRCS}
    PROPERTIES COMPILE_OPTIONS "-include;sys/select.h"
)

# ---- Pre-built CycloneDDS target library ----
set(CDDS_HOST_PREFIX   ${X5H_REPO_ROOT}/build/cyclonedds_host/out CACHE PATH "CycloneDDS host tools install prefix")
set(CDDS_TARGET_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/cdds_target_out CACHE PATH "CycloneDDS x5h target library install prefix")

if(NOT EXISTS ${CDDS_HOST_PREFIX}/bin/idlc)
    message(FATAL_ERROR
        "Host idlc not found at ${CDDS_HOST_PREFIX}/bin/idlc. "
        "Run ./build.sh --platform freertos-x5h first, or pass CDDS_HOST_PREFIX.")
endif()
if(NOT EXISTS ${CDDS_TARGET_PREFIX}/lib/libddsc.a)
    message(FATAL_ERROR
        "Cross-built CycloneDDS not found at ${CDDS_TARGET_PREFIX}/lib/libddsc.a. "
        "Run ./build.sh --platform freertos-x5h first (it calls "
        "scripts/build-cdds-target.sh), or pass CDDS_TARGET_PREFIX.")
endif()

add_library(cdds STATIC IMPORTED GLOBAL)
set_target_properties(cdds PROPERTIES
    IMPORTED_LOCATION ${CDDS_TARGET_PREFIX}/lib/libddsc.a)

set(CYCLONEDDS_INCLUDE_DIR ${CDDS_TARGET_PREFIX}/include)

# Expose host idlc on PATH so autoware_msgs's configure-time IDL generation
# finds it.
set(ENV{PATH} "${CDDS_HOST_PREFIX}/bin:$ENV{PATH}")
# Only prepend a ":" separator when there is an existing value to separate
# from (review round 1 fix): when ENV{LD_LIBRARY_PATH} is unset/empty (the
# common case for a fresh shell), the old unconditional
# "${CDDS_HOST_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}" left a trailing colon,
# which the dynamic loader treats as an empty path component meaning "the
# current working directory" -- silently adding CWD to the search path for
# every process this configure-time environment propagates to.
if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
    set(ENV{LD_LIBRARY_PATH} "${CDDS_HOST_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")
else()
    set(ENV{LD_LIBRARY_PATH} "${CDDS_HOST_PREFIX}/lib")
endif()
set(CycloneDDS_DIR ${CDDS_HOST_PREFIX}/lib/cmake/CycloneDDS)

# ---- Eigen3 (host package, header-only) ----
find_package(Eigen3 REQUIRED)
find_program(PATCH_EXECUTABLE patch REQUIRED)

# Eigen NEON psincos int32_t==long fix. The R-Car BSP's inherited compile
# flags enable -mfpu=neon-fp-armv8 for every target in this project
# (add_compile_options(), see the header comment at the top of this file),
# so Eigen 3.4's NEON packet specializations are active for actuation_x5h
# exactly as they were for freertos_s32z2 on the same Cortex-R52 + NEON
# ABI, linking the same autoware_mpc_lateral_controller component --
# psincos_float's `int` scratch buffer (vs. arm-none-eabi's int32_t==long)
# is a COMPILE error, not a latent runtime bug, so this is not optional
# once NEON is active. See vendor_patched/eigen-psincos-int32.patch's own
# header comment for the full mechanism (copied verbatim from
# freertos_s32z2/vendor_patched/ -- the bug and fix are toolchain/Eigen
# properties, not S32Z2-specific, so this is a new copy, not a reference
# into that target's directory). EIGEN_FAST_MATH=0 then keeps trig
# DISPATCH scalar so the now-compilable vectorised psincos path is never
# actually invoked at runtime (accurate libm trig on a safety island,
# NEON still vectorises the matrix hot path).
#
# target_compile_definitions (not the directory-scoped add_compile_
# definitions this used to be, review round 1 fix): this directory also
# holds the vendor's own rcar_bsp sample/BSP targets (e.g. freertos_bsp,
# dummy -- see rcar_bsp/.../CMakeLists.txt's add_library/add_executable
# calls processed earlier in this same directory scope), none of which use
# Eigen at all; add_compile_definitions() would have leaked EIGEN_FAST_MATH
# into their command lines too for no reason.
target_compile_definitions(actuation_x5h PRIVATE EIGEN_FAST_MATH=0)
set(_x5h_eigen_patched_dir ${CMAKE_CURRENT_BINARY_DIR}/eigen-patched)
set(_x5h_eigen_patch ${CMAKE_CURRENT_LIST_DIR}/vendor_patched/eigen-psincos-int32.patch)
# Sentinel keyed to the patch file's own hash (review round 1 fix), not
# just its bare existence: the previous ${_x5h_eigen_patched_dir}/.patched
# check would silently keep reusing a stale patched Eigen tree forever if
# eigen-psincos-int32.patch were ever edited (e.g. widened, or corrected)
# without also deleting the build directory -- a `patch` re-apply against
# the same already-patched tree would just fail (already-patched hunks), but
# nothing here would prompt one to look, since the target still builds fine
# against the old copy.
file(SHA256 ${_x5h_eigen_patch} _x5h_eigen_patch_hash)
set(_x5h_eigen_sentinel ${_x5h_eigen_patched_dir}/.patched-${_x5h_eigen_patch_hash})
if(NOT EXISTS ${_x5h_eigen_sentinel})
    file(REMOVE_RECURSE ${_x5h_eigen_patched_dir})
    file(COPY ${EIGEN3_INCLUDE_DIR}/Eigen DESTINATION ${_x5h_eigen_patched_dir})
    execute_process(
        COMMAND ${PATCH_EXECUTABLE} -p1 -d ${_x5h_eigen_patched_dir}
        INPUT_FILE ${_x5h_eigen_patch}
        RESULT_VARIABLE _x5h_eigen_patch_rc)
    if(NOT _x5h_eigen_patch_rc EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${_x5h_eigen_patch} to Eigen at ${_x5h_eigen_patched_dir}")
    endif()
    file(WRITE ${_x5h_eigen_sentinel} "")
endif()
target_include_directories(actuation_x5h BEFORE PRIVATE ${_x5h_eigen_patched_dir})

# ---- Autoware Messages (IDL -> C via host idlc) ----
# NOT add_subdirectory(.../autoware_msgs) and not that file's own project():
# both are illegal inside cmake_language(DEFER CALL)'d execution
# ("Subdirectories may not be created during deferred execution" -- see
# cmake/inject_actuation_x5h.cmake's own header comment for why this whole
# file runs deferred in the first place, so actuation_x5h inherits the
# vendor's Cortex-R52 compile/link options). Instead, reproduce
# actuation_module/src/autoware/autoware_msgs/CMakeLists.txt's own IDL -> C
# generation and library commands directly here, targeting the same source
# tree, so no new directory/binary scope is ever created. CycloneDDS_DIR
# already points at the HOST prefix (set above, alongside the PATH/
# LD_LIBRARY_PATH exports), so find_package(CycloneDDS) below resolves the
# host idlc and its architecture-agnostic CMake package -- the same
# HOST-prefix pattern freertos_s32z2/CMakeLists.txt already uses ahead of its
# own add_subdirectory(autoware_msgs) call.
set(AUTOWARE_MSGS_DIR ${X5H_ACTUATION_DIR}/src/autoware/autoware_msgs)
set(AUTOWARE_MSGS_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)
file(MAKE_DIRECTORY ${AUTOWARE_MSGS_BINARY_DIR})

# idlc is a host tool (architecture-agnostic); temporarily unset
# CMAKE_SIZEOF_VOID_P so find_package(CycloneDDS) does not reject the host
# package's pointer width against our target toolchain's, exactly mirroring
# autoware_msgs/CMakeLists.txt's own workaround.
set(_x5h_saved_sizeof_void_p ${CMAKE_SIZEOF_VOID_P})
unset(CMAKE_SIZEOF_VOID_P)
find_package(CycloneDDS REQUIRED COMPONENTS ddsc idlc)
set(CMAKE_SIZEOF_VOID_P ${_x5h_saved_sizeof_void_p})

file(GLOB AUTOWARE_MSGS_IDL_FILES ${AUTOWARE_MSGS_DIR}/msg/*.idl)
set(AUTOWARE_MSGS_SOURCES "")
set(AUTOWARE_MSGS_HEADERS "")
foreach(_x5h_idl ${AUTOWARE_MSGS_IDL_FILES})
    get_filename_component(_x5h_idl_name ${_x5h_idl} NAME_WE)
    list(APPEND AUTOWARE_MSGS_SOURCES ${AUTOWARE_MSGS_BINARY_DIR}/${_x5h_idl_name}.c)
    list(APPEND AUTOWARE_MSGS_HEADERS ${AUTOWARE_MSGS_BINARY_DIR}/${_x5h_idl_name}.h)
endforeach()

# Absolute path to idlc (CDDS_HOST_PREFIX/bin/idlc, already guarded to exist
# at line 354 above), not a bare `idlc` relying on PATH: this custom command
# runs later, inside `cmake --build`'s own generated Makefiles/ninja rules,
# a separate process tree from configure-time -- the set(ENV{PATH} ...) a
# few lines up only affects the configure-time process (see that line's own
# comment), so a bare name here would only resolve when build.sh's shell
# happens to still have PATH exported (as it does today via
# build_freertos_x5h()'s own export), silently breaking any `cmake --build`
# invoked standalone (e.g. from an IDE, or a re-run without build.sh).
add_custom_command(
    OUTPUT ${AUTOWARE_MSGS_SOURCES} ${AUTOWARE_MSGS_HEADERS}
    COMMAND /bin/sh -c "find ${AUTOWARE_MSGS_DIR}/msg/ -maxdepth 1 -name '*.idl' -exec ${CDDS_HOST_PREFIX}/bin/idlc {} \\; && sed -i -- 's|/\\(.\\+\\)/\\(.*\\.idl\\)|\\2|' *.h *.c"
    WORKING_DIRECTORY ${AUTOWARE_MSGS_BINARY_DIR}
    DEPENDS ${AUTOWARE_MSGS_IDL_FILES}
    VERBATIM
)

add_library(autoware_msgs STATIC ${AUTOWARE_MSGS_SOURCES} ${AUTOWARE_MSGS_HEADERS})
set_target_properties(autoware_msgs PROPERTIES LINKER_LANGUAGE CXX)

get_target_property(_x5h_cdds_pkg_include_dir CycloneDDS::ddsc INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(autoware_msgs SYSTEM BEFORE PUBLIC
    ${_x5h_cdds_pkg_include_dir}
    ${AUTOWARE_MSGS_BINARY_DIR}
    ${AUTOWARE_MSGS_DIR}/include
)

# ---- Autoware controller core (shared with POSIX/S32Z2 builds) ----
include(${X5H_ACTUATION_DIR}/cmake/actuation_core.cmake)

target_sources(actuation_x5h PRIVATE
    ${X5H_ACTUATION_DIR}/src/main.cpp
    ${APP_SOURCES}
)

# Rename main() to actuation_main() only inside the Autoware app's main.cpp;
# freertos_main.cpp keeps its own main() (the FreeRTOS entry point invoked
# from startup code) and does not call actuation_main() yet -- wiring that
# call in is not this task's job (see this section's header comment).
set_source_files_properties(${X5H_ACTUATION_DIR}/src/main.cpp PROPERTIES
    COMPILE_DEFINITIONS "main=actuation_main"
)

# ---- Restore hosted libstdc++ for C++ sources ----
# rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/CMakeLists.txt's own
# add_compile_options() (see this file's own header comment) unconditionally
# passes -ffreestanding to every C *and* C++ source in this directory scope,
# with no per-language guard and no vendor-file we are allowed to edit to
# add one (this file is vendor BSP demo code, same category as the S32Z2
# vendor tree we must not touch). Under -ffreestanding, GCC sets
# __STDC_HOSTED__=0, and this toolchain's own
# arm-none-eabi/bits/c++config.h does `#define _GLIBCXX_HOSTED
# __STDC_HOSTED__` unconditionally -- so <vector>/<string>/<chrono>/<cmath>
# (transitively pulled in by autoware_universe_utils, main.cpp's
# common/clock/clock.hpp, etc.) all hit
# bits/requires_hosted.h's "#error This header is not available in
# freestanding mode." This is the actuation module's first-ever real C++
# compile on x5h (Task 3's scaffold never included these headers), so this
# is a newly-surfaced BSP quirk, not a regression.
#
# GCC's C frontend has a -fhosted flag that would undo -ffreestanding, but
# it is documented (and confirmed by direct experiment) as C/ObjC-only --
# passing it to g++ is silently ignored (with a warning) and does not
# restore hosted mode. What *does* work, confirmed by direct experiment:
# re-defining the __STDC_HOSTED__ builtin macro itself, after the
# directory-inherited -ffreestanding, so libstdc++ sees hosted=1 again. This
# only touches our own target's CXX command line (COMPILE_LANGUAGE:CXX)
# -- lwIP/CycloneDDS (C, genuinely freestanding-appropriate here) are
# unaffected.
target_compile_options(actuation_x5h PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-U__STDC_HOSTED__$<SEMICOLON>-D__STDC_HOSTED__=1>
)

# ---- Optimisation: C and C++ at -O2 ----
# Same reason as freertos_s32z2 (see that CMakeLists.txt's own comment):
# Eigen is pathologically slow at -O0 (~86s per solve there), so the
# control cycle needs -O2. Unlike S32Z2, this project's base link options
# do NOT use --gc-sections (confirmed by reading rcar_bsp's own
# add_link_options() call), so the ld 2.42 --gc-sections+-O2+-g segfault
# S32Z2 works around with a forced -g0 does not apply here -- the
# globally-inherited -g (see the header comment at the top of this file)
# is left alone.
#
# CORRECTED (review round 1): this used to be CXX-only, leaving this
# target's own C sources (lwip_port/sys_arch.c, newlib_stubs.c, the CDDS
# glue) at the vendor directory's implicit -O0 -- inconsistent with both
# this target's own C++ (-O2) and with CycloneDDS's own standalone build
# (build-cdds-target.sh's -DCMAKE_BUILD_TYPE=Release, i.e. -O3), and a
# needless budget/throughput/stack-usage cost for no benefit (none of these
# C sources need -O0 for debuggability reasons the C++ side doesn't already
# share). COMPILE_LANGUAGE genex now covers both C and CXX.
target_compile_options(actuation_x5h PRIVATE
    $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:-O2>
)

# ---- rsc_table.c exception: -fno-toplevel-reorder (review round 2 fix) ----
# The blanket C -O2 above regressed check-elf-contract.sh: it started
# reporting ".resource_table size is 0x0, expected 0x100". Root-caused by
# isolated reproduction (compile a minimal stand-in for this file's
# `static struct remote_resource_table __resource resources = {...};` at
# -O0 vs -O2 with the same freestanding/-ffunction-sections/-fdata-sections
# flags this target uses, then `objdump -h` the two .o files): at -O0 the
# .resource_table section is present at 0x100 bytes; at -O2 it is gone
# entirely from the object file, before the linker (which does not use
# --gc-sections here, see the -O2 comment above) ever sees it.
#
# The reason: `resources` is written by nobody and read by nobody in C --
# rsc_table.c's own init_resource_table() copies from the separate `const`
# `resources_data` instead, and `resources` exists only so its
# __attribute__((section(".resource_table"))) placement gives that output
# section its size; __resource_table_start/__resource_table_end (extern,
# provided by the linker script) are the only things anyone ever reads.
# A compiler has no way to know a section-placed-but-unreferenced static
# must survive -- that is exactly what __attribute__((used)) is for, but
# rsc_table.c is vendor code under rcar_bsp/, off limits to edit directly
# (see this file's header comment and AUDIT.md). -fno-toplevel-reorder is
# GCC's own documented escape hatch for precisely this: "output [top-level
# variables] in the same order as they appear in the input file... When
# this option is used, unreferenced static variables are not removed."
# Confirmed by the same isolated repro: adding -fno-toplevel-reorder at -O2
# restores the .resource_table section at exactly 0x100 bytes.
#
# Scoped to this ONE file, not the whole target: no other C source in this
# target relies on a section-attributed-but-C-unreferenced static, and
# -fno-toplevel-reorder also disables reordering/DCE of ordinary unreferenced
# statics tree-wide, which is not a tradeoff worth taking anywhere else.
set_source_files_properties(
    ${X5H_RCAR_BSP_DEMO_DIR}/sample_apps/rpmsg_sample/rsc_table.c
    PROPERTIES COMPILE_OPTIONS "-fno-toplevel-reorder"
)

target_include_directories(actuation_x5h PRIVATE
    ${CYCLONEDDS_INCLUDE_DIR}
    ${EIGEN3_INCLUDE_DIR}
    ${APP_INCLUDE_DIRS}
)
target_link_libraries(actuation_x5h PRIVATE
    cdds
    autoware_msgs
)

# rcar_bsp/.../toolchain_arm_none_eabi.cmake (vendor toolchain file, ours
# not to edit -- same category as the S32Z2 vendor tree) sets CMAKE_LINKER
# to plain arm-none-eabi-gcc and forces both CMAKE_C_LINK_EXECUTABLE and
# CMAKE_CXX_LINK_EXECUTABLE to invoke it (<CMAKE_LINKER> ... -- see that
# file), so every target linked through this project -- including ours --
# links via gcc, never g++. Confirmed via CMakeFiles/actuation_x5h.dir/
# link.txt. Plain gcc does not auto-add libstdc++/libsupc++ to the link the
# way g++'s driver does, so undefined __cxa_*/std::*/operator new symbols
# appear as soon as a target actually needs them -- which Task 3's scaffold
# never did (this is the actuation module's first real C++ link on x5h).
# Listed via target_link_libraries(), not target_link_options(): the
# vendor's own CMAKE_CXX_LINK_EXECUTABLE template places <LINK_FLAGS>
# (target_link_options()) *before* <OBJECTS>, where a -l flag cannot
# satisfy any symbol yet to be referenced, but <LINK_LIBRARIES>
# (target_link_libraries()) after them, where archive resolution works
# normally. Wrapped in --start-group/--end-group because libstdc++,
# libsupc++, libc, and libgcc have circular symbol references that a single
# left-to-right archive scan cannot resolve.
target_link_libraries(actuation_x5h PRIVATE
    -Wl,--start-group
    stdc++
    supc++
    c
    gcc
    -Wl,--end-group
)
