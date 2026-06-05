# Copyright (c) 2026, Arm Limited and contributors.
# SPDX-License-Identifier: Apache-2.0
#
# FreeRTOS on NXP S32Z2 (Cortex-R52) build entry point.
#
# Three NXP packages from the Flexnet portal must be extracted and exported as
# environment variables before invoking cmake:
#
#   export S32_RTD_PATH=~/nxp/RTD_S32ZE_2.0.1/_jar_family/S32DS/software/PlatformSDK_S32ZE
#   export FREERTOS_PATH=~/nxp/FreeRTOS_S32ZE_4.0.0/_jar/S32DS/software/PlatformSDK_S32ZE/FreeRTOS
#   export LWIP_PATH=~/nxp/TCPIP_S32ZE_3.0.0/_jar/S32DS/software/PlatformSDK_S32ZE/stacks/tcpip
#
# Then:
#   cmake -S actuation_module/freertos_s32z2 -B build-s32z2 \
#         -DCMAKE_TOOLCHAIN_FILE=actuation_module/freertos_s32z2/cmake/arm-cortex-r52.cmake
#   cmake --build build-s32z2 -j

cmake_minimum_required(VERSION 3.22)
project(actuation_module_freertos_s32z2 LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mirror the POSIX FreeRTOS build's anchor variable so actuation_core.cmake
# and the autoware component .cmake files resolve their relative paths.
set(ACTUATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(REPO_ROOT ${ACTUATION_DIR}/..)

# ---- NXP package locations (three roots) ----
foreach(_var IN ITEMS S32_RTD_PATH FREERTOS_PATH LWIP_PATH)
    if(NOT DEFINED ENV{${_var}})
        message(FATAL_ERROR
            "${_var} is not set. Point it to the extracted NXP package root. "
            "See actuation_module/freertos_s32z2/README.md for the layout.")
    endif()
    set(${_var} $ENV{${_var}})
    if(NOT IS_DIRECTORY "${${_var}}")
        message(FATAL_ERROR "${_var}=${${_var}} is not a directory.")
    endif()
endforeach()

# ---- RTD Config-Tools-generated PB configurations ----
# These come from S32 Config Tools (S32DS) and carry the NXP Confidential and
# Proprietary licence header, so they cannot live in this public repository.
# They are instead bundled as a PRIVATE git submodule at s32ct_config (the
# S32CT project root, with the layout below). The submodule is clonable only by
# developers with access; everyone else regenerates it via S32 Design Studio
# (see README.md) and points S32CT_GENERATED_DIR at their own project root.
#   $S32CT_GENERATED_DIR/
#     board/Siul2_Port_Ip_Cfg.{c,h}
#     generate/include/*.h
#     generate/src/*.c
# An explicit S32CT_GENERATED_DIR environment variable overrides the submodule.
if(DEFINED ENV{S32CT_GENERATED_DIR})
    set(S32CT_GENERATED_DIR $ENV{S32CT_GENERATED_DIR})
else()
    set(S32CT_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/s32ct_config)
endif()
if(NOT IS_DIRECTORY "${S32CT_GENERATED_DIR}/generate/src")
    message(FATAL_ERROR
        "S32CT_GENERATED_DIR=${S32CT_GENERATED_DIR} does not contain a "
        "generate/src subdirectory. Initialise the private submodule with "
        "'git submodule update --init actuation_module/freertos_s32z2/s32ct_config', "
        "or generate the RTD PB configurations in S32 Design Studio's S32 "
        "Configuration Tools (Update Code on a .mex import of the lwIP "
        "FreeRTOS R52 example) and set S32CT_GENERATED_DIR. See "
        "actuation_module/freertos_s32z2/README.md.")
endif()
message(STATUS "S32CT generated configs: ${S32CT_GENERATED_DIR}")

# Reference FreeRTOS example (provides startup, linker, and demo board config).
set(S32Z2_FREERTOS_EXAMPLE
    ${FREERTOS_PATH}/examples/S32DS/FreeRTOS_Toggle_Led_Example_S32Z2XX_R52)

# Locate the FreeRTOS Cortex-R52 port (ARM_CR52_GIC, self-contained in port.c).
find_path(S32Z2_FREERTOS_PORT_DIR
    NAMES portmacro.h port.c
    PATHS ${FREERTOS_PATH}/Source/portable/GCC/ARM_CR52_GIC
    DOC "NXP FreeRTOS Cortex-R52 port directory"
    REQUIRED NO_DEFAULT_PATH)

find_path(S32Z2_FREERTOS_KERNEL_DIR
    NAMES FreeRTOS.h
    PATHS ${FREERTOS_PATH}/Source/include
    DOC "NXP FreeRTOS kernel include directory"
    REQUIRED NO_DEFAULT_PATH)

set(S32Z2_FREERTOS_SOURCE_DIR ${FREERTOS_PATH}/Source)

message(STATUS "S32Z2 FreeRTOS port directory: ${S32Z2_FREERTOS_PORT_DIR}")
message(STATUS "S32Z2 FreeRTOS kernel headers: ${S32Z2_FREERTOS_KERNEL_DIR}")
message(STATUS "S32Z2 FreeRTOS example:        ${S32Z2_FREERTOS_EXAMPLE}")

# ---- Build-time patching of NXP vendor sources ----
# The FreeRTOS port.c and the lwIP NETC glue eth_port.c need small fixes, but
# both carry the NXP Confidential and Proprietary header and must not be
# committed to this public repository. We commit only our diffs
# (vendor_patched/*.patch) and apply them to the pristine originals from the NXP
# packages into the build tree at configure time. configure_file(COPYONLY)
# refreshes the copy from the original on every configure, so the subsequent
# single-pass `patch` stays idempotent.
find_program(PATCH_EXECUTABLE patch REQUIRED)
set(_s32z2_vendor_patched_dir ${CMAKE_CURRENT_BINARY_DIR}/vendor_patched)
file(MAKE_DIRECTORY ${_s32z2_vendor_patched_dir})
function(s32z2_apply_vendor_patch orig patch out_var)
    get_filename_component(_name "${orig}" NAME)
    set(_out "${_s32z2_vendor_patched_dir}/${_name}")
    if(NOT EXISTS "${orig}")
        message(FATAL_ERROR "NXP vendor source not found: ${orig}")
    endif()
    configure_file("${orig}" "${_out}" COPYONLY)
    execute_process(
        COMMAND "${PATCH_EXECUTABLE}" "${_out}" "${patch}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _log ERROR_VARIABLE _log)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${patch} to ${orig}:\n${_log}")
    endif()
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

s32z2_apply_vendor_patch(
    "${S32Z2_FREERTOS_PORT_DIR}/port.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor_patched/port.c.patch"
    _s32z2_port_c)
s32z2_apply_vendor_patch(
    "${LWIP_PATH}/code/ports/netif/ethif/rtd/generic/eth_port.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor_patched/eth_port.c.patch"
    _s32z2_eth_port_c)

# ---- Configurable values ----
# Mirror the POSIX FreeRTOS build's Zephyr-Kconfig-equivalent knobs so the
# shared platform_config.h works unchanged.
set(CONFIG_LOG_LEVEL 1 CACHE STRING "Log level (0=off, 1=info, 2=debug)")
set(CONFIG_LOG_THROTTLE_RATE 3 CACHE STRING "Log throttle rate in seconds")
set(CONFIG_DDS_DOMAIN_ID 2 CACHE STRING "DDS domain ID")
# CycloneDDS selects its NIC by this value (interface name or IP). On the
# single-NIC S32Z2 the board's own IP is the natural selector; default it to
# the lwip_bringup.c static-fallback address so a bench build is self-consistent.
set(CONFIG_DDS_NETWORK_INTERFACE "192.168.0.105" CACHE STRING "DDS network interface name or IP")
set(CONFIG_DDS_LOG_LEVEL 0 CACHE STRING "DDS log level (0=off, 1=warn, 2=debug)")
# Controller node thread stack. The MPC lateral + PID control cycle is
# stack-heavy (Eigen matrices + OSQP on the stack); 256 KiB overflowed by a few
# hundred bytes (saved SP below the stack base, caught by the overflow hook), so
# 512 KiB gives ~2x margin. The buffer lives in int_sram via node_stack_in_sram.ld
# (the section attr in freertos_threading.h), which has room; int_sram_dram does not.
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

configure_file(
    ${ACTUATION_DIR}/include/platform/freertos/freertos_config_generated.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/freertos_config_generated.h
    @ONLY
)

# ---- Platform definitions ----
add_compile_definitions(PLATFORM_FREERTOS=1 PLATFORM_FREERTOS_S32Z2=1)

# Take the RTD-friendly branches in NXP's lwIP arch/cc.h and lwipopts:
# without these, arch/cc.h pulls a toolchain-dependent device_registers.h
# that does not exist for GCC.
add_compile_definitions(USING_RTD=1 S32Z27=1)

# NXP's lwIP FreeRTOS sys_arch.c uses CPU_CORTEX_* guards to pick between
# Cortex-M (NVIC) and Cortex-R52 (GIC) ISR entry checks. Without one of
# them defined the preprocessor leaves both `if (...)` lines in the source
# and `else` ends up orphaned.
add_compile_definitions(CPU_CORTEX_R52=1)

# Identify the target as an RTU R52 core (NXP's .cproject defines this
# project-wide for the FreeRTOS_Toggle_Led RTU0_R52 example). Without it,
# core.c's Core_registerIsrHandler takes the non-RTU `#else` branch and writes
# `R52VtabeRefArray[irq_id]` (treating the per-core table-pointer ARRAY as the
# vector table) instead of `R52VtabeRefArray[coreId][irq_id]` -- so the tick
# ISR registered by generic_timer.c lands in garbage at &R52VtabeRefArray+30*4
# rather than in the core-0 ATCM vector table (0x30000000[30]). Both NXP's
# IRQ_Handler and the port's vPortInterruptDispatcher read 0x30000000[30], find
# the unregistered default undefined_handler, and stall. Defining RTU_R52 makes
# the register and lookup paths agree.
add_compile_definitions(RTU_R52)

# NXP's startup gates the FP/NEON CPACR write on this macro. We compile
# C/C++ with -mfloat-abi=hard -mfpu=neon-fp-armv8, so the compiler is free
# to emit VFP/Advanced-SIMD instructions in any function (ABI argument
# passing, autovectorisation, Eigen). Without ENABLE_FPU defined,
# Core_FPU_Init() (core.c) compiles to a no-op stub and CPACR.CP10/CP11
# stay disabled — the first FP instruction the compiler emits then traps
# to UndefInstr_Handler. Zephyr's reset.S programs CPACR unconditionally,
# which is why the same chip + the same toolchain boot Zephyr cleanly.
add_compile_definitions(ENABLE_FPU=1)

# NXP's SystemInit gates Core_MPU_Init() on this macro. With MPU disabled,
# Cortex-R52's default memory map treats 0x4000_0000-0x7FFF_FFFF as Normal
# memory rather than Device; halfword writes to MSCM IRSPRC (0x419A_0880+,
# the first thing SystemInit does after Sys_GetCoreID) then get buffered/
# coalesced by the bus and the peripheral aborts the access. With MPU_ENABLE,
# Core_MPU_Init runs first and the PRBAR/PRLAR tables (filled by the NXP
# startup_init from RTD's vector code) mark 0x4000_0000-0x9FFF_FFFF as
# Device-nGnRnE, after which the MSCM loop sees a regular peripheral.
# The Toggle_Led example's .cproject sets this — we have to set it too.
add_compile_definitions(MPU_ENABLE=1)

# Stay on the GENERIC_ECC_SRAM_INIT_64B init path (NXP's default).
# init_data_bss uses block_copy64 / zero_copy64 / write_64bit; write_64bit
# is the only Thumb-encoded helper in that chain and is fixed via the
# `--wrap=write_64bit` linker hook below. The alternative VECTOR_INST
# path drags in mem_xfer_by_vector / mem_zero_by_vector (NEON STM/LDM in
# core_utility.s) which would each need their own wrap; not worth it.

# (_S32DS_FREERTOS_ is applied per-source to generic_timer.c below — adding
# it globally pulls OsIf_ArchCfg.h into eth_port.c too, which conflicts with
# lwIP's sys_mutex_t typedef.)

# Eigen 3.4's NEON specializations assume `int32_t = int`, but
# arm-none-eabi newlib types `int32_t` as `long int` on Cortex-R52, which
# breaks ploadu<PacketI>(...) overloads. Determinism matters more than SIMD
# throughput on the safety island; disable Eigen vectorization here.
add_compile_definitions(EIGEN_DONT_VECTORIZE=1)

# S32CT's Update Code left {Mcu,Power_Ip,Ram_Ip}_VS_0_PBcfg.h as 1-byte
# empty stubs inside S32CT_GENERATED_DIR/generate/include. The matching
# *_Cfg.h files do '#include "X_VS_0_PBcfg.h"' (quoted form, which prefers
# the including file's own directory over -I) and then run a vendor /
# AUTOSAR / SW version sentinel check that the empty stubs cannot satisfy.
#
# Use -imacros to pre-seed the sentinel macros from our pbcfg_shims/
# replacements before each TU starts parsing; the empty stubs then do
# nothing and the sentinel comparisons pass.
add_compile_options(
    "SHELL:-imacros ${CMAKE_CURRENT_SOURCE_DIR}/pbcfg_shims/Mcu_VS_0_PBcfg.h"
    "SHELL:-imacros ${CMAKE_CURRENT_SOURCE_DIR}/pbcfg_shims/Power_Ip_VS_0_PBcfg.h"
    "SHELL:-imacros ${CMAKE_CURRENT_SOURCE_DIR}/pbcfg_shims/Ram_Ip_VS_0_PBcfg.h"
)

# ---- Application executable ----
# startup.s / linker_ram_r52.ld are NXP Confidential and Proprietary; they are
# referenced in-place from the extracted FreeRTOS example and never copied
# into this repository.
set(_s32z2_startup_asm
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/startup.s
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/Vector_Table.s
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/core_utility.s
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/exceptions_stack.s
)
# The NXP startup .s files use C preprocessor macros (#ifdef __ghs__ etc.)
# to switch toolchains and define THUMB_CODE. GNU `as` does not preprocess
# lowercase .s files by default; force cpp preprocessing so the GCC branch
# of the conditionals is taken.
set_source_files_properties(${_s32z2_startup_asm} PROPERTIES
    COMPILE_OPTIONS "-x;assembler-with-cpp"
)

add_executable(actuation_freertos_s32z2
    freertos_main.cpp
    board_init.c
    newlib_stubs.c
    operator_new.cpp
    cp15_arm.S
    ${_s32z2_startup_asm}
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/core.c
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/exceptions.c
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/gic500.c
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/startup_init.c
    ${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Startup_Code/system.c
)
# cp15_arm.S needs the same -x assembler-with-cpp treatment as NXP's .s
set_source_files_properties(cp15_arm.S PROPERTIES
    COMPILE_OPTIONS "-x;assembler-with-cpp"
)

target_link_options(actuation_freertos_s32z2 PRIVATE
    # NXP's linker as the *default* script (-dT, not -T) so heap_in_sram.ld's
    # "INSERT AFTER .sram_code" can augment it: INSERT only augments the default
    # script, and -T would replace it (-> "`.sram_code' not found for insert").
    # The vendor file stays unmodified.
    -Wl,--default-script=${S32Z2_FREERTOS_EXAMPLE}/Project_Settings/Linker_Files/linker_ram_r52.ld
    # Relocate the FreeRTOS (heap_4) ucHeap out of the nearly-full 512 KiB
    # int_sram_dram into the 7 MiB int_sram code region so CycloneDDS has heap.
    # The INSERT script is given via -T (so it is parsed as a script) and, because
    # it contains INSERT, it augments the default script set above instead of
    # replacing it.
    -T${CMAKE_CURRENT_SOURCE_DIR}/heap_in_sram.ld
    # Relocate the controller node thread's stack (.node_stack) out of the
    # nearly-full int_sram_dram into int_sram too — the MPC/Eigen control cycle
    # overflowed 256 KiB there. Another INSERT fragment (augments the default).
    -T${CMAKE_CURRENT_SOURCE_DIR}/node_stack_in_sram.ld
    # Discard the orphan .ARM.exidx/.ARM.extab unwind sections. Unwinding is
    # already disabled (__exidx_start/_end defsym'd 0 below), and at -O1+ the
    # dangling orphan exidx makes ld segfault during the final link. Another
    # INSERT fragment so it augments the default script.
    -T${CMAKE_CURRENT_SOURCE_DIR}/discard_unwind.ld
    # main.cpp is C++, so -Dmain=actuation_main produces the mangled symbol
    # _Z14actuation_mainv. NXP's startup.s and freertos_main.cpp both call
    # `actuation_main` unmangled; expose it as an alias so both reach the
    # same function. Touching main.cpp would change POSIX/Zephyr symbols.
    "-Wl,--defsym=actuation_main=_Z14actuation_mainv"
    # generic_timer.c registers SysTick_Handler as the physical-timer ISR
    # but doesn't define it; the CR52 port supplies xPortSysTickHandler for
    # the non-SMP path (xPortCoreSysTickHandler exists only when
    # configUSE_SMP==1). Alias them at link time.
    "-Wl,--defsym=SysTick_Handler=xPortSysTickHandler"
    # GCC's libgcc unwinder references __exidx_start/__exidx_end to walk
    # the .ARM.exidx exception-handling section. NXP's linker_ram_r52.ld
    # doesn't emit those symbols (the section is unused on this build),
    # so define them as an empty range. catch-based exceptions still
    # work; uncaught throws end in std::terminate as expected.
    "-Wl,--defsym=__exidx_start=0"
    "-Wl,--defsym=__exidx_end=0"
    # The port's FPU handlers live together in one .handlers input section, so
    # keeping vPortUndefinedInstruction (wired to the undef vector in
    # cp15_arm.S) also keeps its neighbour vPortInterruptDispatcher, whose
    # non-_S32DS_FREERTOS_ build path references __VECTOR_RAM. We don't use that
    # dispatcher (the IRQ vector goes to NXP's IRQ_Handler, which looks up ISRs
    # via R52VtabeRefArray), so the symbol just needs to resolve. Point it at
    # the core-0 RAM vector table base so the value is at least sensible.
    "-Wl,--defsym=__VECTOR_RAM=__VTABLE_0"
    # NXP's core_utility.s ships these helpers in Thumb mode (.thumb2).
    # Their `mov pc, lr` epilogue should interwork on ARMv8-R but on this
    # Cortex-R52 the T bit stays set on return, so calls from ARM C code
    # (Core_FPU_Init in core.c) come back still in Thumb state and the
    # caller's ARM-encoded body is decoded as Thumb-2 — execution
    # derails into the .intc_vector data area within a few instructions.
    # Wrap them onto our ARM-mode replacements in cp15_arm.S whose
    # `bx lr` epilogue interworks correctly.
    "-Wl,--wrap=read_CPACR"
    "-Wl,--wrap=write_CPACR"
    "-Wl,--wrap=write_FPEXC"
    "-Wl,--wrap=read_FPEXC"
    "-Wl,--wrap=write_64bit"
    # Same `mov pc, lr` non-interwork bug affects every CP15/MPU/GIC helper
    # NXP's core_utility.s ships. SystemInit → Core_IC_Init → read/write_SCTLR
    # was the next wall after the FPU-enable fix; Core_MPU_Init and the
    # gic500 init path call the rest. Wrap every helper that the current
    # build references via `blx` (see objdump | grep 'blx 321012').
    "-Wl,--wrap=read_SCTLR"
    "-Wl,--wrap=write_SCTLR"
    "-Wl,--wrap=write_PRSELR"
    "-Wl,--wrap=write_PRBAR"
    "-Wl,--wrap=write_PRLAR"
    "-Wl,--wrap=write_MAIR0"
    "-Wl,--wrap=write_MAIR1"
    "-Wl,--wrap=read_MPIDR"
    "-Wl,--wrap=write_ICC_SRE"
    "-Wl,--wrap=write_ICC_PMR"
    "-Wl,--wrap=write_ICC_BPR0"
    "-Wl,--wrap=write_ICC_BPR1"
    "-Wl,--wrap=write_ICC_IGRPEN0"
    "-Wl,--wrap=write_ICC_IGRPEN1"
    "-Wl,--wrap=read_ICC_CTLR"
    "-Wl,--wrap=write_ICC_CTLR"
)

target_include_directories(actuation_freertos_s32z2 PRIVATE
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/../include/platform/freertos/s32z2
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${S32Z2_FREERTOS_KERNEL_DIR}
    ${S32Z2_FREERTOS_PORT_DIR}
    ${S32Z2_FREERTOS_EXAMPLE}/include
    ${S32Z2_FREERTOS_EXAMPLE}/RTD/include
    ${S32CT_GENERATED_DIR}/generate/include
    ${S32CT_GENERATED_DIR}/board
    ${S32_RTD_PATH}/RTD/BaseNXP_TS_T31D53M20I1R0/header
    ${S32_RTD_PATH}/RTD/BaseNXP_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Mcl_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Port_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Platform_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Platform_TS_T31D53M20I1R0/startup/include
    ${S32_RTD_PATH}/RTD/Uart_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Gpt_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Eth_43_NETC_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/EthSwt_43_NETC_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/EthIf_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Mka_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Det_TS_T31D53M20I1R0/include
    ${S32_RTD_PATH}/RTD/Rte_TS_T31D53M20I1R0/include
)

# Pull generated PB/Cfg sources into the target.
#
# The lwip example doesn't include Mcu / Power_Ip / Ram_Ip component
# instances, so S32CT leaves their VS_0_PBcfg.{c,h} as 1-byte stubs and
# their _Cfg.c files reference symbols that depend on missing config
# (failing with MCAL_ENABLE_USER_MODE_SUPPORT-style sentinel errors).
# Exclude both layers from the build; the matching _Cfg.h sentinel checks
# are satisfied by pbcfg_shims/ via -imacros above. The runtime impact is
# that low-level MCU/Power/Ram bring-up happens via the NXP-provided
# startup code in the FreeRTOS example, not via the AUTOSAR Mcu/Power/Ram
# drivers.
file(GLOB _s32ct_generated_src
    ${S32CT_GENERATED_DIR}/generate/src/*.c
    ${S32CT_GENERATED_DIR}/board/*.c)
list(FILTER _s32ct_generated_src EXCLUDE REGEX
    "/(Mcu_Cfg|Mcu_VS_0_PBcfg|Power_Ip_Cfg|Power_Ip_VS_0_PBcfg|Ram_Ip_Cfg|Ram_Ip_VS_0_PBcfg)\\.c$")
target_sources(actuation_freertos_s32z2 PRIVATE ${_s32ct_generated_src})

set_target_properties(actuation_freertos_s32z2 PROPERTIES SUFFIX ".elf")

# ---- FreeRTOS kernel + ARM_CR52_GIC port ----
# The ARM_CR52_GIC port is self-contained in port.c (no portASM.S).
file(GLOB _freertos_core
    ${S32Z2_FREERTOS_SOURCE_DIR}/tasks.c
    ${S32Z2_FREERTOS_SOURCE_DIR}/queue.c
    ${S32Z2_FREERTOS_SOURCE_DIR}/list.c
    ${S32Z2_FREERTOS_SOURCE_DIR}/timers.c
    ${S32Z2_FREERTOS_SOURCE_DIR}/event_groups.c
    ${S32Z2_FREERTOS_SOURCE_DIR}/stream_buffer.c
    ${S32Z2_FREERTOS_SOURCE_DIR}/portable/MemMang/heap_4.c
)
target_sources(actuation_freertos_s32z2 PRIVATE
    ${_freertos_core}
    # port.c patched at configure time (NOT the pristine ${S32Z2_FREERTOS_PORT_DIR}/port.c):
    # vendor_patched/port.c.patch fixes the ARM solicited-resume CPSR bug
    # (moveqs pc,lr inheriting SPSR_irq with a stray Thumb T-bit). portmacro.h
    # and the rest of the port still come from the SDK include dir.
    ${_s32z2_port_c}
    # ARM generic-timer-driven tick source: provides the weak
    # prvSetupTimerInterrupt() that port.c calls from vTaskStartScheduler.
    ${S32Z2_FREERTOS_SOURCE_DIR}/generic_timer.c
)
# Force generic_timer.c onto the S32DS branch (uses OsIf_ArchCfg.h + core.h
# instead of a generic gic.h that NXP doesn't actually ship).
#
# -mgeneral-regs-only: the tick path lives entirely in generic_timer.c
# (SysTick_Handler -> vUpdateTimer -> read_CNTPCT) and runs in interrupt
# context. The ARM_CR52_GIC port uses a lazy-FPU scheme whose interrupt-mode
# FP save/restore (the undefined-instruction svc_mode path + the in-place
# dispatcher save/restore) corrupts a preempted task's callee-saved VFP
# registers d8-d15. read_CNTPCT does no floating-point math, but GCC emitted
# "vmov.i32 d16,#0; vstr d16" merely to zero a uint64_t local, which made the
# 1 kHz tick an FP-using ISR and reliably zeroed live d8/d9 in tasks (e.g. the
# function pointers GCC parks in d8/d9 across the calls in CycloneDDS
# dds__builtin_init -> NULL btif.is_visible -> blx 0 -> PrefetchAbort).
# generic_timer.c is pure integer (uint64_t), so forbidding FP/SIMD registers
# here is functionally inert and removes the FP-from-ISR trigger. (handoff
# risk #4: confine NEON to non-ISR paths.)
set_source_files_properties(${S32Z2_FREERTOS_SOURCE_DIR}/generic_timer.c
    PROPERTIES
        COMPILE_DEFINITIONS "_S32DS_FREERTOS_=1"
        COMPILE_OPTIONS "-mgeneral-regs-only")

# port.c's vPortInterruptDispatcher (wired to the IRQ vector in cp15_arm.S)
# looks up the per-IRQ ISR. Its non-_S32DS_FREERTOS_ path reads a flat
# __VECTOR_RAM table, but generic_timer.c registers the tick ISR through the
# S32DS R52VtabeRefArray per-core table mechanism (the same one NXP's stock
# IRQ_Handler used). Without this define the dispatcher looked up the wrong
# table, found the unregistered default, and spun in undefined_handler. Build
# port.c on the S32DS branch so the dispatcher uses R52VtabeRefArray and finds
# xPortSysTickHandler. (This also places its handlers in .systeminit, which the
# linker script maps, instead of an orphan .handlers section.)
set_source_files_properties(${_s32z2_port_c}
    PROPERTIES COMPILE_DEFINITIONS "_S32DS_FREERTOS_=1")

# common/'s `#include <pthread.h>` is resolved by the hand-rolled shim at
# actuation_module/include/platform/freertos/s32z2/pthread.h (that path is
# already first on the include search list). The shim maps pthread_mutex_*
# to FreeRTOS semaphores and pthread_create/join/cancel to xTaskCreate,
# avoiding FreeRTOS-Plus-POSIX and its header layering problems.

# ---- lwIP (from NXP TCP/IP Stack package) ----
find_path(LWIP_DIR
    NAMES src/api/sockets.c
    PATHS ${LWIP_PATH}/lwip
    DOC "NXP-provided lwIP source root"
    REQUIRED NO_DEFAULT_PATH)
message(STATUS "lwIP root: ${LWIP_DIR}")

file(GLOB _lwip_core
    ${LWIP_DIR}/src/api/*.c
    ${LWIP_DIR}/src/core/*.c
    ${LWIP_DIR}/src/core/ipv4/*.c
    ${LWIP_DIR}/src/netif/*.c
)
target_sources(actuation_freertos_s32z2 PRIVATE ${_lwip_core})
target_include_directories(actuation_freertos_s32z2 PRIVATE
    ${LWIP_DIR}/src/include
    # NXP-supplied arch/cc.h for the GCC port (USING_RTD branch)
    ${LWIP_PATH}/code/ports/platform/generic/gcc/setting
    # NXP-supplied arch/sys_arch.h for the FreeRTOS OS port
    ${LWIP_PATH}/code/ports/OS/FreeRTOS
    # OsIf_rtd_port.h used by sys_arch.c
    ${LWIP_PATH}/code/ports/osif/rtd
)
# Also pull the matching sys_arch.c into the sources.
target_sources(actuation_freertos_s32z2 PRIVATE
    ${LWIP_PATH}/code/ports/OS/FreeRTOS/sys_arch.c
)

# ---- NXP NETC <-> lwIP glue (eth_port.c) and RTD NETC driver ----
target_sources(actuation_freertos_s32z2 PRIVATE
    # eth_port.c patched at configure time (NOT the pristine LWIP_PATH copy):
    # vendor_patched/eth_port.c.patch fixes the upstream STD_OFF (poll) path,
    # which was never exercised (seed the RX ring in STD_OFF; fix a poll-thread
    # spawn guard; fix a stray brace in the STD_OFF Tcpip_TxConfirmation; recycle
    # RX pbufs; non-blocking linkoutput). See the [actuation patch #N] markers.
    ${_s32z2_eth_port_c}
    # Bridges the driver's AUTOSAR EthIf upper-layer callbacks
    # (EthIf_RxIndication/EthIf_TxConfirmation) to eth_port.c's Tcpip_*
    # functions, and supplies OsIf_TimeDelay for the poll thread. We build no
    # EthIf.c and S32CT generates none.
    ${CMAKE_CURRENT_SOURCE_DIR}/ethif_shim.c
)
# Run NETC RX completion in POLL mode (STD_OFF) CONSISTENTLY across the whole
# build (the RTD driver AND eth_port.c). Both the driver's Eth_43_NETC_Cfg.h and
# the post-build Eth_43_NETC_VS_0_PBcfg.h #ifndef-guard this macro, so a single
# command-line define wins everywhere. This is the RTD's supported polling
# configuration: ethif_poll_thread calls Eth_Receive() which drains the BD ring
# and reports frames via EthIf_RxIndication -- no RX MSI-X IRQ, so no R52
# GIC/MRU plumbing and no RX-ISR FPU-corruption wall.
#
# The generated PB config (Netc_Eth_Ip_VS_0_PBcfg.c) was produced with
# EthCtrlEnableRxInterrupt=true, so its RX ring-config struct still names
# Eth_43_NETC_RxIrqCallback in a (never-invoked, poll-mode) function pointer.
# We satisfy that link reference with a no-op stub in ethif_shim.c rather than
# regenerate the config.
add_compile_definitions(ETH_43_NETC_RX_IRQ_ENABLED=STD_OFF)
target_include_directories(actuation_freertos_s32z2 PRIVATE
    ${LWIP_PATH}/code/ports/netif/ethif/rtd/generic
)
file(GLOB _netc_sources
    ${S32_RTD_PATH}/RTD/Eth_43_NETC_TS_T31D53M20I1R0/src/*.c
    # EthSwt_43_NETC (EthSwt_43_NETC.c, EthSwt_43_NETC_Ipw.c, Netc_EthSwt_Ip.c):
    # the S32Z2 NETC sits behind an integrated switch. device_init() runs
    # EthSwt_43_NETC_Init() before Eth_43_NETC_Init(); lwip_bringup.c now does
    # the same, so the switch driver must be compiled in. Its PB config
    # (EthSwt_43_NETC_VS_0_PBcfg.c) comes from the S32CT generate/src glob.
    ${S32_RTD_PATH}/RTD/EthSwt_43_NETC_TS_T31D53M20I1R0/src/*.c
)
target_sources(actuation_freertos_s32z2 PRIVATE ${_netc_sources})

# RTD support drivers used transitively by NETC + lwIP sys_arch:
#   Det          - error reporting (called from every NETC entry point)
#   BaseNXP/OsIf - OS-abstraction counters/timeouts NETC uses for retries
#   Uart_TS      - Linflexd_Uart_Ip_SyncSend (board_init.c console)
file(GLOB _rtd_support_sources
    ${S32_RTD_PATH}/RTD/Det_TS_T31D53M20I1R0/src/*.c
    ${S32_RTD_PATH}/RTD/BaseNXP_TS_T31D53M20I1R0/src/*.c
    ${S32_RTD_PATH}/RTD/Uart_TS_T31D53M20I1R0/src/*.c
    # Siul2_Port_Ip.c provides the IP-layer Siul2_Port_Ip_PinInit()
    # board_init.c uses to mux PORT150 → ALT1 → LIN_9_TX.
    ${S32_RTD_PATH}/RTD/Port_TS_T31D53M20I1R0/src/Siul2_Port_Ip.c
    # Clock_Ip*.c — needed by Clock_Ip_Init() in board_init.c so the
    # LINFLEXD9 module clock is actually enabled before uart9_init.
    # SystemInit alone doesn't touch the LINFLEXD clock tree.
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Data.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Divider.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_DividerTrigger.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_ExtOsc.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_FracDiv.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Frequency.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Gate.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_IntOsc.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Irq.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Monitor.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Pll.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_ProgFreqSwitch.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Selector.c
    ${S32_RTD_PATH}/RTD/Mcu_TS_T31D53M20I1R0/src/Clock_Ip_Specific.c
)
target_sources(actuation_freertos_s32z2 PRIVATE ${_rtd_support_sources})

# NXP-supplied default SchM_*.c implementations for the modules we link
# against. SchM_Enter_*/SchM_Exit_*_EXCLUSIVE_AREA_* are AUTOSAR scheduler
# critical-section hooks; in non-AUTOSAR FreeRTOS builds these defaults
# wrap them as taskENTER_CRITICAL / taskEXIT_CRITICAL.
target_sources(actuation_freertos_s32z2 PRIVATE
    ${S32_RTD_PATH}/RTD/Rte_TS_T31D53M20I1R0/src/SchM_Eth_43_NETC.c
    ${S32_RTD_PATH}/RTD/Rte_TS_T31D53M20I1R0/src/SchM_EthSwt_43_NETC.c
    ${S32_RTD_PATH}/RTD/Rte_TS_T31D53M20I1R0/src/SchM_Uart.c
    # Clock_Ip_Monitor.c uses SchM_Enter_Mcu_MCU_EXCLUSIVE_AREA_*; SchM_Mcu.c
    # is the non-AUTOSAR default that wraps them as taskENTER/EXIT_CRITICAL.
    ${S32_RTD_PATH}/RTD/Rte_TS_T31D53M20I1R0/src/SchM_Mcu.c
)

target_sources(actuation_freertos_s32z2 PRIVATE
    lwip_bringup.c
)

# ---- Pre-built CycloneDDS target library ----
set(CDDS_HOST_PREFIX   ${CMAKE_SOURCE_DIR}/../../build-freertos/cdds_host_out CACHE PATH "")
set(CDDS_TARGET_PREFIX ${CMAKE_SOURCE_DIR}/../../build-s32z2/cdds_target_out CACHE PATH "")

if(NOT EXISTS ${CDDS_HOST_PREFIX}/bin/idlc)
    message(FATAL_ERROR
        "Host idlc not found at ${CDDS_HOST_PREFIX}/bin/idlc. "
        "Run the FreeRTOS POSIX simulator phase 1 build first.")
endif()
if(NOT EXISTS ${CDDS_TARGET_PREFIX}/lib/libddsc.a)
    message(FATAL_ERROR
        "Cross-built CycloneDDS not found at ${CDDS_TARGET_PREFIX}/lib/libddsc.a. "
        "Run actuation_module/freertos_s32z2/scripts/build-cdds-target.sh first.")
endif()

add_library(cdds STATIC IMPORTED GLOBAL)
set_target_properties(cdds PROPERTIES
    IMPORTED_LOCATION ${CDDS_TARGET_PREFIX}/lib/libddsc.a)

set(CYCLONEDDS_INCLUDE_DIR ${CDDS_TARGET_PREFIX}/include)

# Expose host idlc on PATH so autoware_msgs configure finds it.
set(ENV{PATH}            "${CDDS_HOST_PREFIX}/bin:$ENV{PATH}")
set(ENV{LD_LIBRARY_PATH} "${CDDS_HOST_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")
set(CycloneDDS_DIR ${CDDS_HOST_PREFIX}/lib/cmake/CycloneDDS)

# ---- Eigen3 (host package, header-only) ----
find_package(Eigen3 REQUIRED)

# ---- Autoware Messages (IDL -> C via host idlc) ----
add_subdirectory(${CMAKE_SOURCE_DIR}/../src/autoware/autoware_msgs
                 ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

# ---- Autoware controller core (shared with POSIX simulator) ----
include(${CMAKE_SOURCE_DIR}/../cmake/actuation_core.cmake)

target_sources(actuation_freertos_s32z2 PRIVATE
    ${ACTUATION_DIR}/src/main.cpp
    ${APP_SOURCES}
)

# ---- Optimisation policy: C++ at -O2, C at -O0, never -g ----
# The control cycle is dominated by a dense-Cholesky Eigen QP solve
# (QPSolverEigenLeastSquareLLT -- this port does NOT use OSQP) plus the MPC matrix
# generation. At -O0 the first solve takes ~86 s because Eigen is pathologically
# slow unoptimised (it depends on aggressive inlining of its expression
# templates). Compiling the C++ at -O2 cuts the control cycle ~25x (to ~3.5 s) and
# the full host->board->control_cmd round-trip works with identical output.
#
# Optimisation is applied to C++ ONLY -- the C sources stay at -O0 -- and it is
# applied UNIFORMLY across all C++ (a single target-wide option, not a per-TU
# mix). Both constraints are load-bearing and were established on hardware:
#   * The custom C platform/network port (lwIP + NETC + vendor glue + FreeRTOS)
#     is tuned for -O0 codegen. At -O1 it silently drops controller input RX; at
#     -O2 it hard-hangs the board right after DDS setup (no UART, no ping). So C
#     stays -O0.
#   * Mixing -O0 and -O2 *within* C++ (e.g. only the autoware math at -O2, the DDS
#     glue at -O0) makes dds_create_domain_with_rawconfig fail at boot -- an
#     ODR/ABI mismatch on inline code shared between the -O0 and -O2 translation
#     units. Uniform C++ optimisation (all -O2) avoids it; consistent -O0 or -O2
#     both boot DDS fine, only the mix breaks it.
#
# -g must stay OFF: GNU ld 2.42 (BFD) segfaults during the final link when -O2's
# inlining orphans the per-function DWARF sections and --gc-sections then walks
# the dangling .debug_* list (in lang_size_sections). -O2 itself links fine
# without -g. The explicit -g0 below ENFORCES this so the constraint is not just
# documentation: it overrides a -g pulled in by CMAKE_BUILD_TYPE=Debug or a
# custom CMAKE_CXX_FLAGS, which would otherwise reintroduce the link crash. If
# you genuinely need debug info, drop the -g0 and use -gsplit-dwarf (keeps
# .debug_* out of the link); address-level gdb (arm-none-eabi-nm) works without
# -g regardless.
#
# Not yet hard-real-time: the ~3.5 s cycle is scalar Eigen (NEON is disabled via
# EIGEN_DONT_VECTORIZE, see above). Closing the gap to the 150 ms control period
# needs NEON and/or an algorithmic change -- a separate effort.
target_compile_options(actuation_freertos_s32z2 PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-O2>
    # Enforce "never -g" for C++ (scoped to match -O2, the combination that
    # crashes ld 2.42). Last on the command line, so it wins over any earlier -g.
    $<$<COMPILE_LANGUAGE:CXX>:-g0>
)

# Rename main() to actuation_main() only inside the Autoware app's main.cpp;
# freertos_main.cpp keeps its own main(), and startup.s `bl main` must reach
# that wrapper to bring up the FreeRTOS scheduler. A global -Dmain= would
# rewrite startup.s too and skip the wrapper entirely.
set_source_files_properties(${ACTUATION_DIR}/src/main.cpp PROPERTIES
    COMPILE_DEFINITIONS "main=actuation_main"
)

target_include_directories(actuation_freertos_s32z2 PRIVATE
    ${CYCLONEDDS_INCLUDE_DIR}
    ${EIGEN3_INCLUDE_DIR}
    ${APP_INCLUDE_DIRS}
)
target_link_libraries(actuation_freertos_s32z2 PRIVATE
    cdds
    autoware_msgs
)
