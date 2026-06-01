# CMake toolchain file for NXP S32Z2 / ARM Cortex-R52 bare-metal.
# Used by actuation_module/freertos_s32z2/ and the cross-build of CycloneDDS.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-r52)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

set(_S32Z2_CFLAGS
    "-mcpu=cortex-r52 -mfpu=neon-fp-armv8 -mfloat-abi=hard "
    "-ffunction-sections -fdata-sections -fno-common"
)
string(REPLACE ";" "" _S32Z2_CFLAGS "${_S32Z2_CFLAGS}")

set(CMAKE_C_FLAGS_INIT "${_S32Z2_CFLAGS}")
# Project mandates "C++17 with exceptions and RTTI enabled" (CLAUDE.md).
# The Autoware controller and CycloneDDS bindings use both.
set(CMAKE_CXX_FLAGS_INIT "${_S32Z2_CFLAGS} -fexceptions -frtti")
set(CMAKE_ASM_FLAGS_INIT "${_S32Z2_CFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -nostartfiles")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
