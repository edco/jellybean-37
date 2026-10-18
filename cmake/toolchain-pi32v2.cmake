# CMake Toolchain file for JieLi PI32v2 architecture (AC791N / WL82)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR pi32v2)

# Toolchain paths: prioritize JIELI_TOOLCHAIN_PATH env/var, fallback to project-local toolchain/pi32v2
if(NOT DEFINED JIELI_TOOLCHAIN_PATH)
    if(DEFINED ENV{JIELI_TOOLCHAIN_PATH})
        set(JIELI_TOOLCHAIN_PATH "$ENV{JIELI_TOOLCHAIN_PATH}")
    else()
        get_filename_component(_PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
        if(EXISTS "${_PROJ_ROOT}/toolchain/pi32v2")
            set(JIELI_TOOLCHAIN_PATH "${_PROJ_ROOT}/toolchain/pi32v2")
        endif()
    endif()
endif()

set(TOOLCHAIN_ROOT "${JIELI_TOOLCHAIN_PATH}")
set(TOOLCHAIN_BIN_DIR "${TOOLCHAIN_ROOT}/bin")

set(CMAKE_C_COMPILER "${TOOLCHAIN_BIN_DIR}/clang")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN_DIR}/clang++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_BIN_DIR}/clang")
set(CMAKE_LINKER "${TOOLCHAIN_BIN_DIR}/ld")
set(CMAKE_OBJCOPY "${TOOLCHAIN_BIN_DIR}/objcopy")
set(CMAKE_OBJDUMP "${TOOLCHAIN_BIN_DIR}/objdump")
set(CMAKE_AR "${TOOLCHAIN_BIN_DIR}/ar")
set(CMAKE_STRIP "${TOOLCHAIN_BIN_DIR}/strip")

# Skip compiler test linking since we need custom linker scripts
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Base compiler flags
set(TARGET_FLAGS "-target pi32v2 -mcpu=r3 -mfprev1 -mllvm -pi32v2-large-program=true -isystem ${TOOLCHAIN_ROOT}/include -isystem ${TOOLCHAIN_ROOT}/lib/clang/4.0.1/include")
set(CMAKE_C_FLAGS_INIT "${TARGET_FLAGS} -Oz -g -ffunction-sections -fdata-sections -Wall")
set(CMAKE_CXX_FLAGS_INIT "${TARGET_FLAGS} -Oz -g -ffunction-sections -fdata-sections -Wall -std=gnu++14 -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "-target pi32v2 -mcpu=r3 -mfprev1 -g")

# System search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
