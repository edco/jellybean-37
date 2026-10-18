# SMK-37 Pro Visual Diagnostics Reference

The SMK-37 Pro LCD firmware diagnostic subsystem renders a structured matrix of 15x15 pixel visual flags across the 240x240 ST7789 display.

---

## 1. Grid Geometry & Color Architecture

- **Background Canvas**: Mid-Grey (`RGB(128, 128, 128)` = `0x1084` byte-swapped RGB565).
- **Resolution**: 240x240 pixels $\rightarrow$ 16 columns $\times$ 16 rows ($16 \times 15 = 240\text{ px}$).
- **Square Structure** (for each column $c \in [0..15]$, row $r \in [0..15]$):
  - $X = c \times 15$, $Y = r \times 15$
  - **Outer Frame**: 15x15 pixels (leaving 1px mid-grey margin around adjacent squares)
  - **Outline**: 13x13 pixel white square at $(X+1, Y+1)$
  - **Core**: 11x11 pixel filled square at $(X+2, Y+2)$

```
+---------------------------------------+  (15x15 mid-grey boundary)
|  +---------------------------------+  |
|  | ############################### |  |  (13x13 white outline)
|  | #  +-------------------------+  # |  |
|  | #  |                         |  # |  |
|  | #  |   11x11 Coloured Core   |  # |  |
|  | #  |                         |  # |  |
|  | #  +-------------------------+  # |  |
|  | ############################### |  |
|  +---------------------------------+  |
+---------------------------------------+
```

---

## 2. Color Systems

### 3-Bit RGB Color Values (Row 0 & Row 1)
Each flag represents a 3-bit number with binary channels $(R, G, B)$ mapped to full brightness or off:

| Value | Binary (R G B) | Color | Abbr | Hex (Byte-Swapped RGB565) |
|---|---|---|---|---|
| **0** | `0 0 0` | Black | **K** | `0x0000` |
| **1** | `0 0 1` | Blue | **B** | `0x1F00` |
| **2** | `0 1 0` | Green | **G** | `0xE007` |
| **3** | `0 1 1` | Cyan | **C** | `0xFF07` |
| **4** | `1 0 0` | Red | **R** | `0x00F8` |
| **5** | `1 0 1` | Magenta | **M** | `0x1FF8` |
| **6** | `1 1 0` | Yellow | **Y** | `0xE0FF` |
| **7** | `1 1 1` | White | **W** | `0xFFFF` |

### Sequential Progress Flags (RYGCBM Rotation)
Used for step-by-step progress tracking:
1. **Red** (R) $\rightarrow$ 2. **Yellow** (Y) $\rightarrow$ 3. **Green** (G) $\rightarrow$ 4. **Cyan** (C) $\rightarrow$ 5. **Blue** (B) $\rightarrow$ 6. **Magenta** (M) $\rightarrow$ (loops back to Red)

---

## 3. Screen Row Allocations

### Row 0 ($Y = 0\dots 14$): 24-Bit Build Stamp
- Consists of **8 flags** (Columns 0 to 7).
- Each flag encodes 3 bits of a random 24-bit build identifier generated at compile time.
- Column 0 represents bits 23..21; Column 7 represents bits 2..0.
- When built, the build output prints the expected colors: e.g. `[Y W R G B M B W]`.

### Row 1 ($Y = 15\dots 29$): Numeric Output & Boot Mode
- **Column 0: Boot Mode Indicator**
  - 🟥 **Red**: Running directly from SRAM (`tools/run_ram.sh`)
  - 🟦 **Blue**: Running from Flash XIP after cold power cycle (power switch / cable)
  - 🟩 **Green**: Running from Flash XIP after initial write & soft reboot (`tools/flash_uboot.sh`)
- **Columns 1 to 15**: Reserved for numeric diagnostic output (register dumps, error codes).

---

## 4. Predefined Progress Flags

### Row 2 ($Y = 30\dots 44$): Core 0 Early Boot & Hardware Setup

| Flag ID | Col | Color | Predefined Enum | Milestone Description |
|---|---|---|---|---|
| **0** | 0 | 🟥 Red | `DIAG_FLAG_MAIN_ENTRY` | `main()` entry point reached |
| **1** | 1 | 🟨 Yellow | `DIAG_FLAG_WDT_DISABLED` | Watchdog timer disabled |
| **2** | 2 | 🟩 Green | `DIAG_FLAG_INT_INIT_DONE` | Core 0 interrupt controller initialized |
| **3** | 3 | 🟦 Cyan | `DIAG_FLAG_CLOCK_320M_DONE` | System clock switched to 320 MHz (in SRAM) |
| **4** | 4 | 🟦 Blue | `DIAG_FLAG_TIME_INIT_DONE` | Hardware 24MHz Timer1 time base initialized |
| **5** | 5 | 🟪 Magenta | `DIAG_FLAG_ST7789_INIT_DONE` | ST7789 LCD hardware initialized & canvas cleared |
| **6** | 6 | 🟥 Red | `DIAG_FLAG_INPUT_PRE_INIT` | Early input listener callbacks registered |
| **7** | 7 | 🟨 Yellow | `DIAG_FLAG_MCU_INPUT_INIT_DONE`| Keybed & ADC matrix scanner task spawned |
| **8** | 8 | 🟩 Green | `DIAG_FLAG_GUI_TASK_CREATED` | FreeRTOS `#C0gui_task` created |
| **9** | 9 | 🟦 Cyan | `DIAG_FLAG_CORE1_VEC_FLUSHED` | Core 1 reset vector placed & DCache flushed |
| **10** | 10 | 🟦 Blue | `DIAG_FLAG_SCHEDULER_START` | `vTaskStartScheduler()` called on Core 0 |
| **11** | 11 | 🟪 Magenta | `DIAG_FLAG_CORE1_ENTRY` | Core 1 entry trampoline reached (`cpu1_entry`) |
| **12** | 12 | 🟥 Red | `DIAG_FLAG_CORE1_INT_INIT` | `interrupt_init()` executed on Core 1 |
| **13** | 13 | 🟨 Yellow | `DIAG_FLAG_CORE1_SCHEDULER_START`| `vTaskStartScheduler()` called on Core 1 |
| **14** | 14 | 🟩 Green | `DIAG_FLAG_SPARE_14` | Spare |
| **15** | 15 | 🟦 Cyan | `DIAG_FLAG_SPARE_15` | Spare |

### Row 3 ($Y = 45\dots 59$): Application & Subsystem Bringup

| Flag ID | Col | Color | Predefined Enum | Milestone Description |
|---|---|---|---|---|
| **16** | 0 | 🟦 Blue | `DIAG_FLAG_GUI_TASK_ENTRY` | `vGuiTask` scheduled and running on Core 0 |
| **17** | 1 | 🟪 Magenta | `DIAG_FLAG_LVGL_INIT_DONE` | `lv_init()` completed |
| **18** | 2 | 🟥 Red | `DIAG_FLAG_HAL_DISP_INIT_DONE`| `hal_display_init()` completed |
| **19** | 3 | 🟨 Yellow | `DIAG_FLAG_APP_INIT_ENTRY` | `app_init()` entered |
| **20** | 4 | 🟩 Green | `DIAG_FLAG_SYNTH_INIT_DONE` | `synth_init()` completed |
| **21** | 5 | 🟦 Cyan | `DIAG_FLAG_INPUT_POST_INIT` | `app_register_input()` completed |
| **22** | 6 | 🟦 Blue | `DIAG_FLAG_HAL_AUDIO_INIT_DONE`| `hal_audio_init()` completed |
| **23** | 7 | 🟪 Magenta | `DIAG_FLAG_HAL_AUDIO_START_DONE`| `hal_audio_start()` completed |
| **24** | 8 | 🟥 Red | `DIAG_FLAG_UI_SAMPLE_INIT_DONE`| `ui_sample_init()` (LVGL widgets) completed |
| **25** | 9 | 🟨 Yellow | `DIAG_FLAG_APP_INIT_DONE` | `app_init()` returned cleanly to `vGuiTask` |
| **26** | 10 | 🟩 Green | `DIAG_FLAG_GUI_LOOP_FIRST_TICK`| `vGuiTask` while(1) loop entered and running |
