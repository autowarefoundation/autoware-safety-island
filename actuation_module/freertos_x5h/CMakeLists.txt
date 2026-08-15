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
# This file links the full actuation_x5h image: the R-Car BSP boot scaffold
# (boots the BSP, prints a boot banner) plus the actuation module,
# CycloneDDS, and lwIP-over-RPMsg (see the "full-link" section below),
# everything compiling and linking inside the frozen memory budget. lwIP is
# brought up for real (freertos_x5h/lwip_bringup.c) against a real RPMsg
# transport endpoint (freertos_x5h/rpmsg_transport.{h,c}); freertos_main.cpp
# calls into all of it via actuation_task(). The ELF's memory layout is
# frozen and verified by
# scripts/check-elf-contract.sh: .text at 0x11600000 (the Core1 vram2 slot)
# and .resource_table at 0x96650000 (the fixed remoteproc resource-table
# carveout), both produced by the BSP's own unmodified linker scripts
# referenced below (see AUDIT.md Section 5). Total slot usage is verified
# against the 10 MiB budget by scripts/check-image-budget.sh.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mirror the other FreeRTOS targets' anchor variables so
# actuation_core.cmake / autoware component .cmake files resolve their
# relative paths the same way; the actuation module section below (and the
# CycloneDDS/lwIP sections that follow it) use these. CMAKE_CURRENT_LIST_DIR,
# not CMAKE_CURRENT_SOURCE_DIR -- see the header comment above.
#
# X5H_-prefixed: this set() runs in the VENDOR's directory scope (see the
# header comment above), so an unprefixed name here would leak into that
# scope's variable namespace -- a growing collision surface as more sections
# below add more variables. X5H_BSP_RPMSG_SOURCES below already followed this
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
# Task 7 swaps this one variable to local, actively-maintained copies of the
# BSP sample's own platform_rcar.c/remoteproc_rcar.c/rsc_table.c. Review
# checked this claim by diffing all three against the vendor tree: rsc_table.c
# is byte-identical; platform_rcar.c and remoteproc_rcar.c each differ from
# the vendor original by exactly one trailing-whitespace character trimmed
# from a comment line (verified via `diff -u` against
# rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/sample_apps/rpmsg_sample/, no other
# differences). None of this affects the resource table those files produce
# -- the trimmed bytes are inside C comments, not in any resource-table
# initializer -- so it stays byte-identical to the sample's while living
# under our own directory, alongside rpmsg_transport.c/rsc_table.h/platform_info.h/
# platform_info_common.h (also local copies -- quote-#include resolves
# "rsc_table.h" etc. against the including file's own directory first, which
# is what keeps these copies from ever seeing the shadowed drivers/virtio/
# trio that ENABLE_OPENAMP=1 also puts on this target's include path).
set(X5H_BSP_RPMSG_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/platform_rcar.c
    ${CMAKE_CURRENT_LIST_DIR}/remoteproc_rcar.c
    ${CMAKE_CURRENT_LIST_DIR}/rsc_table.c
)

# ---- Task 18: diagnostic instrumentation for the silent CR52 wedge ----
#
# Listed in BOTH targets' own add_executable() calls below rather than added
# afterwards through target_sources(), so each target's provenance stays
# legible from its own definition -- the same reasoning netif_only_x5h's
# source list already carries.
#
# x5h_diag_vectors.S is this target's first .S source. The vendor's own
# project() call enables ASM alongside C (rcar_bsp/.../CMakeLists.txt's
# `enable_language(C ASM)`), and because our targets are defined in that
# same directory scope they inherit its CMAKE_ASM_FLAGS and its
# directory-scoped add_compile_options() -- the same treatment that already
# builds the BSP's own boot.S and asm_vectors.S. The capital-S suffix is
# required, not cosmetic: it is what makes GCC run the C preprocessor over
# the file, which is how it picks up the X5H_DIAG_EXC_* codes it shares with
# x5h_diag.c out of x5h_diag.h.
#
# Neither file is behind a compile definition or a CMake option. This is a
# diagnostic image whose whole purpose is to always talk; a flag that could
# default off would defeat it.
set(X5H_DIAG_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/x5h_diag.c
    ${CMAKE_CURRENT_LIST_DIR}/x5h_diag_vectors.S
)

# The same -Wall/-Wextra treatment the Task 6/7 network sources get further
# down this file, and for the same reason: neither target's own
# target_compile_options() block enables them, and new code in this
# directory is held to the host test target's bar. C only -- these are not
# flags the assembler consumes.
set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/x5h_diag.c
    ${CMAKE_CURRENT_LIST_DIR}/x5h_cdds_process.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra"
)

# ${CMAKE_CURRENT_LIST_DIR}/freertos_main.cpp, not a bare relative
# "freertos_main.cpp": a plain include() (see the header comment above)
# resolves relative source paths against CMAKE_CURRENT_SOURCE_DIR, which is
# the vendor's own directory here, not this one.
add_executable(actuation_x5h
    ${CMAKE_CURRENT_LIST_DIR}/freertos_main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/newlib_stubs.c
    # Task 19. Listed here and NOT in X5H_DIAG_SOURCES because, unlike the
    # Task 18 files, it is specific to this target: it defines ddsrt_getpid()
    # and ddsrt_getprocessname() so that libddsc.a's process.c.obj is never
    # pulled into the link (see that file's header comment), and it includes
    # CycloneDDS's own headers to do it. netif_only_x5h links neither
    # libddsc.a nor those include directories, so the override belongs to
    # actuation_x5h alone.
    ${CMAKE_CURRENT_LIST_DIR}/x5h_cdds_process.c
    ${X5H_DIAG_SOURCES}
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
#
# main.cpp is C++, so -Dmain=actuation_main (below, on src/main.cpp) produces
# the mangled symbol _Z14actuation_mainv (Itanium ABI: "actuation_main" is 14
# characters, taking void, so no further parameter encoding), not the plain C
# symbol `actuation_main` freertos_main.cpp's actuation_task() calls via its
# own `extern "C" int actuation_main(void);` declaration. Mirrors
# freertos_s32z2/CMakeLists.txt's own identical --defsym line (same main.cpp,
# same signature, same mangling) verbatim -- this is not new C++-ABI
# guesswork, just the same fix ported to the platform that had never actually
# linked a call to actuation_main() until this task wired one in.
target_link_options(actuation_x5h PRIVATE
    -T${X5H_RCAR_BSP_DEMO_DIR}/common/linker/lscript_rsc_table_vram2.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/actuation_x5h.map
    "-Wl,--defsym=actuation_main=_Z14actuation_mainv"
)

# ---- Full-link the actuation module + CycloneDDS + lwIP ----
#
# Network bring-up is live: freertos_main.cpp's actuation_task() calls
# lwip_bring_up_blocking() (freertos_x5h/lwip_bringup.c), which brings up the
# real RPMsg netif and transport. This section makes everything below
# compile and link within the frozen 10 MiB Core1 slot budget
# (scripts/check-image-budget.sh). These are plain target_sources() on
# actuation_x5h (not members of a static archive), and the base link options
# above do not use --gc-sections, so every translation unit added here is
# unconditionally included in the link regardless of whether anything calls
# into it.

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
# Default 0 (off), unchanged -- the shipping image is unaffected by this
# knob's new level. Level 3 (Task 23) is the diagnostic setting for the
# once-per-boot discovery defect: a discovery/liveliness-only CycloneDDS trace
# mask, chosen because level 2's DDS_LC_ALL is unusable on this target's
# busy-polled, unbuffered 115200 console. Build the instrumented image with
# -DCONFIG_DDS_LOG_LEVEL=3; the mask itself, and the full justification for
# preferring it over DDS_LC_ALL, live at the point of use in
# actuation_module/include/common/dds/config.hpp's "Trace" section. Do not
# make 2 or 3 a default here.
set(CONFIG_DDS_LOG_LEVEL 0 CACHE STRING "DDS log level (0=off, 1=warn, 2=debug/DDS_LC_ALL, 3=discovery-only trace)")
# Task 8: unicast SPDP peer + full multicast disable. Task 6's RPMsg-backed
# lwIP netif is a point-to-point link -- it carries no multicast at all,
# unlike the S32Z2 bench (a switched segment where multicast is merely
# IGMP-snooped, not absent), so unlike S32Z2's CONFIG_DDS_PEER default of ""
# (multicast-only unless overridden), the CR52 must always discover its one
# possible peer -- the Linux edge ECU -- by unicast, and never attempt
# multicast in the first place (see common/dds/config.hpp's
# CONFIG_DDS_DISABLE_MULTICAST). Both constants are the frozen wire values
# shared with the Linux side's cyclonedds-x5h.xml.
set(CONFIG_DDS_PEER "172.16.52.1" CACHE STRING "Unicast SPDP peer IPv4 (Linux edge ECU over the RPMsg netif)")
# Task 21: size the DDS wire units under the frozen 462-byte RPMsg-netif MTU
# instead of relying on IP fragmentation. Full derivation lives as a comment
# in common/dds/config.hpp right where these are consumed -- summary only
# here: UDP payload ceiling = 462 - 20 (IPv4) - 8 (UDP) = 434 B.
# CONFIG_DDS_FRAGMENT_SIZE leaves room inside that ceiling for one RTPS
# message header (20 B) + one INFO_DST submessage (16 B, worst case: a
# directed retransmit) + one INFO_TS submessage (12 B) + one DATA_FRAG
# submessage header (36 B) = 84 B of fixed overhead, so 434 - 84 = 350,
# rounded DOWN to the nearest multiple of 4 (CycloneDDS pads a fragment's
# payload to a 4-byte boundary and transmits that padding -- ddsi_xmsg.c's
# align4u()/`assert ((plsize % 4) == 0)`), giving 348 B. Fix-round-1
# correction: a first draft used 68 B / 366 B here, missing both the
# directed-retransmit INFO_DST submessage and the 4-byte alignment
# requirement -- see config.hpp's own comment for the full derivation and
# why 366 B still IP-fragmented on exactly the traffic (Trajectory,
# Odometry) this task exists to fix.
# CONFIG_DDS_MAX_MSG_SIZE / CONFIG_DDS_MAX_REXMIT_MSG_SIZE are themselves
# defined as "the maximum UDP payload Cyclone DDS will generate" (also for
# retransmits), so both get the 434 B ceiling directly. These MUST be set on
# both ends of this link -- see also
# scripts/build-edge-ecu-peer-arm64.sh's matching -D flags for the arm64
# Linux peer -- or the end left at the CycloneDDS defaults (14720/1456/1344)
# still IP-fragments every full-size datagram onto a link whose reassembly
# depth cannot keep up (see lwip_port/lwipopts.h's IP_REASS_* comment).
set(CONFIG_DDS_MAX_MSG_SIZE 434 CACHE STRING "Max DDS UDP payload size (bytes); see common/dds/config.hpp for derivation")
set(CONFIG_DDS_MAX_REXMIT_MSG_SIZE 434 CACHE STRING "Max DDS retransmit UDP payload size (bytes); see common/dds/config.hpp for derivation")
set(CONFIG_DDS_FRAGMENT_SIZE 348 CACHE STRING "DDSI sample fragment size (bytes); see common/dds/config.hpp for derivation")
# target_compile_definitions(actuation_x5h ...), not directory-scoped
# add_compile_definitions() (review finding, Minor): this file runs inside
# the VENDOR's own directory scope (see the header comment above), so a
# directory-scoped add_compile_definitions() here would leak
# CONFIG_NET_CONFIG_PEER_IPV4_ADDR/CONFIG_DDS_DISABLE_MULTICAST/CONFIG_DDS_
# MAX_MSG_SIZE/CONFIG_DDS_MAX_REXMIT_MSG_SIZE/CONFIG_DDS_FRAGMENT_SIZE into
# every target configured in that scope, not just actuation_x5h -- including
# netif_only_x5h, which does not compile common/dds/config.hpp (the only
# consumer of any of these macros) at all. Scoped to actuation_x5h alone,
# matching the EIGEN_FAST_MATH=0 precedent already in this file.
if(CONFIG_DDS_PEER)
    target_compile_definitions(actuation_x5h PRIVATE CONFIG_NET_CONFIG_PEER_IPV4_ADDR="${CONFIG_DDS_PEER}")
endif()
target_compile_definitions(actuation_x5h PRIVATE CONFIG_DDS_DISABLE_MULTICAST=1)
target_compile_definitions(actuation_x5h PRIVATE
    CONFIG_DDS_MAX_MSG_SIZE=${CONFIG_DDS_MAX_MSG_SIZE}
    CONFIG_DDS_MAX_REXMIT_MSG_SIZE=${CONFIG_DDS_MAX_REXMIT_MSG_SIZE}
    CONFIG_DDS_FRAGMENT_SIZE=${CONFIG_DDS_FRAGMENT_SIZE}
)
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

# ---- Task 6: RPMsg-backed lwIP netif + bring-up ----
# rpmsg_netif_core.c (Task 5, "no lwIP, no OS deps") was up to now only
# compiled into the host-side rpmsg_netif_core_test (see
# freertos/CMakeLists.txt) -- this is the first time it is linked into the
# actual target image. rpmsg_netif.c is the lwIP-facing glue that calls into
# it; lwip_bringup.c is this platform's real lwip_bring_up_blocking()
# (replaces the Task 3 weak stub deleted from freertos_main.cpp);
# rpmsg_transport.c is Task 7's real OpenAMP/RPMsg endpoint (replaces the
# Task 6 stub of the same name; see that file's own header comment) --
# together with X5H_BSP_RPMSG_SOURCES above, this is what actually announces
# the rpmsg-eth service and feeds inbound frames to rpmsg_netif_rx().
target_sources(actuation_x5h PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_netif_core.c
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_netif.c
    ${CMAKE_CURRENT_LIST_DIR}/lwip_bringup.c
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_transport.c
)

# actuation_x5h's own target_compile_options() blocks (below, in this same
# file) do not enable -Wall/-Wextra, and rpmsg_netif_core_test (the host
# test target in freertos/CMakeLists.txt) does -- these four files get the
# same treatment here so this new code is held to that bar in-target too.
set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_netif_core.c
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_netif.c
    ${CMAKE_CURRENT_LIST_DIR}/lwip_bringup.c
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_transport.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra"
)

# ---- Task 7: netif_only_x5h -- the Stage 2 board artifact ----
#
# A SECOND target in this same configure (per the controller's ruling on
# this task), not a project-wide option that would make actuation_x5h itself
# conditional: one `cmake --build` of this one configure must produce BOTH
# actuation_x5h.elf (the full actuation module) and netif_only_x5h.elf (the
# same board/transport/netif stack with the actuation module, CycloneDDS,
# Eigen, and autoware_msgs left out entirely -- lwIP answers ICMP natively
# with nothing on top, so ping alone proves the netif works before ever
# trusting the full actuation link). Built on request via
# `cmake --build build/freertos-x5h --target netif_only_x5h` (see the task
# brief); not part of build.sh's own default target list.
#
# Deliberately NOT built from an OBJECT library shared with actuation_x5h:
# the two targets' compile-definition sets differ (X5H_NETIF_ONLY below,
# absent from actuation_x5h) and sharing compiled objects across two
# different definition sets is not how CMake object libraries work anyway
# (one compilation per OBJECT library, not per consuming target). Re-listing
# the same small source set here is simpler and keeps each target's
# provenance (what actually went into THIS elf) legible from its own
# add_executable() call, matching how actuation_x5h itself is defined above.
add_executable(netif_only_x5h
    ${CMAKE_CURRENT_LIST_DIR}/freertos_main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/newlib_stubs.c
    ${X5H_DIAG_SOURCES}
    ${X5H_BSP_RPMSG_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_netif_core.c
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_netif.c
    ${CMAKE_CURRENT_LIST_DIR}/lwip_bringup.c
    ${CMAKE_CURRENT_LIST_DIR}/rpmsg_transport.c
    ${lwipcore_SRCS}
    ${lwipcore4_SRCS}
    ${lwipapi_SRCS}
    ${LWIP_DIR}/src/netif/ethernet.c
    ${CMAKE_CURRENT_LIST_DIR}/lwip_port/sys_arch.c
)

set_target_properties(netif_only_x5h PROPERTIES
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    EXCLUDE_FROM_ALL TRUE
)

target_compile_definitions(netif_only_x5h PRIVATE
    PLATFORM_FREERTOS=1
    PLATFORM_FREERTOS_X5H=1
    MFIS_CHAN=${MFIS_CHAN}
    X5H_NETIF_ONLY=1
)

target_include_directories(netif_only_x5h PRIVATE
    ${X5H_ACTUATION_DIR}/include
    ${X5H_ACTUATION_DIR}/include/platform/freertos/x5h
    ${X5H_RCAR_BSP_DEMO_DIR}
    ${X5H_RCAR_BSP_DEMO_DIR}/CMSIS_5
    ${X5H_RCAR_BSP_DEMO_DIR}/CMSIS_5/CMSIS/Core_R/Include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${LWIP_DIR}/src/include
    ${CMAKE_CURRENT_LIST_DIR}/lwip_port
)

# ---- X5H_DIAG_TASK_TABLE (Task 23, diagnostic only, OFF by default) ----
# Turns on the periodic per-task state/priority/stack/run-time dump in
# x5h_diag.c's beacon. What it is for, what each field discriminates, and what
# it costs are all documented at the point of use in x5h_diag.c's "R3b" block
# -- not repeated here. Configure with -DX5H_DIAG_TASK_TABLE=ON alongside
# -DCONFIG_DDS_LOG_LEVEL=3 to build the full instrumented image; leave it off
# for anything that is going to stay on the board.
#
# Placed here rather than beside the CONFIG_DDS_* knobs above because it has
# to be applied to BOTH targets and netif_only_x5h is not defined until this
# point in the file. Both get it for the same reason netif_only_x5h compiles
# x5h_diag.c at all: it is the cheap isolation image reached for first when
# the board misbehaves, and it should not be the one build that cannot say
# which task stopped.
option(X5H_DIAG_TASK_TABLE "Emit a periodic FreeRTOS task table on the console (diagnostic builds only)" OFF)
if(X5H_DIAG_TASK_TABLE)
    target_compile_definitions(actuation_x5h PRIVATE X5H_DIAG_TASK_TABLE=1)
    target_compile_definitions(netif_only_x5h PRIVATE X5H_DIAG_TASK_TABLE=1)
endif()

target_link_libraries(netif_only_x5h PRIVATE
    freertos_bsp
    openamp
    libmetal
    m
)

target_link_options(netif_only_x5h PRIVATE
    -T${X5H_RCAR_BSP_DEMO_DIR}/common/linker/lscript_rsc_table_vram2.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/netif_only_x5h.map
)

# Review finding (Minor): the fd_set/struct-timeval fix for lwipapi_SRCS is
# NOT repeated here. set_source_files_properties() sets properties on the
# named source FILES, in the CURRENT DIRECTORY SCOPE -- not per-target -- so
# the call already made once, above (actuation_x5h's own lwipapi_SRCS
# treatment), already applies to these same files for every target in this
# directory scope, netif_only_x5h included. A second, identical call here
# was a no-op that only obscured that fact.

# Same C/C++ hosted-mode restore as actuation_x5h needs (see that target's
# own detailed comment below, in this same file): freertos_main.cpp's
# X5H_NETIF_ONLY branch still reaches configure_network() ->
# common/logger/logger.hpp, which pulls in <chrono>/<map>/<string> --
# hosted-only headers under this directory's inherited -ffreestanding.
target_compile_options(netif_only_x5h PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-U__STDC_HOSTED__$<SEMICOLON>-D__STDC_HOSTED__=1>
)

# Same -O2 rationale as actuation_x5h: no reason for this target's C sources
# to sit at the directory's implicit -O0 either.
target_compile_options(netif_only_x5h PRIVATE
    $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:-O2>
)

# Same plain-gcc-link C++ runtime group as actuation_x5h needs (see that
# target's own detailed comment below) -- freertos_main.cpp is compiled as
# C++ here too (its X5H_NETIF_ONLY branch), so the same undefined-symbol
# risk applies even though this target never touches CycloneDDS/Eigen/the
# actuation module itself.
target_link_libraries(netif_only_x5h PRIVATE
    -Wl,--start-group
    stdc++
    supc++
    c
    gcc
    -Wl,--end-group
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
# by the if(NOT EXISTS ...) check in the "Pre-built CycloneDDS target
# library" section above), not a bare `idlc` relying on PATH: this custom command
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
# from startup code) and calls actuation_main() from its actuation_task()
# (Task 7's own wiring -- see that file's header comment). The --defsym alias
# this rename needs (main.cpp is C++, so the renamed symbol is mangled) lives
# on actuation_x5h's target_link_options() above, next to the comment
# explaining why.
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
#
# Path updated for Task 7: rsc_table.c now lives at
# ${CMAKE_CURRENT_LIST_DIR}/rsc_table.c (our own local copy -- see
# X5H_BSP_RPMSG_SOURCES above), not under X5H_RCAR_BSP_DEMO_DIR.
set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/rsc_table.c
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
