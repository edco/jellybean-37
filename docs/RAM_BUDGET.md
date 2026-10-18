# SRAM Budget — Direct-to-RAM Target

Numbers for `jellybean37_ram.elf` (loaded at `0x01C02000` by
`tools/jellybean_loadram.py`). The flash XIP target (`flash_app.ld`) is a
different memory map and is **not** constrained by any of this.

## Hardware window

From the SDK's own layout for this part, `resources/fw-AC79_AIoT_SDK/cpu/wl82/sdk_ld_sfc.c:23`:

```c
RAM0_SIZE = 0x1c7fe00 - 0x1c00000 - TLB_SIZE - BOOT_INFO_SIZE - 128;
UPDATA_BEG = 0x1c7fe00 - 128;              /* = 0x01C7FD80 */
```

| | |
|---|---|
| SRAM array ends | `0x01C7FE00` |
| `UPDATA_BEG` (boot-status retention, **do not use**) | `0x01C7FD80` |
| App origin (`ORIGIN + TLB_SIZE`) | `0x01C02000` |
| **Usable app window** | **515,456 B (503.4 KB)** |

`0x01C7FD80` is live: `mcu_midi.c` stores the boot-status word there and it must
survive a soft reset. `mcu_main.c` also flushes the core-1 vector at
`0x01C7FFE0` (above the array, in the boot register block).

`ram_app.ld` previously declared `LENGTH = 480K`, ending at `0x01C7A000` — 23,936
bytes of real SRAM silently unused. Corrected.

## Current map (fits, with 1,472 B slack)

| | bytes |
|---|---|
| `.text` | 257,708 |
| `.data` | 42,460 |
| `.bss` (zeroed by `crt0.s`) | 113,688 |
| **static total** | **413,856** |
| core-1 stack `[_stack_top, _sstack_top)` | 6,144 |
| heap `[HEAP_BEGIN, HEAP_END)` | **93,984** |
| slack before `UPDATA_BEG` | 1,472 |

`_bss_end = 0x01C670B8`, `_stack_top = 0x01C7E000`, `_sstack_top = 0x01C7F800`.
`jellybean37_ram.bin` is 300,544 B (was 376,832 B).

## Why the heap has to be large

`malloc` is backed by `sbrk`, which derives its arena directly from
`HEAP_BEGIN`/`HEAP_END` (`FreeRTOSConfig.h`'s `configTOTAL_HEAP_SIZE` is inert —
the port is on heap_3, so there is no static heap array in `.bss`). So this one
number covers:

| consumer | bytes |
|---|---|
| `#C0gui_task` (depth 4096) | 16,384 |
| `#C0keybed_scan` (depth 1024) | 4,096 |
| Idle (256) + Timer (512) | 3,072 |
| 4 × TCB | 448 |
| Dexed `ProcessorVoice[16]` | 320 |
| Dexed 16 × `Dx7Note` (692 B each) | 11,072 |
| **measured floor** | **35,392** |
| Dexed `EngineMsfa` / `EngineMkI` / `EngineOpl` | *unmeasured* |
| MCUno-60 runtime allocations | *unmeasured* |

Depths are words: `portSTACK_TYPE` is `int` (`FreeRTOS/pi32v2/portmacro.h:83`),
so 4 bytes per unit. `dexed.cpp:201-203` allocates all three engines regardless
of which one is selected.

## What the fix actually was

Two independent problems:

1. **Layout.** `.bss` overran the core stacks, so the core-1 canary
   (`mcu_main.c:227-235`) painted over `g_core_event_ring` and other globals.
   Fixed by moving the stacks to `0x01C7E000`/`0x01C7F800`, reclaiming the
   23,936 B that the old `LENGTH = 480K` discarded.

2. **SRAM starvation.** That alone left a 17,440 B heap against a 35,392 B
   floor. On this target the entire image lives in SRAM, so the only way to get
   RAM is to *delete code* — moving a table to `flash`/`.rodata` does **not**
   help, because a `const` table in `.rodata` occupies exactly as much SRAM as
   the same table in `.bss`. It only inflates the blob.

`config/lv_conf.h` was enabling 12 unused core widgets and 17 unused extra
widgets. All extra widgets **default to `1`** in `lv_conf_internal.h`
(lines 1604-1865), so they compiled in unless named explicitly. Only
`lv_bar_create` and `lv_label_create` are ever called.

| | before | after | delta |
|---|---|---|---|
| `.text` | 334,096 | 257,708 | **−76,388** |
| `.data` | 42,588 | 42,460 | −128 |
| `.bss` | 113,720 | 113,688 | −32 |
| **static** | **490,404** | **413,856** | **−76,548** |
| heap | 17,440 | 93,984 | +76,544 |

That single config change was worth ~74.7 KB — far more than any buffer trim.
`LV_MEM_SIZE` stays at **32 KB**; the draw buffer `s_buf1` (19,200 B) was left
alone too. Both were the wrong lever.

## LVGL pool sizing

`lv_mem.c` tracks its own high-water mark unconditionally, so the pool can be
sized from data instead of guesswork. Sweeping `LV_MEM_SIZE` against the
simulator (same app code, same `lv_conf.h`):

| | |
|---|---|
| 20 / 22 / 24 / 26 KB | **SIGSEGV** in `lv_obj_class_create_obj()` at `ui_sample.c:160` |
| 28 / 32 KB | clean |

Sampling `lv_mem_monitor()` from the main loop reports `max_used = 10,870 B`,
but that is measured **after** `app_init()`. Peak *during* UI construction is
what fails: building an object tree reallocates each parent's
`spec_attr->children[]` array as children are added, so the transient high-water
mark is well above the settled figure. Do not size the pool from steady-state
usage.

## Guards

`ram_app.ld` and `flash_app.ld` `ASSERT` on `_bss_end <= _stack_top`, heap size,
`_sstack_top` ordering, and the stack top against `SRAM_PHYS_END`. These fire at
link time — they did fire on the broken image:

```
ld: RAM layout: .bss overruns the core stacks (_bss_end > _stack_top). ...
ld: RAM layout: heap is inverted or too small (need >= 16 KB ...).
```

`HEAP_MIN` is 48 KB — ~13 KB above the 35,392 B floor, so the guard trips while
there is still somewhere to put newly added static RAM rather than after the heap
has been starved.

Both targets also set `LINK_DEPENDS` on their script (`CMakeLists.txt`); without
it ninja does not relink when only the memory map changes.

## If SRAM gets tight again

In descending order of value per unit of risk:

1. **Re-check for newly enabled LVGL widgets.** The trim above is the biggest win
   available and it was free. Re-run the `grep` documented in `lv_conf.h`.
2. **Shrink `LV_MEM_SIZE`** — 4 KB per step, but the floor is 28 KB and it is
   close to the edge; prefer other options first.
3. **Halve the display draw buffer** — `mcu_display.c:11` uses
   `LCD_WIDTH * 40` px = 19,200 B of RGB565. `LCD_WIDTH * 20` frees 9,600 B at
   the cost of more DMA flushes (LVGL just splits into more bands; the image is
   identical). Note the `40` is duplicated at the array and in the
   `lv_disp_draw_buf_init()` call — keep them in sync.
4. **Drop the unused Dexed engines.** `dexed.cpp:201-203` allocates `EngineMsfa`,
   `EngineMkI` and `EngineOpl` no matter which is selected. Largest single
   remaining win, but `Synth_Dexed/` must stay bit-for-bit identical to upstream
   (`AGENTS.md` §3), so this needs an overlay/forced-include approach rather
   than a source edit.