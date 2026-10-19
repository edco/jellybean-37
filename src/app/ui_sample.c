/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file ui_sample.c
 * LVGL v8 Synthesizer Dashboard UI for SMK-37 Pro (240x240)
 */

#include "ui_sample.h"
#include "hal_synth.h"
#include "hal_diag.h"
#include "hal_audio.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *ui_screen = NULL;
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_bank = NULL;
static lv_obj_t *lbl_patch = NULL;
static lv_obj_t *lbl_extra = NULL;
static lv_obj_t *lbl_octave = NULL;
static lv_obj_t *lbl_status = NULL;

static lv_obj_t *bar_vu = NULL;

static lv_obj_t *pad_dots[SMK_NUM_PADS];
static lv_obj_t *key_strip = NULL;

static uint8_t key_state[SMK_NUM_KEYS] = {0};
static int8_t current_octave = 0;

static const char *const BUTTON_NAMES[SMK_BTN_COUNT] = {
    "KnobBank", "FaderBank", "NavLeft", "NavRight",
    "Oct-", "Oct+", "Arp", "Repeat", "Scale", "Chord",
    "Globe", "BT", "Patch", "Para", "FX", "Seq", "Play", "Rec"
};

void ui_sample_init(void)
{
    /* Row 5 cols 0..3 = current scanner ISR stage (4 bits, Green=1/Grey=0).
     * Starts at stage 0x00 (bare 2 kHz SPI2 re-arm). vGuiTask advances the
     * stage after this returns; if the system freezes during init the row-5
     * pattern is all-grey (stage 0). */
    for (int b = 0; b < 4; b++) {
        hal_diag_set_square(5, b, DIAG_COLOR_GREY);
    }

    hal_diag_set_square(4, 0, DIAG_COLOR_RED);       /* Col 0 (Red): ui_sample_init entered */

    ui_screen = lv_scr_act();
    lv_obj_clean(ui_screen);
    lv_obj_set_style_bg_color(ui_screen, lv_color_hex(0x0C0E14), 0);
    lv_obj_clear_flag(ui_screen, LV_OBJ_FLAG_SCROLLABLE);
    hal_diag_set_square(4, 1, DIAG_COLOR_YELLOW);    /* Col 1 (Yellow): Screen configured */

    /* 1. Top Header Bar (Height 26px) */
    lv_obj_t *header = lv_obj_create(ui_screen);
    hal_diag_set_square(4, 2, DIAG_COLOR_GREEN);     /* Col 2 (Green): header created */

    lv_obj_set_size(header, 240, 26);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x161922), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 2, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    hal_diag_set_square(4, 3, DIAG_COLOR_CYAN);      /* Col 3 (Cyan): header configured */

    lbl_title = lv_label_create(header);
    hal_diag_set_square(4, 4, DIAG_COLOR_BLUE);      /* Col 4 (Blue): title label created */

    lv_label_set_text(lbl_title, "SMK-37 Pro");
    hal_diag_set_square(4, 5, DIAG_COLOR_MAGENTA);   /* Col 5 (Magenta): title text set */

    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 4, 0);
    hal_diag_set_square(4, 6, DIAG_COLOR_RED);       /* Col 6 (Red): title aligned */

    lbl_octave = lv_label_create(header);
    hal_diag_set_square(4, 7, DIAG_COLOR_YELLOW);    /* Col 7 (Yellow): octave label created */

    lv_label_set_text(lbl_octave, "OCT 0");
    hal_diag_set_square(4, 8, DIAG_COLOR_GREEN);     /* Col 8 (Green): octave text set */

    lv_obj_set_style_text_color(lbl_octave, lv_color_hex(0xFF4081), 0);
    lv_obj_align(lbl_octave, LV_ALIGN_RIGHT_MID, -4, 0);
    hal_diag_set_square(4, 9, DIAG_COLOR_CYAN);      /* Col 9 (Cyan): octave aligned */

    /* 2. Bank, Patch Info & VU Meter Area */
    lbl_bank = lv_label_create(ui_screen);
    lv_label_set_text(lbl_bank, "[Dexed (FM)] B1: RitCh1.syx");
    lv_obj_set_style_text_color(lbl_bank, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_pos(lbl_bank, 8, 30);

    /* VU Meter Bar */
    bar_vu = lv_bar_create(ui_screen);
    lv_obj_set_size(bar_vu, 224, 6);
    lv_obj_set_pos(bar_vu, 8, 48);
    lv_bar_set_range(bar_vu, 0, 100);
    lv_bar_set_value(bar_vu, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_vu, lv_color_hex(0x1A1E28), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_vu, lv_color_hex(0x00E676), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_vu, 2, 0);
    hal_diag_set_square(4, 10, DIAG_COLOR_CYAN);     /* Col 10 (Cyan): Patch & VU ready */

    lbl_patch = lv_label_create(ui_screen);
    lv_label_set_text(lbl_patch, "P01: FM-Rhodes");
    lv_obj_set_style_text_color(lbl_patch, lv_color_hex(0xFFD600), 0);
    lv_obj_set_pos(lbl_patch, 8, 58);

    lbl_extra = lv_label_create(ui_screen);
    lv_label_set_text(lbl_extra, "");
    lv_obj_set_style_text_color(lbl_extra, lv_color_hex(0x00E676), 0);
    lv_obj_set_pos(lbl_extra, 8, 74);
    hal_diag_set_square(4, 11, DIAG_COLOR_BLUE);     /* Col 11 (Blue): Patch display ready */

    /* 4. 16 Drum Pads Grid (2 rows x 8 columns) */
    lv_obj_t *lbl_pads = lv_label_create(ui_screen);
    lv_label_set_text(lbl_pads, "PADS");
    lv_obj_set_style_text_color(lbl_pads, lv_color_hex(0x8892A0), 0);
    lv_obj_set_pos(lbl_pads, 8, 92);

    for (int p = 0; p < SMK_NUM_PADS; p++) {
        int row = p / 8; // 0: Top row, 1: Bottom row
        int col = p % 8;
        lv_obj_t *pad_box = lv_obj_create(ui_screen);
        lv_obj_set_size(pad_box, 24, 18);
        lv_obj_set_pos(pad_box, 8 + col * 28, 108 + row * 22);
        lv_obj_set_style_bg_color(pad_box, lv_color_hex(0x1F2430), 0);
        lv_obj_set_style_border_color(pad_box, lv_color_hex(0x32394A), 0);
        lv_obj_set_style_border_width(pad_box, 1, 0);
        lv_obj_set_style_radius(pad_box, 3, 0);
        lv_obj_clear_flag(pad_box, LV_OBJ_FLAG_SCROLLABLE);
        pad_dots[p] = pad_box;
    }
    hal_diag_set_square(4, 12, DIAG_COLOR_MAGENTA);  /* Col 12 (Magenta): 16 pads ready */

    /* 5. Status / Event Feedback Label */
    lbl_status = lv_label_create(ui_screen);
    lv_label_set_text(lbl_status, "Ready: 37 Keys Active");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_pos(lbl_status, 8, 158);
    hal_diag_set_square(4, 13, DIAG_COLOR_RED);      /* Col 13 (Red): Status label ready */

    /* 6. 37-Key Keyboard Visual Strip */
    lv_obj_t *lbl_keys = lv_label_create(ui_screen);
    lv_label_set_text(lbl_keys, "KEYBED");
    lv_obj_set_style_text_color(lbl_keys, lv_color_hex(0x8892A0), 0);
    lv_obj_set_pos(lbl_keys, 8, 178);

    key_strip = lv_obj_create(ui_screen);
    lv_obj_set_size(key_strip, 226, 44);
    lv_obj_set_pos(key_strip, 7, 192);
    lv_obj_set_style_bg_color(key_strip, lv_color_hex(0x12151E), 0);
    lv_obj_set_style_border_color(key_strip, lv_color_hex(0x282F40), 0);
    lv_obj_set_style_border_width(key_strip, 1, 0);
    lv_obj_set_style_radius(key_strip, 2, 0);
    lv_obj_set_style_pad_all(key_strip, 1, 0);
    lv_obj_clear_flag(key_strip, LV_OBJ_FLAG_SCROLLABLE);

    /* Create 37 key segments */
    for (int k = 0; k < SMK_NUM_KEYS; k++) {
        lv_obj_t *key_seg = lv_obj_create(key_strip);
        lv_obj_set_size(key_seg, 5, 38);
        lv_obj_set_pos(key_seg, k * 6, 1);
        
        /* White or black key coloring */
        int semitone = k % 12;
        bool is_black = (semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10);
        if (is_black) {
            lv_obj_set_size(key_seg, 4, 24);
            lv_obj_set_style_bg_color(key_seg, lv_color_hex(0x353C4E), 0);
        } else {
            lv_obj_set_style_bg_color(key_seg, lv_color_hex(0xDDE3EE), 0);
        }
        lv_obj_set_style_border_width(key_seg, 0, 0);
        lv_obj_set_style_radius(key_seg, 1, 0);
        lv_obj_clear_flag(key_seg, LV_OBJ_FLAG_SCROLLABLE);
    }
    hal_diag_set_square(4, 14, DIAG_COLOR_GREEN);    /* Col 14 (Green): 37 keys ready */
    hal_diag_set_square(4, 15, DIAG_COLOR_WHITE);    /* Col 15 (White): ui_sample_init complete */
}

void ui_sample_show_screen(void)
{
    if (ui_screen) {
        lv_scr_load(ui_screen);
    }
}

void ui_sample_update(void)
{
    if (!bar_vu) return;

    uint8_t vu = hal_synth_get_vu_level();
    lv_bar_set_value(bar_vu, vu, LV_ANIM_OFF);
    
    if (vu > 85) {
        lv_obj_set_style_bg_color(bar_vu, lv_color_hex(0xFF1744), LV_PART_INDICATOR);
    } else if (vu > 60) {
        lv_obj_set_style_bg_color(bar_vu, lv_color_hex(0xFFD600), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(bar_vu, lv_color_hex(0x00E676), LV_PART_INDICATOR);
    }
}

void ui_sample_set_key(uint8_t key_index, bool pressed)
{
    if (key_index >= SMK_NUM_KEYS || !key_strip) return;
    key_state[key_index] = pressed ? 1 : 0;

    lv_obj_t *key_seg = lv_obj_get_child(key_strip, key_index);
    if (!key_seg) return;

    if (pressed) {
        lv_obj_set_style_bg_color(key_seg, lv_color_hex(0x00E5FF), 0);
        char buf[32];
        snprintf(buf, sizeof(buf), "Key %u ON (MIDI %u)", key_index + 1, 48 + current_octave * 12 + key_index);
        if (lbl_status) lv_label_set_text(lbl_status, buf);
    } else {
        int semitone = key_index % 12;
        bool is_black = (semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10);
        lv_obj_set_style_bg_color(key_seg, is_black ? lv_color_hex(0x353C4E) : lv_color_hex(0xDDE3EE), 0);
    }
}

void ui_sample_set_pad(uint8_t pad_index, bool pressed, uint8_t velocity)
{
    if (pad_index >= SMK_NUM_PADS || !pad_dots[pad_index]) return;

    if (pressed) {
        lv_obj_set_style_bg_color(pad_dots[pad_index], lv_color_hex(0xFF4081), 0);
        char buf[32];
        snprintf(buf, sizeof(buf), "PAD %u Hit (vel %u)", pad_index + 1, velocity);
        if (lbl_status) lv_label_set_text(lbl_status, buf);
    } else {
        lv_obj_set_style_bg_color(pad_dots[pad_index], lv_color_hex(0x1F2430), 0);
    }
}

void ui_sample_set_pad_pressure(uint8_t pad_index, uint8_t pressure)
{
    if (lbl_status && pressure > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "PAD %u Press: %u", pad_index + 1, pressure);
        lv_label_set_text(lbl_status, buf);
    }
}

void ui_sample_set_encoder(uint8_t enc_index, int8_t delta)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "Enc %u: %s", enc_index + 1, (delta > 0) ? "UP (+1)" : "DOWN (-1)");
    if (lbl_status) lv_label_set_text(lbl_status, buf);
}

void ui_sample_set_fader(uint8_t fader_index, uint8_t value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "Fader %u: %u/127", fader_index + 1, value);
    if (lbl_status) lv_label_set_text(lbl_status, buf);
}

void ui_sample_set_wheel(SmkWheel wheel, int16_t value)
{
    char buf[32];
    if (wheel == SMK_WHEEL_PITCH) {
        snprintf(buf, sizeof(buf), "PitchBend: %d", value);
    } else {
        snprintf(buf, sizeof(buf), "ModWheel: %d", value);
    }
    if (lbl_status) lv_label_set_text(lbl_status, buf);
}

void ui_sample_set_button(SmkButton button, bool pressed)
{
    if (button >= SMK_BTN_COUNT) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "Btn %s: %s", BUTTON_NAMES[button], pressed ? "DOWN" : "UP");
    if (lbl_status) lv_label_set_text(lbl_status, buf);
}

void ui_sample_set_octave(int8_t octave)
{
    current_octave = octave;
    if (lbl_octave) {
        char buf[16];
        snprintf(buf, sizeof(buf), "OCT %s%d", octave < 0 ? "-" : "+", octave < 0 ? -(int)octave : (int)octave);
        lv_label_set_text(lbl_octave, buf);
    }
}

void ui_sample_set_engine_bank_patch(uint8_t engine, uint8_t bank, uint8_t patch, const char *engine_name, const char *bank_name, const char *patch_name, bool pending)
{
    (void)engine;
    if (lbl_bank) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[%s] B%u: %s", engine_name ? engine_name : "", bank + 1, bank_name ? bank_name : "");
        lv_label_set_text(lbl_bank, buf);
    }
    if (lbl_patch) {
        char buf[64];
        snprintf(buf, sizeof(buf), "P%02u: %s", patch + 1, patch_name ? patch_name : "");
        lv_label_set_text(lbl_patch, buf);
    }
    if (lbl_extra) {
        if (pending) {
            lv_label_set_text(lbl_extra, "[ Loading... ]");
            lv_obj_set_style_text_color(lbl_extra, lv_color_hex(0xFF9100), 0);
        } else {
            lv_label_set_text(lbl_extra, "");
        }
    }
}

void ui_sample_set_bank_patch(uint8_t bank, uint8_t patch, const char *bank_name, const char *patch_name, bool pending)
{
    ui_sample_set_engine_bank_patch(0, bank, patch, "Dexed (FM)", bank_name, patch_name, pending);
}

void ui_sample_show_volume(uint8_t volume)
{
    if (lbl_extra) {
        char buf[32];
        uint8_t pct = (uint8_t)(((uint32_t)volume * 100) / 127);
        snprintf(buf, sizeof(buf), "VOL: %u/127 (%u%%)", volume, pct);
        lv_label_set_text(lbl_extra, buf);
        lv_obj_set_style_text_color(lbl_extra, lv_color_hex(0x00E676), 0);
    }
}

void ui_sample_show_limiter(float t, float m, bool is_thresh)
{
    if (lbl_extra) {
        char buf[36];
        if (is_thresh) {
            if (t >= 1.00f) {
                snprintf(buf, sizeof(buf), "Limiter T: 1.00 (Off)");
            } else {
                snprintf(buf, sizeof(buf), "Limiter T: %.2f", t);
            }
        } else {
            if (m <= 1.0f) {
                snprintf(buf, sizeof(buf), "Limiter M: 1.0 (Off)");
            } else {
                snprintf(buf, sizeof(buf), "Limiter M: %.1f", m);
            }
        }
        lv_label_set_text(lbl_extra, buf);
        lv_obj_set_style_text_color(lbl_extra, lv_color_hex(0x00E676), 0);
    }
    if (lbl_status) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Limiter: T=%.2f M=%.1f", t, m);
        lv_label_set_text(lbl_status, buf);
    }
}

void ui_sample_clear_volume_overlay(void)
{
    if (lbl_extra) {
        lv_label_set_text(lbl_extra, "");
    }
}
