# Jellybean-37

Open dual-target firmware and hardware simulator for the **M-VAVE SMK-37 Pro** keyboard synthesizer (JieLi AC791N / WL82 32-bit RISC core). This is currently a fairly rough testbed for the MCUno-32 engine and various optimisations to improve latency and performance. It's probably not much use to anyone who just wants to use the keyboard as an intsrument/MIDI controller, but I encourage other firmware authors to use the code/ideas here for their own projects.

---

## User Features

- **Dual Sound Engines**:
  - **Roland Juno-60 ([`external/mcuno-32`](external/mcuno-32))**: 6-voice polyphonic virtual analog synth with authentic PolyBLEP oscillators, IR3109 4-pole OTA filter, non-linear resonance, master HPF, and stereo MN3009 BBD chorus emulation.
  - **Yamaha DX7 ([`external/synth_dexed_msfa`](external/synth_dexed_msfa))**: 16-voice polyphonic 6-operator FM synthesis engine using the Google MSFA core with full factory DX7 patch bank support.
  - K1 controls volume, K2 chooses engine, K3 selects patch bank, K4 selects patch.
- **Performance improvements**
  - Low latency
  - 48kHz DAC/USB out
- **Aftertouch**
  - Aftertouch range on the pads is separated from the normal velocity range so pressing a pad doesn't immediately trigger aftertouch.
- **RAM Loader**
  - Try the firmware without ever flashing it to your device. With the RAM loader it runs entirely from SRAM and disappears on reboot.

## Developer Features

- **Dual-Target Development**:
  - **Host Simulator (`jellybean37_sim`)**: Instant desktop testing with x86_64 Linux SDL2 emulation of the 240x240 LCD display, 48 kHz stereo audio, PC keyboard controls, and host MIDI.
  - **MCU Firmware (`jellybean37_flash.elf`, `jellybean37_ram.elf`)**: Bare-metal embedded target with FreeRTOS multitasking, double-buffered I2S DMA audio, ST7789 SPI display driver, ADC keybed velocity matrix scanner, and USB MIDI + CDC composite device.
- **Clean Codebase**:
  - Independent, self-contained project with zero dependencies on factory firmware or firmware-derived binary blobs.
  - Submodule-based dependency for [LVGL v8.4.0](https://github.com/lvgl/lvgl).
  - Standalone Python tools: direct-to-RAM loader ([`tools/jellybean_loadram.py`](tools/jellybean_loadram.py)) and USB-MIDI OTA flasher ([`tools/jellybean_install.py`](tools/jellybean_install.py)).
- **Asymmetric Multiprocesssing**:
  - FreeRTOS on Core 0 runs UI and keybed scanning. Code runs in place from flash.
  - Bare metal Core 1 runs synth engines and feeds DMA audio output. Hot code runs from SRAM.
  - The default JieLi FreeRTOS binary is hard-coded to SMP, but the one from their example OTA loader is single core. I've created a cursed hybrid build. It seems fairly stable (though I haven't tried things like the Bluetooth stack).

---

## Quickstart

### WARNING

This project is a work in progress and is provided with absolutely no warranty. There may be bugs in the tools or firmware that could brick your device, corrupt your operating system, and incur the emnity of corvids. Use at your own risk.

### 1. Prerequisites

- CMake 3.20+
- Ninja build system
- Python 3.8+
- Host packages (for simulator): `libsdl2-dev`, `build-essential`

### 2. Setup Toolchain

Run the automated toolchain installer to set up the JieLi PI32v2 compiler and post-build tools in `toolchain/` (gitignored):

```bash
./tools/setup_toolchain.sh
```

### 3. Build

#### Native SDL2 Desktop Simulator
```bash
cmake --preset native
ninja -C build/native

# Run the simulator:
./build/bin/jellybean37_sim
```

#### MCU Embedded Firmware Target
```bash
cmake --preset mcu
ninja -C build/mcu
```

#### Superbuild (Both Targets)
```bash
cmake --preset default
ninja -C build
```

#### Package Production Firmware Images
```bash
ninja -C build/mcu package_firmware
```
Outputs in `build/bin/`:
- `jellybean37_flash.elf` & `jellybean37_flash.bin`: Compiled flash app binaries
- `jellybean37_ram.elf` & `jellybean37_ram.bin`: Compiled direct-to-RAM binaries
- `jellybean37.ufw` & `jellybean37.fw`: Native JieLi update containers
- `jellybean37_001.fwsc`: M-VAVE watermarked OTA canister

---

## Flashing & Device Execution

### 1. Safe Direct-to-RAM Execution (Default)
Loads the RAM image directly into SRAM at `0x01C02000` via MaskROM SCSI. Reboots the device automatically via DFU SysEx:

```bash
python3 tools/jellybean_loadram.py build/bin/jellybean37_ram.bin
```
> [!TIP]
> RAM mode executes in memory without touching or wearing out NOR flash. If the device crashes or power is cycled, the original flash remains completely untouched.

### 2. Burn to SPI NOR Flash
Flashes the `.fwsc` canister over USB-MIDI using the dedicated 2-stage pull-based OTA update engine:

```bash
./tools/jellybean_install.py build/bin/jellybean37_001.fwsc
```

---

## Directory Overview

```
jellybean-37/
├── CMakeLists.txt              # Unified Dual-Target & Superbuild CMake
├── CMakePresets.json           # Presets: default, mcu, native
├── AGENTS.md                   # Agent guidelines & C++ conventions
├── README.md                   # This documentation
├── config/                     # FreeRTOSConfig, LVGL, Dexed compat headers
├── ld/                         # flash_app.ld (XIP) & ram_app.ld (SRAM)
├── src/
│   ├── app/                    # Application logic, UI, and synth shims
│   ├── hal/                    # Hardware Abstraction Layer interfaces
│   ├── mcu/                    # AC791N hardware target implementation
│   └── sim/                    # SDL2 desktop hardware simulator
├── external/
│   ├── lvgl/                   # LVGL v8.4.0 (submodule)
│   ├── mcuno-32/               # Roland Juno-60 engine (submodule)
│   └── synth_dexed_msfa/       # Yamaha DX7 MSFA engine (vendored)
├── vendor/
│   ├── jieli-sdk/              # Minimal JieLi AIoT SDK headers & libs
│   └── jieli-other/            # JieLi OTA bootloader & loader trees
├── assets/factory/             # Factory packaging assets (bootloader, cfg)
└── tools/                      # Flasher, packager, build shim, toolchain scripts
```

---

## License

- **Original Project Code**: Licensed under the Mozilla Public License v2.0 (MPL-2.0).
- `external/mcuno-32`: Mozilla Public License v2.0 (MPL-2.0).
- `external/synth_dexed_msfa`: Contains Apache License 2.0 components (Google MSFA) and GNU General Public License v3.0 (GPL-3.0) components (`dexed.cpp`, `dexed.h` by Pascal Gauthier and Holger Wirtz).
- `external/lvgl`: MIT License.
- `vendor/jieli-*`: Proprietary JieLi Technology headers and precompiled runtimes.

### Combined Work License (GPL-3.0)
Because this project incorporates and links with GPL-3.0 code from `external/synth_dexed_msfa`, the project as a whole and its distributed firmware and simulator binaries are licensed under the terms of the **GNU General Public License, Version 3 (GPL-3.0)** (or, at your option, any later version).
