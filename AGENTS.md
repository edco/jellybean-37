# Guidelines for AI Agents Working on `jellybean-37`

This document defines the architecture rules, build constraints, and integration patterns for developing features in [`jellybean-37`](.). Always follow these principles to keep builds clean, robust, and reproducible.

---

## 1. Dual-Target Build Architecture

The firmware codebase is designed around a unified hardware abstraction layer (HAL) that targets two platforms simultaneously:

1. **`NATIVE` (Host Hardware Simulator)**:
   * Compiles for `x86_64` Linux using system GCC/Clang and SDL2.
   * Executable: [`build/bin/jellybean37_sim`](build/bin/jellybean37_sim).
   * Emulates the 240x240 ST7789 display (scaled), 48 kHz stereo audio, 37-key velocity-sensitive keybed, drum pads, faders, knobs, and buttons.
2. **`MCU` (Embedded Hardware Target)**:
   * Cross-compiles for the JieLi AC791N / WL82 32-bit RISC core (`pi32v2:r3`) using the JieLi Clang 4.0.1 toolchain.
   * Outputs: [`jellybean37_flash.elf`](build/bin/jellybean37_flash.elf) (`jellybean37_flash.bin`) and [`jellybean37_ram.elf`](build/bin/jellybean37_ram.elf) (`jellybean37_ram.bin`).

### Mandatory Verification Rule
**Every feature, fix, or dependency added to `jellybean-37` MUST compile cleanly on BOTH targets.**

```bash
# Build both NATIVE and MCU targets simultaneously via Superbuild
cmake --preset default
ninja -C build

# Fast individual target builds during iteration
ninja -C build/native
ninja -C build/mcu

# Package complete flash and OTA canister containers
ninja -C build/mcu package_firmware
```

---

## 2. Zero-Modification Rule for Vendor SDK (`vendor/jieli-sdk`, `vendor/jieli-other`)

**Never modify vendor SDK files in `vendor/`.**

Vendor SDK files are upstream reference files and may be updated or re-extracted. If an SDK header has missing dependencies, naming collisions, or language linkage conflicts (e.g. missing `extern "C"`), resolve them externally:

### Pattern A: Overlay Wrappers with `#include_next`
If a vendor header lacks `extern "C"` guards (such as `generic/printf.h`):
1. Create a matching wrapper in [`src/mcu/compat/`](src/mcu/compat/).
2. Place `src/mcu/compat` **first** in `include_directories()`.
3. Use `#include_next` inside `extern "C" { ... }`:

```c
#ifndef _COMPAT_GENERIC_PRINTF_H_
#define _COMPAT_GENERIC_PRINTF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include_next "generic/printf.h"

#ifdef __cplusplus
}
#endif

#endif
```

### Pattern B: Forced Include for Target-Specific Prerequisites
If SDK headers expect specific definitions before inclusion (e.g. `FreeRTOSConfig.h` expecting `local_irq_disable()` and `ASSERT()` from `asm/cpu.h` when included via C++ standard headers), inject them using CMake's forced-include options rather than patching the SDK.

---

## 3. Zero-Modification Rule for External Subtrees & Submodules

Third-party libraries imported as git submodules or vendored trees (`external/mcuno-32`, `external/lvgl`, `external/synth_dexed_msfa`) should remain cleanly separated from application code.

### Pattern A: Global Forced-Include Compatibility Header
Supply missing environment definitions (types, constants, Arduino macros) via [`config/dexed_compat.h`](config/dexed_compat.h):
* **Types & Constants**: `float32_t`, `AUDIO_BLOCK_SAMPLES`, `M_PI`, `constrain(amt, low, high)`.
* **Timing**: Map Arduino `millis()` to HAL `hal_get_ms()`.
* **Arduino Prints**: Provide a zero-cost inline dummy struct:
  ```cpp
  struct DummySerial {
      template<typename... Args>
      void printf(const char *, Args &&...) {}
      template<typename T>
      void print(const T &) {}
      template<typename T>
      void println(const T &) {}
  };
  static DummySerial Serial __attribute__((unused));
  ```

### Pattern B: OOP Subclassing Over Modifying Upstream Classes
If a library declares state or render methods as `protected:` (e.g. `Dexed::getSamples`):
* **Do NOT modify library headers to change visibility.**
* In the application wrapper ([`src/app/dexed_shim.cpp`](src/app/dexed_shim.cpp)), subclass cleanly.

---

## 4. JieLi PI32v2 C++ Toolchain Constraints

The JieLi MCU compiler is Clang 4.0.1 targeting `pi32v2` with specific constraints:

1. **C++ Standard & Exception Flags**:
   * Must use `-std=gnu++14 -fno-exceptions -fno-rtti`.
   * Using standard `c++14` without `gnu++` defines `__STRICT_ANSI__`, masking POSIX/C functions.
   * `-fno-exceptions` is required to avoid bloating firmware image size and emitting calls to `_Unwind_Resume`.
   * A fallback `_Unwind_Resume` stub is maintained in [`src/mcu/stubs.c`](src/mcu/stubs.c) as a linker safeguard.

2. **Header Search Order**:
   * C++ standard library headers (`vendor/jieli-sdk/include_lib/c++/include`) **MUST precede** toolchain C runtime headers (`TOOLCHAIN_ROOT/include`).
   * SDK newlib headers (`include_lib/newlib/include`) and POSIX pthread stubs (`include_lib/c++/simple_pthread`) must be included before system paths.

3. **Linker Libraries**:
   * Any C++ code linking on the MCU target requires both `libc++.a` and `libc++abi.a` added to `SDK_LIBS`:
     ```cmake
     ${SDK_ROOT}/include_lib/c++/libstdc++/libc++.a
     ${SDK_ROOT}/include_lib/c++/libstdc++/libc++abi.a
     ```

4. **CMake Toolchain Gotcha (Compiler Probe)**:
   * **Never place `-include <path>` inside `CMAKE_CXX_FLAGS_INIT` in `cmake/toolchain-pi32v2.cmake`.**
   * CMake's initial compiler sanity check (`CMakeTestCXXCompiler.cmake`) compiles a minimal test file without the project's include directories; putting `-include` in `CMAKE_CXX_FLAGS_INIT` will break CMake configuration.
   * Always apply forced includes via `add_compile_options()` in `CMakeLists.txt`.

---

## 5. Code Style & Naming Conventions (Rust-Style Casing)

All original C and C++ code written for this project must follow modern **Rust-style casing conventions**:

1. **Types (UpperCamelCase / PascalCase)**:
   * Structs, classes, unions, enums, and `typedef` aliases must use `UpperCamelCase`.
   * Examples: `SynthEngine`, `SpscRing`, `KeybedEvent`, `JunoEngineParams`, `AudioBuffer`.

2. **Macros, Enums, and Constants (SCREAMING_SNAKE_CASE)**:
   * Preprocessor `#define` macros, compile-time constants (`const`, `constexpr`), and enum variants must be all-caps with underscores.
   * Examples: `AUDIO_SAMPLE_RATE`, `AUDIO_BLOCK_SAMPLES`, `EVT_NOTE_ON`, `MAX_POLYPHONY`, `DMA_SUB_WORDS`.

3. **Everything Else (lowercase_snake_case)**:
   * Functions, member methods, local variables, function parameters, struct/class fields, and namespaces must use lowercase with underscores.
   * Examples: `synth_init()`, `render_block()`, `note_on()`, `sample_rate`, `frame_count`, `out_left`, `s_volume_percent`.

---

## 6. Summary Checklist Before Submitting Code

- [ ] Does `ninja -C build/native` build without warnings or errors?
- [ ] Does `ninja -C build/mcu` build both Flash (`jellybean37_flash.elf`) and RAM (`jellybean37_ram.elf`) binaries?
- [ ] Did you check `git status` to ensure **no files in `vendor/` were modified**?
- [ ] Are all timing and hardware hooks routed cleanly through the HAL ([`src/hal/`](src/hal/))?
- [ ] Does all newly written C/C++ code adhere to **Rust-style casing** (Types: `PascalCase`, Constants/Macros: `ALL_CAPS`, Functions/Variables: `snake_case`)?
