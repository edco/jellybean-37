/**
 * @file lv_conf.h
 * Configuration for LVGL v8 on JieLi AC791N (SMK-37 Pro - 240x240 ST7789)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE
#endif


/* Display dimensions */
#define MY_DISP_HOR_RES        240
#define MY_DISP_VER_RES        240
#define LV_HOR_RES_MAX         240
#define LV_VER_RES_MAX         240

/* Color settings: RGB565 */
#define LV_COLOR_DEPTH         16
#if defined(TARGET_MCU)
#define LV_COLOR_16_SWAP       1 /* ST7789 over SPI expects big-endian */
#else
#define LV_COLOR_16_SWAP       0 /* Native SDL2 expects native host little-endian */
#endif
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0
#define LV_COLOR_CHROMA_KEY    lv_color_hex(0x00FF00)

/* Memory settings */
#define LV_MEM_CUSTOM          0
/* MEASURED FLOOR -- do not lower this without re-measuring.
 *
 * lv_mem.c tracks its own high-water mark unconditionally (max_used), so the
 * pool can be sized from data instead of guesswork. Sweeping LV_MEM_SIZE and
 * running the native simulator (same app code, same lv_conf.h):
 *
 *     20/22/24/26 KB -> SIGSEGV in lv_obj_class_create_obj() during
 *                       ui_sample_init() (ui_sample.c:160), while growing a
 *                       parent's spec_attr->children[] array
 *     28 KB         -> clean
 *     32 KB         -> clean
 *
 * Note the steady-state figure is misleading: sampling lv_mem_monitor() from
 * the main loop reports max_used = 10,870 bytes, but that is measured AFTER
 * app_init(). Peak *during* UI construction is what actually fails, because
 * building an object tree allocates transiently (children arrays are
 * reallocated as objects are added) before settling.
 *
 * Left at 32 KB. The 28 KB pool works and would free 4 KB, but it sits only
 * 2 KB above a measured segfault, so it is not worth spending without a reason.
 * The RAM this firmware was short of came from somewhere else entirely -- see
 * the widget block below, which is worth ~74.7 KB. See docs/RAM_BUDGET.md. */
#define LV_MEM_SIZE            (32 * 1024U)
#define LV_MEM_ADR             0
#define LV_MEM_BUF_MAX_NUM     16

/* HAL settings */
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

/* Feature configuration */
#define LV_USE_PERF_MONITOR    0
#define LV_USE_MEM_MONITOR     0
#define LV_USE_REFR_DEBUG      0

/* Font settings */
#define LV_FONT_MONTSERRAT_10  0
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_UNSCII_8       1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/* Core Widgets
 *
 * Only LV_USE_BAR and LV_USE_LABEL are enabled because those are the only
 * widgets this firmware constructs. Verified by grepping every lv_* call site:
 *     $ grep -rhoE '\blv_[a-z_0-9]+' src/ --include=*.c --include=*.cpp | sort -u
 * The complete API surface is lv_obj_* (core), lv_bar_*, lv_label_*, plus the
 * lv_disp_drv_* / lv_timer / lv_scr plumbing -- no other widget appears.
 *
 * Every disabled widget here is dead weight in BOTH .text (its class vtable and
 * the default theme's per-widget style setup) and .bss. See docs/RAM_BUDGET.md:
 * on the direct-to-RAM target every byte of image is SRAM, so trimming unused
 * widgets is reclaimable RAM, not just flash.
 *
 * If a widget is ever needed, enable it here AND update the grep above.
 */
#define LV_USE_ARC             0
#define LV_USE_BAR             1
#define LV_USE_BTN             0
#define LV_USE_BTNMATRIX       0
#define LV_USE_CANVAS          0
#define LV_USE_CHECKBOX        0
#define LV_USE_DROPDOWN        0
#define LV_USE_IMG             0
#define LV_USE_LABEL           1
#define LV_USE_LINE            0
#define LV_USE_ROLLER          0
#define LV_USE_SLIDER          0
#define LV_USE_SWITCH          0
#define LV_USE_TEXTAREA        0
#define LV_USE_TABLE           0

/* Extra Widgets -- ALL disabled.
 *
 * Every extra widget defaults to 1 in lv_conf_internal.h (lines 1604-1865), so
 * they are all compiled in unless named here. They also drag in dependencies:
 * disabling LV_USE_IMG breaks lv_animimg, disabling BTNMATRIX/TEXTAREA breaks
 * lv_keyboard/lv_msgbox/lv_spinbox, disabling ARC breaks lv_spinner. Each of
 * those is an #error in the widget header, so they must be turned off here
 * rather than left to default.
 *
 * Only LV_USE_BAR and LV_USE_LABEL are used; see the grep in the Core Widgets
 * block above. */
#define LV_USE_ANIMIMG         0
#define LV_USE_CALENDAR        0
#define LV_USE_CHART           0
#define LV_USE_COLORWHEEL      0
#define LV_USE_IMGBTN          0
#define LV_USE_KEYBOARD        0
#define LV_USE_LED             0
#define LV_USE_LIST            0
#define LV_USE_MENU            0
#define LV_USE_METER           0
#define LV_USE_MSGBOX          0
#define LV_USE_SPAN            0
#define LV_USE_SPINBOX         0
#define LV_USE_SPINNER         0
#define LV_USE_TABVIEW         0
#define LV_USE_TILEVIEW        0
#define LV_USE_WIN             0

/* Themes -- default theme only. BASIC and MONO also default to 1. */
#define LV_USE_THEME_DEFAULT   1
#define LV_THEME_DEFAULT_DARK  1
#define LV_USE_THEME_BASIC     0
#define LV_USE_THEME_MONO      0

#endif /* LV_CONF_H */
