cmake_minimum_required(VERSION 3.20)

# Determine target platform: ALL (Superbuild), MCU, or NATIVE
if(NOT DEFINED TARGET_PLATFORM)
    if(CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_NAME STREQUAL "Generic")
        set(TARGET_PLATFORM "MCU")
    else()
        set(TARGET_PLATFORM "ALL")
    endif()
endif()

message(STATUS "=== Jellybean-37 Target Platform: ${TARGET_PLATFORM} ===")

# =========================================================================
# Load Build Configuration (config/firmware.conf)
# =========================================================================
set(FIRMWARE_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/firmware.conf")
if(EXISTS "${FIRMWARE_CONFIG_FILE}")
    file(STRINGS "${FIRMWARE_CONFIG_FILE}" CONFIG_LINES REGEX "^[ \t]*[A-Za-z0-9_]+[ \t]*=")
    foreach(CONFIG_LINE IN LISTS CONFIG_LINES)
        string(REGEX REPLACE "^[ \t]*([A-Za-z0-9_]+)[ \t]*=[ \t]*(.*)[ \t]*$" "\\1;\\2" KEY_VAL "${CONFIG_LINE}")
        list(GET KEY_VAL 0 CONF_KEY)
        list(GET KEY_VAL 1 CONF_VAL)
        string(STRIP "${CONF_KEY}" CONF_KEY)
        string(STRIP "${CONF_VAL}" CONF_VAL)
        # Strip trailing comments
        string(REGEX REPLACE "[ \t]*#.*$" "" CONF_VAL "${CONF_VAL}")
        string(STRIP "${CONF_VAL}" CONF_VAL)
        set(CFG_${CONF_KEY} "${CONF_VAL}")
    endforeach()
endif()

# 1. CPU Frequency Configuration (320 MHz vs 360 MHz Factory Overclock)
if(DEFINED CFG_CPU_FREQ_MHZ)
    set(SMK37_CPU_FREQ_MHZ "${CFG_CPU_FREQ_MHZ}" CACHE STRING "Target CPU frequency in MHz (320 or 360)" FORCE)
elseif(NOT DEFINED SMK37_CPU_FREQ_MHZ)
    set(SMK37_CPU_FREQ_MHZ "320" CACHE STRING "Target CPU frequency in MHz (320 or 360)")
endif()
set_property(CACHE SMK37_CPU_FREQ_MHZ PROPERTY STRINGS "320" "360")
if(NOT SMK37_CPU_FREQ_MHZ STREQUAL "320" AND NOT SMK37_CPU_FREQ_MHZ STREQUAL "360")
    message(FATAL_ERROR "Invalid SMK37_CPU_FREQ_MHZ: '${SMK37_CPU_FREQ_MHZ}'. Must be 320 or 360.")
endif()

# 2. Audio Sample Rate Configuration
if(DEFINED CFG_AUDIO_SAMPLE_RATE)
    set(SMK37_AUDIO_SAMPLE_RATE "${CFG_AUDIO_SAMPLE_RATE}" CACHE STRING "Target Audio Sample Rate in Hz" FORCE)
elseif(NOT DEFINED SMK37_AUDIO_SAMPLE_RATE)
    set(SMK37_AUDIO_SAMPLE_RATE "48000" CACHE STRING "Target Audio Sample Rate in Hz")
endif()
if(NOT SMK37_AUDIO_SAMPLE_RATE STREQUAL "48000")
    message(FATAL_ERROR "Invalid SMK37_AUDIO_SAMPLE_RATE: '${SMK37_AUDIO_SAMPLE_RATE}'. Must be 48000.")
endif()

# 3. Startup Diagnostics & Microbenchmarks Option
if(DEFINED CFG_STARTUP_DIAGNOSTICS)
    set(STARTUP_DIAGNOSTICS "${CFG_STARTUP_DIAGNOSTICS}" CACHE BOOL "Run startup diagnostics, self-test, and microbenchmarks" FORCE)
elseif(NOT DEFINED STARTUP_DIAGNOSTICS)
    set(STARTUP_DIAGNOSTICS OFF CACHE BOOL "Run startup diagnostics, self-test, and microbenchmarks")
endif()
if(STARTUP_DIAGNOSTICS)
    set(STARTUP_DIAGNOSTICS ON)
else()
    set(STARTUP_DIAGNOSTICS OFF)
endif()

# 4. USB Serial CDC ACM Option
if(DEFINED CFG_ENABLE_SERIAL)
    set(ENABLE_SERIAL "${CFG_ENABLE_SERIAL}" CACHE BOOL "Enable USB CDC-ACM virtual serial COM port" FORCE)
elseif(NOT DEFINED ENABLE_SERIAL)
    set(ENABLE_SERIAL ON CACHE BOOL "Enable USB CDC-ACM virtual serial COM port")
endif()
if(ENABLE_SERIAL)
    set(ENABLE_SERIAL ON)
else()
    set(ENABLE_SERIAL OFF)
endif()

# 5. Sustained-load Audio Benchmark Mode
if(DEFINED CFG_BENCH_MODE)
    set(MCUNO_BENCH_MODE "${CFG_BENCH_MODE}" CACHE BOOL "Build sustained-load audio benchmark" FORCE)
elseif(NOT DEFINED MCUNO_BENCH_MODE)
    set(MCUNO_BENCH_MODE OFF CACHE BOOL "Build sustained-load audio benchmark")
endif()
if(MCUNO_BENCH_MODE)
    set(MCUNO_BENCH_MODE ON)
else()
    set(MCUNO_BENCH_MODE OFF)
endif()

# 6. Keybed Matrix Scan Frequency in Hz
if(DEFINED CFG_KEYBED_SCAN_FREQ_HZ)
    set(KEYBED_SCAN_FREQ_HZ "${CFG_KEYBED_SCAN_FREQ_HZ}" CACHE STRING "Keybed scan frequency in Hz" FORCE)
elseif(NOT DEFINED KEYBED_SCAN_FREQ_HZ)
    set(KEYBED_SCAN_FREQ_HZ "2000" CACHE STRING "Keybed scan frequency in Hz")
endif()

message(STATUS "=== Config: CPU=${SMK37_CPU_FREQ_MHZ}MHz, AUDIO=${SMK37_AUDIO_SAMPLE_RATE}Hz, SCAN=${KEYBED_SCAN_FREQ_HZ}Hz, DIAG=${STARTUP_DIAGNOSTICS}, SERIAL=${ENABLE_SERIAL}, BENCH_MODE=${MCUNO_BENCH_MODE} ===")

# =========================================================================
# Superbuild Orchestrator (Builds both MCU and Native)
# =========================================================================
if(TARGET_PLATFORM STREQUAL "ALL")
    project(jellybean37_superbuild NONE)
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FIRMWARE_CONFIG_FILE}")
    include(ExternalProject)

    if(NOT DEFINED BIN_OUTPUT_DIR)
        set(BIN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin")
    endif()
    file(MAKE_DIRECTORY ${BIN_OUTPUT_DIR})

    # Generate target-configured isd_config.ini in top-level build directory
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/config/isd_config.ini.in
        ${CMAKE_CURRENT_BINARY_DIR}/isd_config.ini
        @ONLY
    )

    # MCU Target sub-build
    ExternalProject_Add(mcu
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mcu"
        CMAKE_ARGS
            -DTARGET_PLATFORM=MCU
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-pi32v2.cmake
            -DCMAKE_BUILD_TYPE=MinSizeRel
            -DBIN_OUTPUT_DIR=${BIN_OUTPUT_DIR}
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            -DSTARTUP_DIAGNOSTICS=${STARTUP_DIAGNOSTICS}
            -DENABLE_SERIAL=${ENABLE_SERIAL}
            -DMCUNO_BENCH_MODE=${MCUNO_BENCH_MODE}
            -DSMK37_CPU_FREQ_MHZ=${SMK37_CPU_FREQ_MHZ}
            -DSMK37_AUDIO_SAMPLE_RATE=${SMK37_AUDIO_SAMPLE_RATE}
        BUILD_ALWAYS 1
        INSTALL_COMMAND ""
        USES_TERMINAL_BUILD 1
        USES_TERMINAL_CONFIGURE 1
    )

    # Native Simulator sub-build
    ExternalProject_Add(native
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/native"
        CMAKE_ARGS
            -DTARGET_PLATFORM=NATIVE
            -DCMAKE_BUILD_TYPE=Debug
            -DBIN_OUTPUT_DIR=${BIN_OUTPUT_DIR}
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            -DSTARTUP_DIAGNOSTICS=${STARTUP_DIAGNOSTICS}
            -DENABLE_SERIAL=${ENABLE_SERIAL}
            -DMCUNO_BENCH_MODE=${MCUNO_BENCH_MODE}
            -DSMK37_CPU_FREQ_MHZ=${SMK37_CPU_FREQ_MHZ}
            -DSMK37_AUDIO_SAMPLE_RATE=${SMK37_AUDIO_SAMPLE_RATE}
        BUILD_ALWAYS 1
        INSTALL_COMMAND ""
        USES_TERMINAL_BUILD 1
        USES_TERMINAL_CONFIGURE 1
    )

    # Forward package_firmware target in Superbuild
    add_custom_target(
        package_firmware
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}/mcu --target package_firmware
        DEPENDS mcu
        COMMENT "Packaging firmware via MCU sub-build"
    )

    # Forward ota target in Superbuild
    add_custom_target(
        ota
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}/mcu --target ota
        DEPENDS mcu
        COMMENT "Building OTA binary via MCU sub-build"
    )

    # Generate unified compile_commands.json for language servers (clangd)
    add_custom_target(compile_commands ALL
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/merge_compile_commands.py
            ${CMAKE_CURRENT_BINARY_DIR}/native/compile_commands.json
            ${CMAKE_CURRENT_BINARY_DIR}/mcu/compile_commands.json
            ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
        DEPENDS native mcu
        COMMENT "Generating unified compile_commands.json for language servers"
    )

    add_custom_target(clean_all
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}/mcu --target clean
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}/native --target clean
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${BIN_OUTPUT_DIR}
        COMMENT "Cleaning all sub-projects and bin artifacts"
    )

    return()
endif()

# =========================================================================
# Single-Target Build (MCU or NATIVE)
# =========================================================================
project(jellybean37 C CXX ASM)
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FIRMWARE_CONFIG_FILE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Global include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/external/synth_dexed_msfa
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mcuno-32/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/external/lvgl/src
)

# Generate target-configured isd_config.ini in build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config/isd_config.ini.in
    ${CMAKE_CURRENT_BINARY_DIR}/isd_config.ini
    @ONLY
)

if(STARTUP_DIAGNOSTICS)
    add_compile_definitions(STARTUP_DIAGNOSTICS=1)
else()
    add_compile_definitions(STARTUP_DIAGNOSTICS=0)
endif()

if(ENABLE_SERIAL)
    add_compile_definitions(ENABLE_SERIAL=1)
else()
    add_compile_definitions(ENABLE_SERIAL=0)
endif()

if(MCUNO_BENCH_MODE)
    add_compile_definitions(MCUNO_BENCH_MODE=1)
endif()

add_compile_definitions(
    SMK37_PRO
    LG_N=5
    CPU_FREQ_MHZ=${SMK37_CPU_FREQ_MHZ}
    "CPU_FREQ_HZ=(${SMK37_CPU_FREQ_MHZ}000000UL)"
    AUDIO_SAMPLE_RATE=${SMK37_AUDIO_SAMPLE_RATE}
    KEYBED_SCAN_FREQ_HZ=${KEYBED_SCAN_FREQ_HZ}
)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include;${CMAKE_CURRENT_SOURCE_DIR}/config/dexed_compat.h>")

# Canonical output directory for binaries
if(NOT DEFINED BIN_OUTPUT_DIR)
    set(BIN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin")
endif()
file(MAKE_DIRECTORY ${BIN_OUTPUT_DIR})

# LVGL v8 Library Target
file(GLOB_RECURSE LVGL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/external/lvgl/src/*.c
)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# Shared Application Sources
set(SHARED_APP_SOURCES
    src/hal/hal_input.c
    src/app/patch_banks.c
    src/app/mcuno60_patches.c
    src/app/dexed_shim.cpp
    src/app/juno_shim.cpp
    src/app/synth_engine.cpp
    src/app/startup_anim.c
    src/app/ui_sample.c
    src/app/app_main.c
    external/mcuno-32/src/chorus_mn3009.c
    external/mcuno-32/src/ir3109_vcf.c
    external/mcuno-32/src/juno_engine.c
    external/mcuno-32/src/master_hpf.c
    external/mcuno-32/src/polyblep.c
    external/mcuno-32/src/voice.c
    external/synth_dexed_msfa/dexed.cpp
    external/synth_dexed_msfa/dx7note.cpp
    external/synth_dexed_msfa/env.cpp
    external/synth_dexed_msfa/exp2.cpp
    external/synth_dexed_msfa/fm_core.cpp
    external/synth_dexed_msfa/fm_op_kernel.cpp
    external/synth_dexed_msfa/freqlut.cpp
    external/synth_dexed_msfa/lfo.cpp
    external/synth_dexed_msfa/pitchenv.cpp
    external/synth_dexed_msfa/porta.cpp
    external/synth_dexed_msfa/sin.cpp
    external/synth_dexed_msfa/EngineMsfa.cpp
)

# Audio hot path: override MinSizeRel -Oz with -O2
set(AUDIO_HOT_SOURCES
    external/mcuno-32/src/chorus_mn3009.c
    external/mcuno-32/src/ir3109_vcf.c
    external/mcuno-32/src/juno_engine.c
    external/mcuno-32/src/master_hpf.c
    external/mcuno-32/src/polyblep.c
    external/mcuno-32/src/voice.c
    src/app/dexed_shim.cpp
    src/app/juno_shim.cpp
    src/app/synth_engine.cpp
    external/synth_dexed_msfa/dexed.cpp
    external/synth_dexed_msfa/dx7note.cpp
    external/synth_dexed_msfa/env.cpp
    external/synth_dexed_msfa/exp2.cpp
    external/synth_dexed_msfa/fm_core.cpp
    external/synth_dexed_msfa/fm_op_kernel.cpp
    external/synth_dexed_msfa/freqlut.cpp
    external/synth_dexed_msfa/lfo.cpp
    external/synth_dexed_msfa/pitchenv.cpp
    external/synth_dexed_msfa/porta.cpp
    external/synth_dexed_msfa/sin.cpp
    external/synth_dexed_msfa/EngineMsfa.cpp
)
set_source_files_properties(${AUDIO_HOT_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-O2"
)

# =========================================================================
# Platform 1: Native Host SDL2 Hardware Simulator (x86_64 Linux)
# =========================================================================
if(TARGET_PLATFORM STREQUAL "NATIVE")
    add_compile_definitions(
        TARGET_NATIVE
        LV_CONF_INCLUDE_SIMPLE
    )

    find_package(SDL2 REQUIRED)

    include_directories(
        ${SDL2_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sim
    )

    set(SIM_SOURCES
        ${SHARED_APP_SOURCES}
        src/sim/sim_display.cpp
        src/sim/sim_audio.cpp
        src/sim/sim_synth.cpp
        src/sim/sim_time.cpp
        src/sim/sim_input.cpp
        src/sim/sim_midi.cpp
        src/sim/sim_led.cpp
        src/sim/sim_main.cpp
    )

    add_executable(jellybean37_sim ${SIM_SOURCES})
    target_link_libraries(jellybean37_sim PRIVATE lvgl SDL2::SDL2 m)
    set_target_properties(jellybean37_sim PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
    )

    message(STATUS "Native simulator target configured -> ${BIN_OUTPUT_DIR}/jellybean37_sim")

# =========================================================================
# Platform 2: JieLi PI32v2 MCU Cross-Compilation Target (AC791N / WL82)
# =========================================================================
elseif(TARGET_PLATFORM STREQUAL "MCU")
    if(NOT DEFINED JIELI_TOOLCHAIN_PATH)
        if(DEFINED ENV{JIELI_TOOLCHAIN_PATH})
            set(JIELI_TOOLCHAIN_PATH "$ENV{JIELI_TOOLCHAIN_PATH}")
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/pi32v2")
            set(JIELI_TOOLCHAIN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/pi32v2")
        endif()
    endif()

    set(TOOLCHAIN_ROOT "${JIELI_TOOLCHAIN_PATH}")
    set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/jieli-sdk")
    set(VENDOR_OTHER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/jieli-other/ac791n-ota-loader")

    # Project & SDK include directories
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mcu
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mcu/compat
        ${SDK_ROOT}/include_lib/c++
        ${SDK_ROOT}/include_lib/c++/include
        ${SDK_ROOT}/include_lib/newlib/include
        ${SDK_ROOT}/include_lib/c++/simple_pthread
        ${SDK_ROOT}/include_lib
        ${SDK_ROOT}/include_lib/driver
        ${SDK_ROOT}/include_lib/driver/device
        ${SDK_ROOT}/include_lib/driver/cpu/wl82
        ${SDK_ROOT}/include_lib/system
        ${SDK_ROOT}/include_lib/system/generic
        ${SDK_ROOT}/include_lib/system/os
        ${SDK_ROOT}/include_lib/system/os/FreeRTOS
        ${SDK_ROOT}/include_lib/system/os/FreeRTOS/pi32v2
    )

    # System headers from cross toolchain
    include_directories(SYSTEM
        ${TOOLCHAIN_ROOT}/include
        ${TOOLCHAIN_ROOT}/lib/clang/4.0.1/include
    )

    add_compile_definitions(
        TARGET_MCU
        CONFIG_FREE_RTOS_ENABLE
        CONFIG_THREAD_ENABLE
        CONFIG_CPU_WL82
        LV_CONF_INCLUDE_SIMPLE
    )

    # SDK & Runtime Libraries
    set(SDK_LIBS
        ${SDK_ROOT}/include_lib/c++/libstdc++/libc++.a
        ${SDK_ROOT}/include_lib/c++/libstdc++/libc++abi.a
        ${TOOLCHAIN_ROOT}/lib/r3/libc.a
        ${TOOLCHAIN_ROOT}/lib/r3/libcompiler-rt.a
        ${TOOLCHAIN_ROOT}/lib/r3/libm.a
    )

    # Thinned Vendor Shim Archive
    set(VENDOR_SHIM_LIB "${CMAKE_CURRENT_BINARY_DIR}/libvendor_shim.a")
    set(VENDOR_SHIM_STAGING "${CMAKE_CURRENT_BINARY_DIR}/vendor_shim_staging")
    set(VENDOR_SYSTEM_LIB "${SDK_ROOT}/cpu/wl82/liba/system.a")
    set(VENDOR_CPU_LIB "${SDK_ROOT}/cpu/wl82/liba/cpu.a")
    set(VENDOR_OS_LIB "${VENDOR_OTHER_ROOT}/uboot/include_lib/liba/wl82/net_ota/os_lib.a")

    add_custom_command(
        OUTPUT ${VENDOR_SHIM_LIB}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_vendor_shim.py
                --ar ${TOOLCHAIN_ROOT}/bin/ar
                --output ${VENDOR_SHIM_LIB}
                --staging-dir ${VENDOR_SHIM_STAGING}
                --extract ${VENDOR_SYSTEM_LIB}:malloc.c.o
                          ${VENDOR_SYSTEM_LIB}:printf-stdarg.c.o
                          ${VENDOR_SYSTEM_LIB}:fcvt.c.o
                          ${VENDOR_SYSTEM_LIB}:hwi.c.o
                          ${VENDOR_SYSTEM_LIB}:wait_completion.c.o
                          ${VENDOR_SYSTEM_LIB}:timer.c.o:jieli_sw_timer.c.o
                          ${VENDOR_OS_LIB}:tasks.o
                          ${VENDOR_OS_LIB}:queue.o
                          ${VENDOR_OS_LIB}:list.o
                          ${VENDOR_OS_LIB}:event_groups.o
                          ${VENDOR_OS_LIB}:port.o
                          ${VENDOR_OS_LIB}:port_asm.o
                          ${VENDOR_OS_LIB}:task.o
                          ${VENDOR_OS_LIB}:os_api.o
                          ${VENDOR_CPU_LIB}:cache.c.o
                          ${VENDOR_CPU_LIB}:p33.c.o
                          ${VENDOR_CPU_LIB}:wdt.c.o
                          ${VENDOR_CPU_LIB}:clock.c.o
                          ${VENDOR_CPU_LIB}:delay.c.o
                          ${VENDOR_CPU_LIB}:gpio.c.o
                          ${VENDOR_CPU_LIB}:boot.c.o
                          ${VENDOR_CPU_LIB}:crc16.c.o
                          ${VENDOR_CPU_LIB}:adc.c.o:jieli_audio_adc.c.o
                          ${VENDOR_CPU_LIB}:iis.c.o
                          ${VENDOR_CPU_LIB}:imd.c.o
                          ${VENDOR_CPU_LIB}:spi0.c.o
                          ${VENDOR_CPU_LIB}:encryption.c.o
                          ${VENDOR_CPU_LIB}:timer.c.o:jieli_hw_timer.c.o
                          ${VENDOR_CPU_LIB}:usb.c.o
                          ${VENDOR_CPU_LIB}:usb_phy.c.o
                          ${VENDOR_CPU_LIB}:vm_sfc.c.o
        DEPENDS ${VENDOR_SYSTEM_LIB} ${VENDOR_CPU_LIB} ${VENDOR_OS_LIB} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_vendor_shim.py
        COMMENT "Building thinned vendor shim archive: libvendor_shim.a"
    )
    add_custom_target(vendor_shim DEPENDS ${VENDOR_SHIM_LIB})

    # MCU Application Sources
    set(MCU_APP_SOURCES
        ${SHARED_APP_SOURCES}
        src/mcu/crt0.s
        src/mcu/stubs.c
        src/mcu/st7789.c
        src/mcu/build_id.c
        src/mcu/mcu_display.c
        src/mcu/mcu_audio.c
        src/mcu/mcu_synth.c
        src/mcu/mcu_time.c
        src/mcu/mcu_input.c
        src/mcu/mcu_led.c
        src/mcu/mcu_usb_desc.c
        src/mcu/mcu_usb.c
        src/mcu/mcu_midi.c
        src/mcu/mcu_ota.c
        src/mcu/mcu_main.c
    )

    if(STARTUP_DIAGNOSTICS)
        list(APPEND MCU_APP_SOURCES
            src/mcu/diag.c
            src/mcu/selftest.c
            src/mcu/bench.c
        )
    endif()

    if(MCUNO_BENCH_MODE)
        list(APPEND MCU_APP_SOURCES src/mcu/bench_load.c)
    endif()

    # Vendor SAR ADC Driver Library
    add_library(vendor_sar_adc STATIC
        ${SDK_ROOT}/cpu/wl82/key/adc_api.c
    )

    # 1. Flash Executable Target (XIP)
    add_executable(jellybean37_flash.elf ${MCU_APP_SOURCES})
    add_dependencies(jellybean37_flash.elf vendor_shim)
    target_compile_definitions(jellybean37_flash.elf PRIVATE TARGET_FLASH=1)
    target_link_libraries(jellybean37_flash.elf PRIVATE lvgl vendor_sar_adc ${VENDOR_SHIM_LIB} ${SDK_LIBS})
    target_link_options(jellybean37_flash.elf PRIVATE
        -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/ld/flash_app.ld
        -Wl,--build-id=sha1
        -Wl,--gc-sections
        -Wl,--plugin-opt=-pi32v2-large-program=true
    )
    set_target_properties(jellybean37_flash.elf PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/flash_app.ld"
    )

    # 2. RAM-only Executable Target (0x01C02000 direct load)
    add_executable(jellybean37_ram.elf ${MCU_APP_SOURCES})
    add_dependencies(jellybean37_ram.elf vendor_shim)
    target_compile_definitions(jellybean37_ram.elf PRIVATE TARGET_RAM=1)
    target_link_libraries(jellybean37_ram.elf PRIVATE lvgl vendor_sar_adc ${VENDOR_SHIM_LIB} ${SDK_LIBS})
    target_link_options(jellybean37_ram.elf PRIVATE
        -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/ld/ram_app.ld
        -Wl,--build-id=sha1
        -Wl,--gc-sections
        -Wl,--plugin-opt=-pi32v2-large-program=true
    )
    set_target_properties(jellybean37_ram.elf PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ld/ram_app.ld"
    )

    # Binary extraction and firmware packaging
    find_program(READELF_TOOL NAMES llvm-readelf readelf arm-none-eabi-readelf PATHS /usr/bin)

    add_custom_command(
        TARGET jellybean37_flash.elf POST_BUILD
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/elf2bin.sh $<TARGET_FILE:jellybean37_flash.elf> ${BIN_OUTPUT_DIR}/jellybean37_flash.bin
        COMMENT "Extracting raw binary ${BIN_OUTPUT_DIR}/jellybean37_flash.bin"
    )

    add_custom_command(
        TARGET jellybean37_flash.elf POST_BUILD
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_build_id.py
                --readelf ${READELF_TOOL}
                --elf $<TARGET_FILE:jellybean37_flash.elf>
                --output ${CMAKE_BINARY_DIR}/build_stamp.txt
        COMMENT "Extracting Build-ID and writing ${CMAKE_BINARY_DIR}/build_stamp.txt"
    )

    add_custom_command(
        TARGET jellybean37_ram.elf POST_BUILD
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/elf2bin.sh $<TARGET_FILE:jellybean37_ram.elf> ${BIN_OUTPUT_DIR}/jellybean37_ram.bin
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/pad512.py ${BIN_OUTPUT_DIR}/jellybean37_ram.bin 512
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_build_id.py
                --readelf ${READELF_TOOL}
                --elf $<TARGET_FILE:jellybean37_ram.elf>
                --output ${CMAKE_BINARY_DIR}/build_stamp_ram.txt
        COMMENT "Extracting raw binary, padding to 512 bytes, and extracting RAM Build-ID"
    )

    # Dedicated USB-MIDI OTA Loader for SMK-37 Pro
    set(OTA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/mcu/ota")
    set(OTA_ELF "${CMAKE_CURRENT_BINARY_DIR}/ota.elf")
    set(OTA_RAW_BIN "${CMAKE_CURRENT_BINARY_DIR}/usb_hid_ota.bin")
    set(OTA_BIN "${BIN_OUTPUT_DIR}/ota.bin")

    add_custom_command(
        OUTPUT ${OTA_BIN} ${OTA_RAW_BIN} ${OTA_ELF}
        COMMAND ${CMAKE_C_COMPILER} -target pi32v2 -mcpu=r3 -Os -fno-exceptions -fno-rtti
                -I ${OTA_DIR}
                -I ${SDK_ROOT}/include_lib
                -I ${SDK_ROOT}/include_lib/newlib/include
                -I ${SDK_ROOT}/include_lib/driver/cpu/wl82
                -I ${SDK_ROOT}/include_lib/system
                -I ${SDK_ROOT}/include_lib/system/generic
                ${OTA_DIR}/crt0_ota.S
                ${OTA_DIR}/ota.c
                -Wl,-T,${OTA_DIR}/ota.ld
                -Wl,--gc-sections
                -o ${OTA_ELF}
        COMMAND ${CMAKE_OBJCOPY} -O binary -j .text ${OTA_ELF} ${OTA_RAW_BIN}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/ota_loader.py -o ${OTA_BIN} 0x1C0A800 ${OTA_RAW_BIN} compression ram
        DEPENDS ${OTA_DIR}/crt0_ota.S
                ${OTA_DIR}/ota.ld
                ${OTA_DIR}/ota.c
                ${OTA_DIR}/ota_core.c
                ${OTA_DIR}/ota_protocol.c
                ${OTA_DIR}/ota_usb.c
                ${OTA_DIR}/ota_flash.h
                ${OTA_DIR}/ota_common.h
                ${OTA_DIR}/ota_sys.h
                ${OTA_DIR}/ota_time.h
                ${OTA_DIR}/ota_usb_hw.h
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/ota_loader.py
        COMMENT "Building dedicated SMK-37 Pro USB-MIDI Update Loader and packing ${OTA_BIN}"
    )

    add_custom_target(ota ALL
        DEPENDS ${OTA_BIN}
    )

    add_custom_target(
        package_firmware
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_images.py
                ${BIN_OUTPUT_DIR}/jellybean37_flash.bin
                ${BIN_OUTPUT_DIR}
                --ini ${CMAKE_CURRENT_BINARY_DIR}/isd_config.ini
                --cpu-freq ${SMK37_CPU_FREQ_MHZ}
                --version-str "jellybean37_001"
                --ota-bin ${OTA_BIN}
        DEPENDS jellybean37_flash.elf jellybean37_ram.elf ota
        COMMENT "Packaging jellybean37_flash_image.bin, jellybean37.ufw, jellybean37.fw, and jellybean37_001.fwsc"
    )
endif()
