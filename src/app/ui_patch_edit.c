/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file ui_patch_edit.c
 * LVGL v8 Patch Editing Screen for MCUno-32 / MCUno-60 (240x240)
 * Memory-optimized using direct LVGL custom draw to fit within tight 32KB RAM pool.
 */

#include "ui_patch_edit.h"
#include "ui_sample.h"
#include "mcuno60_patches.h"
#include "patch_banks.h"
#include "hal_synth.h"
#include "hal_led.h"
#include "hal_time.h"
#include "juno_engine.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define NUM_COLS 8

typedef enum {
    CTRL_TYPE_NONE = 0,
    CTRL_TYPE_BUTTON,
    CTRL_TYPE_SWITCH,
    CTRL_TYPE_FADER
} UiCtrlType;

typedef struct {
    UiCtrlType type;
    const char *heading;
    const char *label;
    uint8_t     param_id;
    uint8_t     num_options; /* For switches */
    const char *const *option_names;
} UiControlDef;

/* Option string arrays for switches */
static const char *const POLY_MODE_OPTIONS[] = {"Poly 1", "Poly 2", "Unison"};
static const char *const ARP_DIR_OPTIONS[] = {"Up", "Up/Down", "Down"};
static const char *const ARP_RNG_OPTIONS[] = {"1 Oct", "2 Oct", "3 Oct"};
static const char *const LFO_MODE_OPTIONS[] = {"Triangle", "Square"};
static const char *const PWM_MODE_OPTIONS[] = {"LFO", "Manual", "ENV"};
static const char *const HPF_OPTIONS[] = {"0", "1", "2", "3"};
static const char *const POLARITY_OPTIONS[] = {"Normal", "Inverted"};
static const char *const VCA_MODE_OPTIONS[] = {"Envelope", "Gate"};

/* Definitions of the 4 pages */
static const UiControlDef PAGE_CONTROLS[UI_PATCH_EDIT_NUM_PAGES][NUM_COLS] = {
    /* Page 1: Voice (K1), Arpeggio (K2..K5) & LFO (K6..K8) */
    {
        {CTRL_TYPE_SWITCH, "Voice",    "mod", JUNO_PARAM_POLY_MODE, 3, POLY_MODE_OPTIONS},
        {CTRL_TYPE_BUTTON, "Arpeggio", "en",  JUNO_PARAM_ARP_ON, 0, NULL},
        {CTRL_TYPE_SWITCH, "Arpeggio", "dir", JUNO_PARAM_ARP_MODE, 3, ARP_DIR_OPTIONS},
        {CTRL_TYPE_SWITCH, "Arpeggio", "rng", JUNO_PARAM_ARP_RANGE, 3, ARP_RNG_OPTIONS},
        {CTRL_TYPE_FADER,  "Arpeggio", "rat", JUNO_PARAM_ARP_RATE, 0, NULL},
        {CTRL_TYPE_FADER,  "LFO",      "rat", JUNO_PARAM_LFO_RATE, 0, NULL},
        {CTRL_TYPE_FADER,  "LFO",      "dly", JUNO_PARAM_LFO_DELAY, 0, NULL},
        {CTRL_TYPE_SWITCH, "LFO",      "mod", 0xFF, 2, LFO_MODE_OPTIONS}
    },
    /* Page 2: DCO (K1..K3) & Waves (K4..K8) */
    {
        {CTRL_TYPE_FADER,  "DCO",      "lfo", JUNO_PARAM_DCO_LFO, 0, NULL},
        {CTRL_TYPE_FADER,  "DCO",      "pwm", JUNO_PARAM_DCO_PWM, 0, NULL},
        {CTRL_TYPE_SWITCH, "DCO",      "mod", JUNO_PARAM_DCO_PWM_MODE, 3, PWM_MODE_OPTIONS},
        {CTRL_TYPE_BUTTON, "Wave/Mix", "pls", JUNO_PARAM_DCO_PULSE, 0, NULL},
        {CTRL_TYPE_BUTTON, "Wave/Mix", "saw", JUNO_PARAM_DCO_SAW, 0, NULL},
        {CTRL_TYPE_BUTTON, "Wave/Mix", "sub", JUNO_PARAM_DCO_SUB, 0, NULL},
        {CTRL_TYPE_FADER,  "Wave/Mix", "sub", JUNO_PARAM_DCO_SUB_LEVEL, 0, NULL},
        {CTRL_TYPE_FADER,  "Wave/Mix", "noi", JUNO_PARAM_DCO_NOISE, 0, NULL}
    },
    /* Page 3: HPF (K1) & VCF Filter (K2..K7) */
    {
        {CTRL_TYPE_SWITCH, "HPF",        "hpf", JUNO_PARAM_HPF_POS, 4, HPF_OPTIONS},
        {CTRL_TYPE_FADER,  "VCF Filter", "frq", JUNO_PARAM_VCF_CUTOFF, 0, NULL},
        {CTRL_TYPE_FADER,  "VCF Filter", "res", JUNO_PARAM_VCF_RES, 0, NULL},
        {CTRL_TYPE_SWITCH, "VCF Filter", "pol", JUNO_PARAM_VCF_ENV_POL, 2, POLARITY_OPTIONS},
        {CTRL_TYPE_FADER,  "VCF Filter", "env", JUNO_PARAM_VCF_ENV_MOD, 0, NULL},
        {CTRL_TYPE_FADER,  "VCF Filter", "lfo", JUNO_PARAM_VCF_LFO_MOD, 0, NULL},
        {CTRL_TYPE_FADER,  "VCF Filter", "kbd", JUNO_PARAM_VCF_KBD, 0, NULL},
        {CTRL_TYPE_NONE,   NULL,         "",    0, 0, NULL}
    },
    /* Page 4: VCA (K1..K2), Envelope (K3..K6), Chorus (K7..K8) */
    {
        {CTRL_TYPE_SWITCH, "VCA",      "mod", JUNO_PARAM_VCA_MODE, 2, VCA_MODE_OPTIONS},
        {CTRL_TYPE_FADER,  "VCA",      "lvl", JUNO_PARAM_VCA_LEVEL, 0, NULL},
        {CTRL_TYPE_FADER,  "Envelope", "atk", JUNO_PARAM_ENV_ATTACK, 0, NULL},
        {CTRL_TYPE_FADER,  "Envelope", "dcy", JUNO_PARAM_ENV_DECAY, 0, NULL},
        {CTRL_TYPE_FADER,  "Envelope", "sus", JUNO_PARAM_ENV_SUSTAIN, 0, NULL},
        {CTRL_TYPE_FADER,  "Envelope", "rel", JUNO_PARAM_ENV_RELEASE, 0, NULL},
        {CTRL_TYPE_BUTTON, "Chorus",   "ch1", JUNO_PARAM_CHORUS_MODE, 0, NULL},
        {CTRL_TYPE_BUTTON, "Chorus",   "ch2", JUNO_PARAM_CHORUS_MODE, 0, NULL}
    }
};

/* State */
static lv_obj_t *edit_screen = NULL;
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_page = NULL;
static lv_obj_t *lbl_octave = NULL;
static lv_obj_t *lbl_banner = NULL;
static lv_obj_t *panel_controls = NULL;
static lv_obj_t *lbl_sub[NUM_COLS];
static lv_obj_t *lbl_knob[NUM_COLS];

static uint8_t  current_page = 0; /* Remembered across screen transitions */
static bool     is_screen_active = false;
static uint32_t banner_time = 0;
static bool     banner_active = false;

/* Working copy of parameters */
static JunoEngineParams edit_params;
static uint8_t lfo_mode_custom = 0; /* 0: Triangle, 1: Square */
static char loaded_patch_name[32] = "Juno Default";

static void update_page_view(void);
static float get_param_value(uint8_t page, uint8_t col);

static void draw_rect_helper(lv_draw_ctx_t *draw_ctx, int x, int y, int w, int h, uint32_t bg_hex, uint32_t border_hex, int border_w, int radius)
{
    lv_area_t a;
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + h - 1;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(bg_hex);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = radius;
    dsc.border_width = border_w;
    dsc.border_color = lv_color_hex(border_hex);
    dsc.border_opa = border_w > 0 ? LV_OPA_COVER : LV_OPA_TRANSP;

    lv_draw_rect(draw_ctx, &dsc, &a);
}

static void panel_draw_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_DRAW_MAIN) return;

    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    /* 1. Group Headers Banner Bar */
    const char *last_heading = NULL;
    int group_start_col = 0;

    for (int col = 0; col <= NUM_COLS; col++) {
        const char *heading = (col < NUM_COLS) ? PAGE_CONTROLS[current_page][col].heading : NULL;
        if (col == NUM_COLS || (heading != last_heading && col > group_start_col)) {
            if (last_heading != NULL) {
                int x1 = 2 + group_start_col * 30;
                int x2 = 2 + (col - 1) * 30 + 26;
                int w = x2 - x1;

                draw_rect_helper(draw_ctx, x1, 40, w, 14, 0x181F2C, 0x2A364C, 1, 2);

                /* Text label for group */
                lv_draw_label_dsc_t label_dsc;
                lv_draw_label_dsc_init(&label_dsc);
                label_dsc.color = lv_color_hex(0x00E5FF);
                label_dsc.font = LV_FONT_DEFAULT;

                lv_area_t txt_area;
                txt_area.x1 = x1 + 2;
                txt_area.y1 = 41;
                txt_area.x2 = x2 - 2;
                txt_area.y2 = 53;

                lv_draw_label(draw_ctx, &label_dsc, &txt_area, last_heading, NULL);
            }
            last_heading = heading;
            group_start_col = col;
        }
    }

    /* 2. Control Tracks, Ticks, Caps and Buttons for each column */
    for (int col = 0; col < NUM_COLS; col++) {
        const UiControlDef *def = &PAGE_CONTROLS[current_page][col];
        if (def->type == CTRL_TYPE_NONE) continue;

        int x = 2 + col * 30;
        int center_x = x + 13;

        if (def->type == CTRL_TYPE_FADER) {
            /* Vertical track slot (Y: 66 to 176, height 110px) */
            draw_rect_helper(draw_ctx, center_x - 1, 66, 2, 110, 0x242C3C, 0, 0, 1);

            /* 3 Major ticks (top, middle, bottom: 8px wide) */
            draw_rect_helper(draw_ctx, center_x - 4, 66,  8, 1, 0x8892A0, 0, 0, 0);
            draw_rect_helper(draw_ctx, center_x - 4, 121, 8, 1, 0x8892A0, 0, 0, 0);
            draw_rect_helper(draw_ctx, center_x - 4, 176, 8, 1, 0x8892A0, 0, 0, 0);

            /* 8 Minor ticks (4px wide): 4 bottom-middle, 4 middle-top */
            int minor_y[8] = {165, 154, 143, 132, 110, 99, 88, 77};
            for (int m = 0; m < 8; m++) {
                draw_rect_helper(draw_ctx, center_x - 2, minor_y[m], 4, 1, 0x444E64, 0, 0, 0);
            }

            /* Fader Handle Cap (16x8 px) */
            float val = get_param_value(current_page, col);
            if (val < 0.0f) val = 0.0f;
            if (val > 1.0f) val = 1.0f;
            int cap_y = 176 - (int)(val * 110.0f) - 4;

            draw_rect_helper(draw_ctx, center_x - 8, cap_y, 16, 8, 0x323B4E, 0x5C6984, 1, 2);
            /* Center contrast line on cap */
            draw_rect_helper(draw_ctx, center_x - 5, cap_y + 3, 10, 2, 0x00E5FF, 0, 0, 0);

        } else if (def->type == CTRL_TYPE_SWITCH) {
            /* Vertical track slot */
            draw_rect_helper(draw_ctx, center_x - 1, 66, 2, 110, 0x242C3C, 0, 0, 1);

            /* Major ticks for options */
            uint8_t opts = def->num_options ? def->num_options : 2;
            for (int m = 0; m < opts; m++) {
                int y = 176 - (int)((float)m * (110.0f / (float)(opts - 1)));
                draw_rect_helper(draw_ctx, center_x - 4, y, 8, 1, 0x8892A0, 0, 0, 0);
            }

            /* Switch Handle Cap snapped to selected option */
            float val = get_param_value(current_page, col);
            int opt_idx = (int)val;
            if (opt_idx < 0) opt_idx = 0;
            if (opt_idx >= opts) opt_idx = opts - 1;
            int cap_y = 176 - (int)((float)opt_idx * (110.0f / (float)(opts - 1))) - 4;

            draw_rect_helper(draw_ctx, center_x - 8, cap_y, 16, 8, 0x3A445A, 0xFFD600, 1, 2);
            draw_rect_helper(draw_ctx, center_x - 4, cap_y + 3, 8, 2, 0xFFD600, 0, 0, 0);

        } else if (def->type == CTRL_TYPE_BUTTON) {
            float val = get_param_value(current_page, col);
            bool is_on = (val >= 0.5f);

            /* Red indicator rectangle centered above button (10x4 px) */
            uint32_t led_color = is_on ? 0xFF1744 : 0x38050E;
            draw_rect_helper(draw_ctx, center_x - 5, 126, 10, 4, led_color, is_on ? 0xFF5252 : 0, is_on ? 1 : 0, 1);

            /* Coloured Square (18x18 px) */
            uint32_t btn_bg = is_on ? 0x2B3648 : 0x1C222E;
            uint32_t btn_border = is_on ? 0x00E5FF : 0x404B62;
            draw_rect_helper(draw_ctx, center_x - 9, 136, 18, 18, btn_bg, btn_border, 1, 2);
        }
    }
}

void ui_patch_edit_init(void)
{
    edit_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(edit_screen, lv_color_hex(0x0C0E14), 0);
    lv_obj_clear_flag(edit_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Header Bar (Height 22px) */
    lv_obj_t *header = lv_obj_create(edit_screen);
    lv_obj_set_size(header, 240, 22);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x161922), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 2, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "MCUno-32 EDIT");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 4, 0);

    lbl_octave = lv_label_create(header);
    lv_label_set_text(lbl_octave, "OCT 0");
    lv_obj_set_style_text_color(lbl_octave, lv_color_hex(0xFF4081), 0);
    lv_obj_align(lbl_octave, LV_ALIGN_RIGHT_MID, -4, 0);

    /* 2. Page Number & Value Banner (Y: 23, Height: 16px) */
    lbl_page = lv_label_create(edit_screen);
    lv_obj_set_pos(lbl_page, 6, 23);
    lv_label_set_text(lbl_page, "PAGE 1/4");
    lv_obj_set_style_text_color(lbl_page, lv_color_hex(0xFFD600), 0);

    lbl_banner = lv_label_create(edit_screen);
    lv_obj_set_pos(lbl_banner, 76, 24);
    lv_label_set_text(lbl_banner, "");
    lv_obj_set_style_text_color(lbl_banner, lv_color_hex(0x00E676), 0);
    lv_obj_set_style_text_font(lbl_banner, &lv_font_montserrat_12, 0);

    /* 3. Panel Controls (Y: 40 to 196) with custom draw callback */
    panel_controls = lv_obj_create(edit_screen);
    lv_obj_set_size(panel_controls, 240, 156);
    lv_obj_set_pos(panel_controls, 0, 40);
    lv_obj_set_style_bg_opa(panel_controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_controls, 0, 0);
    lv_obj_set_style_pad_all(panel_controls, 0, 0);
    lv_obj_clear_flag(panel_controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel_controls, panel_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* 4. Column Labels (Sub-labels and Knob names) */
    for (int col = 0; col < NUM_COLS; col++) {
        int x = 2 + col * 30;

        lbl_sub[col] = lv_label_create(edit_screen);
        lv_obj_set_pos(lbl_sub[col], x + 2, 198);
        lv_label_set_text(lbl_sub[col], "");
        lv_obj_set_style_text_color(lbl_sub[col], lv_color_hex(0xDDE3EE), 0);

        lbl_knob[col] = lv_label_create(edit_screen);
        lv_obj_set_pos(lbl_knob[col], x + 4, 216);
        char kbuf[8];
        snprintf(kbuf, sizeof(kbuf), "K%d", col + 1);
        lv_label_set_text(lbl_knob[col], kbuf);
        lv_obj_set_style_text_color(lbl_knob[col], lv_color_hex(0x606B80), 0);
    }

    /* Initialize default parameters from factory bank 0 patch 0 */
    const JunoEngineParams *def_p = mcuno60_get_patch_params(0, 0);
    if (def_p) {
        edit_params = *def_p;
    }

    update_page_view();
}

void ui_patch_edit_show(void)
{
    if (edit_screen) {
        is_screen_active = true;
        hal_led_set_button(SMK_LED_BTN_PARA, true);
        update_page_view();
        lv_scr_load(edit_screen);
    }
}

void ui_patch_edit_hide(void)
{
    is_screen_active = false;
    hal_led_set_button(SMK_LED_BTN_PARA, false);
    ui_sample_show_screen();
}

void ui_patch_edit_toggle(void)
{
    if (is_screen_active) {
        ui_patch_edit_hide();
    } else {
        ui_patch_edit_show();
    }
}

bool ui_patch_edit_is_active(void)
{
    return is_screen_active;
}

void ui_patch_edit_nav_page(int8_t dir)
{
    int new_page = (int)current_page + dir;
    if (new_page < 0) new_page = UI_PATCH_EDIT_NUM_PAGES - 1;
    if (new_page >= UI_PATCH_EDIT_NUM_PAGES) new_page = 0;

    current_page = (uint8_t)new_page;
    update_page_view();
}

void ui_patch_edit_set_octave(int8_t octave)
{
    if (lbl_octave) {
        char buf[16];
        snprintf(buf, sizeof(buf), "OCT %s%d", octave < 0 ? "-" : "+", octave < 0 ? -(int)octave : (int)octave);
        lv_label_set_text(lbl_octave, buf);
    }
}

void ui_patch_edit_load_patch(uint8_t bank_idx, uint8_t patch_idx)
{
    const JunoEngineParams *p = mcuno60_get_patch_params(bank_idx, patch_idx);
    if (p) {
        edit_params = *p;
        const char *name = mcuno60_get_patch_name(bank_idx, patch_idx);
        if (name) {
            snprintf(loaded_patch_name, sizeof(loaded_patch_name), "%s", name);
        }
        if (is_screen_active) {
            update_page_view();
        }
    }
}

void ui_patch_edit_update(void)
{
    if (!is_screen_active) return;

    if (banner_active && (hal_get_ms() - banner_time >= 1500)) {
        banner_active = false;
        if (lbl_banner) {
            lv_label_set_text(lbl_banner, "");
        }
    }
}

static void show_banner(const char *text)
{
    if (lbl_banner && text) {
        lv_label_set_text(lbl_banner, text);
        lv_obj_set_style_text_color(lbl_banner, lv_color_hex(0x00E676), 0);
        banner_time = hal_get_ms();
        banner_active = true;
    }
}

static float get_param_value(uint8_t page, uint8_t col)
{
    switch (page) {
    case 0:
        switch (col) {
        case 0: return (float)edit_params.poly_mode;
        case 1: return edit_params.arp_on ? 1.0f : 0.0f;
        case 2:
            if (edit_params.arp_mode == JUNO_ARP_UP) return 0.0f;
            if (edit_params.arp_mode == JUNO_ARP_UP_DOWN) return 1.0f;
            return 2.0f;
        case 3: return (float)(edit_params.arp_range - 1);
        case 4:
            return (edit_params.arp_rate - 1.55f) / (48.6f - 1.55f);
        case 5:
            return (edit_params.lfo_rate - 0.1f) / 21.0f;
        case 6:
            return edit_params.lfo_delay / 1.5f;
        case 7: return (float)lfo_mode_custom;
        default: break;
        }
        break;
    case 1:
        switch (col) {
        case 0: return edit_params.dco_lfo_mod;
        case 1: return edit_params.dco_pwm;
        case 2:
            if (edit_params.dco_pwm_mode == JUNO_PWM_LFO) return 0.0f;
            if (edit_params.dco_pwm_mode == JUNO_PWM_MAN) return 1.0f;
            return 2.0f;
        case 3: return edit_params.dco_pulse ? 1.0f : 0.0f;
        case 4: return edit_params.dco_saw ? 1.0f : 0.0f;
        case 5: return edit_params.dco_sub ? 1.0f : 0.0f;
        case 6: return edit_params.dco_sub_level;
        case 7: return edit_params.dco_noise;
        default: break;
        }
        break;
    case 2:
        switch (col) {
        case 0: return (float)edit_params.hpf_position;
        case 1: return edit_params.vcf_cutoff;
        case 2: return edit_params.vcf_res;
        case 3: return (edit_params.vcf_env_polarity < 0) ? 1.0f : 0.0f;
        case 4: return edit_params.vcf_env_mod;
        case 5: return edit_params.vcf_lfo_mod;
        case 6: return edit_params.vcf_kbd_track;
        default: break;
        }
        break;
    case 3:
        switch (col) {
        case 0: return (float)edit_params.vca_mode;
        case 1: return edit_params.vca_level;
        case 2: return edit_params.env_attack;
        case 3: return edit_params.env_decay;
        case 4: return edit_params.env_sustain;
        case 5: return edit_params.env_release;
        case 6: return (edit_params.chorus_mode & 1) ? 1.0f : 0.0f;
        case 7: return (edit_params.chorus_mode & 2) ? 1.0f : 0.0f;
        default: break;
        }
        break;
    }
    return 0.0f;
}

static void apply_param_delta(uint8_t page, uint8_t col, int8_t delta)
{
    const UiControlDef *def = &PAGE_CONTROLS[page][col];
    if (def->type == CTRL_TYPE_NONE) return;

    char msg[64] = {0};

    switch (page) {
    case 0:
        switch (col) {
        case 0: {
            int mode = (int)edit_params.poly_mode + delta;
            if (mode < 0) mode = 0;
            if (mode > 2) mode = 2;
            edit_params.poly_mode = mode;
            hal_synth_set_param(JUNO_PARAM_POLY_MODE, (float)edit_params.poly_mode);
            snprintf(msg, sizeof(msg), "Voice Mode: %s", POLY_MODE_OPTIONS[mode]);
            break;
        }
        case 1:
            edit_params.arp_on = !edit_params.arp_on;
            hal_synth_set_param(JUNO_PARAM_ARP_ON, edit_params.arp_on ? 1.0f : 0.0f);
            snprintf(msg, sizeof(msg), "Arpeggio: %s", edit_params.arp_on ? "ON" : "OFF");
            break;
        case 2: {
            int mode = (edit_params.arp_mode == JUNO_ARP_UP) ? 0 : ((edit_params.arp_mode == JUNO_ARP_UP_DOWN) ? 1 : 2);
            mode = (mode + delta) % 3;
            if (mode < 0) mode += 3;
            if (mode == 0) edit_params.arp_mode = JUNO_ARP_UP;
            else if (mode == 1) edit_params.arp_mode = JUNO_ARP_UP_DOWN;
            else edit_params.arp_mode = JUNO_ARP_DOWN;
            hal_synth_set_param(JUNO_PARAM_ARP_MODE, (float)edit_params.arp_mode);
            snprintf(msg, sizeof(msg), "Arp Dir: %s", ARP_DIR_OPTIONS[mode]);
            break;
        }
        case 3: {
            int rng = edit_params.arp_range + delta;
            if (rng < 1) rng = 1;
            if (rng > 3) rng = 3;
            edit_params.arp_range = rng;
            hal_synth_set_param(JUNO_PARAM_ARP_RANGE, (float)edit_params.arp_range);
            snprintf(msg, sizeof(msg), "Arp Range: %d Oct", edit_params.arp_range);
            break;
        }
        case 4: {
            float norm = (edit_params.arp_rate - 1.55f) / (48.6f - 1.55f);
            norm += (float)delta * 0.02f;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            uint32_t bpm = 93 + (uint32_t)(norm * (2916 - 93));
            edit_params.arp_rate = (float)bpm / 60.0f;
            hal_synth_set_param(JUNO_PARAM_ARP_RATE, edit_params.arp_rate);
            snprintf(msg, sizeof(msg), "Arp Rate: %u bpm", bpm);
            break;
        }
        case 5: {
            float norm = (edit_params.lfo_rate - 0.1f) / 21.0f;
            norm += (float)delta * 0.02f;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            edit_params.lfo_rate = 0.1f + norm * 21.0f;
            hal_synth_set_param(JUNO_PARAM_LFO_RATE, edit_params.lfo_rate);
            snprintf(msg, sizeof(msg), "LFO Rate: %.1f Hz", edit_params.lfo_rate);
            break;
        }
        case 6: {
            float norm = edit_params.lfo_delay / 1.5f;
            norm += (float)delta * 0.02f;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            edit_params.lfo_delay = norm * 1.5f;
            hal_synth_set_param(JUNO_PARAM_LFO_DELAY, edit_params.lfo_delay);
            snprintf(msg, sizeof(msg), "LFO Delay: %u ms", (uint32_t)(norm * 1500.0f));
            break;
        }
        case 7: {
            lfo_mode_custom = (lfo_mode_custom + (delta > 0 ? 1 : 1)) % 2;
            snprintf(msg, sizeof(msg), "LFO Mode: %s", LFO_MODE_OPTIONS[lfo_mode_custom]);
            break;
        }
        default: break;
        }
        break;

    case 1:
        switch (col) {
        case 0: {
            edit_params.dco_lfo_mod += (float)delta * 0.02f;
            if (edit_params.dco_lfo_mod < 0.0f) edit_params.dco_lfo_mod = 0.0f;
            if (edit_params.dco_lfo_mod > 1.0f) edit_params.dco_lfo_mod = 1.0f;
            hal_synth_set_param(JUNO_PARAM_DCO_LFO, edit_params.dco_lfo_mod);
            snprintf(msg, sizeof(msg), "DCO LFO: %.2f", edit_params.dco_lfo_mod * 2.5f);
            break;
        }
        case 1: {
            edit_params.dco_pwm += (float)delta * 0.02f;
            if (edit_params.dco_pwm < 0.0f) edit_params.dco_pwm = 0.0f;
            if (edit_params.dco_pwm > 1.0f) edit_params.dco_pwm = 1.0f;
            hal_synth_set_param(JUNO_PARAM_DCO_PWM, edit_params.dco_pwm);
            snprintf(msg, sizeof(msg), "DCO PWM: %u%%", (uint32_t)(edit_params.dco_pwm * 100.0f));
            break;
        }
        case 2: {
            int mode = (edit_params.dco_pwm_mode == JUNO_PWM_LFO) ? 0 : ((edit_params.dco_pwm_mode == JUNO_PWM_MAN) ? 1 : 2);
            mode = (mode + delta) % 3;
            if (mode < 0) mode += 3;
            edit_params.dco_pwm_mode = (mode == 0) ? JUNO_PWM_LFO : ((mode == 1) ? JUNO_PWM_MAN : 2);
            hal_synth_set_param(JUNO_PARAM_DCO_PWM_MODE, (float)edit_params.dco_pwm_mode);
            snprintf(msg, sizeof(msg), "PWM Mode: %s", PWM_MODE_OPTIONS[mode]);
            break;
        }
        case 3:
            edit_params.dco_pulse = !edit_params.dco_pulse;
            hal_synth_set_param(JUNO_PARAM_DCO_PULSE, edit_params.dco_pulse ? 1.0f : 0.0f);
            snprintf(msg, sizeof(msg), "Pulse Wave: %s", edit_params.dco_pulse ? "ON" : "OFF");
            break;
        case 4:
            edit_params.dco_saw = !edit_params.dco_saw;
            hal_synth_set_param(JUNO_PARAM_DCO_SAW, edit_params.dco_saw ? 1.0f : 0.0f);
            snprintf(msg, sizeof(msg), "Saw Wave: %s", edit_params.dco_saw ? "ON" : "OFF");
            break;
        case 5:
            edit_params.dco_sub = !edit_params.dco_sub;
            hal_synth_set_param(JUNO_PARAM_DCO_SUB, edit_params.dco_sub ? 1.0f : 0.0f);
            snprintf(msg, sizeof(msg), "Sub Wave: %s", edit_params.dco_sub ? "ON" : "OFF");
            break;
        case 6: {
            edit_params.dco_sub_level += (float)delta * 0.02f;
            if (edit_params.dco_sub_level < 0.0f) edit_params.dco_sub_level = 0.0f;
            if (edit_params.dco_sub_level > 1.0f) edit_params.dco_sub_level = 1.0f;
            hal_synth_set_param(JUNO_PARAM_DCO_SUB_LEVEL, edit_params.dco_sub_level);
            snprintf(msg, sizeof(msg), "Sub Level: %u%%", (uint32_t)(edit_params.dco_sub_level * 100.0f));
            break;
        }
        case 7: {
            edit_params.dco_noise += (float)delta * 0.02f;
            if (edit_params.dco_noise < 0.0f) edit_params.dco_noise = 0.0f;
            if (edit_params.dco_noise > 1.0f) edit_params.dco_noise = 1.0f;
            hal_synth_set_param(JUNO_PARAM_DCO_NOISE, edit_params.dco_noise);
            snprintf(msg, sizeof(msg), "Noise Level: %u%%", (uint32_t)(edit_params.dco_noise * 100.0f));
            break;
        }
        default: break;
        }
        break;

    case 2:
        switch (col) {
        case 0: {
            int h = edit_params.hpf_position + delta;
            if (h < 0) h = 0;
            if (h > 3) h = 3;
            edit_params.hpf_position = h;
            hal_synth_set_param(JUNO_PARAM_HPF_POS, (float)edit_params.hpf_position);
            snprintf(msg, sizeof(msg), "HPF Position: %d", edit_params.hpf_position);
            break;
        }
        case 1: {
            edit_params.vcf_cutoff += (float)delta * 0.02f;
            if (edit_params.vcf_cutoff < 0.0f) edit_params.vcf_cutoff = 0.0f;
            if (edit_params.vcf_cutoff > 1.0f) edit_params.vcf_cutoff = 1.0f;
            hal_synth_set_param(JUNO_PARAM_VCF_CUTOFF, edit_params.vcf_cutoff);
            float hz = 28.0f * expf(7.135f * edit_params.vcf_cutoff);
            if (hz < 1000.0f) {
                snprintf(msg, sizeof(msg), "VCF Freq: %u Hz", (uint32_t)hz);
            } else {
                snprintf(msg, sizeof(msg), "VCF Freq: %.2f kHz", hz / 1000.0f);
            }
            break;
        }
        case 2: {
            edit_params.vcf_res += (float)delta * 0.02f;
            if (edit_params.vcf_res < 0.0f) edit_params.vcf_res = 0.0f;
            if (edit_params.vcf_res > 1.0f) edit_params.vcf_res = 1.0f;
            hal_synth_set_param(JUNO_PARAM_VCF_RES, edit_params.vcf_res);
            snprintf(msg, sizeof(msg), "VCF Resonance: %u%%", (uint32_t)(edit_params.vcf_res * 100.0f));
            break;
        }
        case 3: {
            edit_params.vcf_env_polarity = (edit_params.vcf_env_polarity < 0) ? 1 : -1;
            hal_synth_set_param(JUNO_PARAM_VCF_ENV_POL, (float)edit_params.vcf_env_polarity);
            snprintf(msg, sizeof(msg), "VCF Env: %s", (edit_params.vcf_env_polarity > 0) ? "Normal" : "Inverted");
            break;
        }
        case 4: {
            edit_params.vcf_env_mod += (float)delta * 0.02f;
            if (edit_params.vcf_env_mod < 0.0f) edit_params.vcf_env_mod = 0.0f;
            if (edit_params.vcf_env_mod > 1.0f) edit_params.vcf_env_mod = 1.0f;
            hal_synth_set_param(JUNO_PARAM_VCF_ENV_MOD, edit_params.vcf_env_mod);
            snprintf(msg, sizeof(msg), "VCF Env Amt: %u%%", (uint32_t)(edit_params.vcf_env_mod * 100.0f));
            break;
        }
        case 5: {
            edit_params.vcf_lfo_mod += (float)delta * 0.02f;
            if (edit_params.vcf_lfo_mod < 0.0f) edit_params.vcf_lfo_mod = 0.0f;
            if (edit_params.vcf_lfo_mod > 1.0f) edit_params.vcf_lfo_mod = 1.0f;
            hal_synth_set_param(JUNO_PARAM_VCF_LFO_MOD, edit_params.vcf_lfo_mod);
            snprintf(msg, sizeof(msg), "VCF LFO Amt: %.1f", edit_params.vcf_lfo_mod * 36.0f);
            break;
        }
        case 6: {
            edit_params.vcf_kbd_track += (float)delta * 0.02f;
            if (edit_params.vcf_kbd_track < 0.0f) edit_params.vcf_kbd_track = 0.0f;
            if (edit_params.vcf_kbd_track > 1.0f) edit_params.vcf_kbd_track = 1.0f;
            hal_synth_set_param(JUNO_PARAM_VCF_KBD, edit_params.vcf_kbd_track);
            snprintf(msg, sizeof(msg), "VCF Kbd Follow: %u%%", (uint32_t)(edit_params.vcf_kbd_track * 100.0f));
            break;
        }
        default: break;
        }
        break;

    case 3:
        switch (col) {
        case 0: {
            edit_params.vca_mode = (edit_params.vca_mode == 0) ? 1 : 0;
            hal_synth_set_param(JUNO_PARAM_VCA_MODE, (float)edit_params.vca_mode);
            snprintf(msg, sizeof(msg), "VCA Mode: %s", (edit_params.vca_mode == 0) ? "Envelope" : "Gate");
            break;
        }
        case 1: {
            edit_params.vca_level += (float)delta * 0.02f;
            if (edit_params.vca_level < 0.0f) edit_params.vca_level = 0.0f;
            if (edit_params.vca_level > 1.0f) edit_params.vca_level = 1.0f;
            hal_synth_set_param(JUNO_PARAM_VCA_LEVEL, edit_params.vca_level);
            float db = -10.0f + edit_params.vca_level * 20.0f;
            snprintf(msg, sizeof(msg), "VCA Level: %+.1f dB", db);
            break;
        }
        case 2: {
            edit_params.env_attack += (float)delta * 0.02f;
            if (edit_params.env_attack < 0.0f) edit_params.env_attack = 0.0f;
            if (edit_params.env_attack > 1.0f) edit_params.env_attack = 1.0f;
            hal_synth_set_param(JUNO_PARAM_ENV_ATTACK, edit_params.env_attack);
            uint32_t ms = 1 + (uint32_t)(edit_params.env_attack * 2999.0f);
            snprintf(msg, sizeof(msg), "ENV Attack: %u ms", ms);
            break;
        }
        case 3: {
            edit_params.env_decay += (float)delta * 0.02f;
            if (edit_params.env_decay < 0.0f) edit_params.env_decay = 0.0f;
            if (edit_params.env_decay > 1.0f) edit_params.env_decay = 1.0f;
            hal_synth_set_param(JUNO_PARAM_ENV_DECAY, edit_params.env_decay);
            float ms = 9.0f * expf(7.834f * edit_params.env_decay);
            if (ms < 1000.0f) snprintf(msg, sizeof(msg), "ENV Decay: %u ms", (uint32_t)ms);
            else snprintf(msg, sizeof(msg), "ENV Decay: %.2f s", ms / 1000.0f);
            break;
        }
        case 4: {
            edit_params.env_sustain += (float)delta * 0.02f;
            if (edit_params.env_sustain < 0.0f) edit_params.env_sustain = 0.0f;
            if (edit_params.env_sustain > 1.0f) edit_params.env_sustain = 1.0f;
            hal_synth_set_param(JUNO_PARAM_ENV_SUSTAIN, edit_params.env_sustain);
            snprintf(msg, sizeof(msg), "ENV Sustain: %u%%", (uint32_t)(edit_params.env_sustain * 100.0f));
            break;
        }
        case 5: {
            edit_params.env_release += (float)delta * 0.02f;
            if (edit_params.env_release < 0.0f) edit_params.env_release = 0.0f;
            if (edit_params.env_release > 1.0f) edit_params.env_release = 1.0f;
            hal_synth_set_param(JUNO_PARAM_ENV_RELEASE, edit_params.env_release);
            float ms = 9.0f * expf(7.834f * edit_params.env_release);
            if (ms < 1000.0f) snprintf(msg, sizeof(msg), "ENV Release: %u ms", (uint32_t)ms);
            else snprintf(msg, sizeof(msg), "ENV Release: %.2f s", ms / 1000.0f);
            break;
        }
        case 6: {
            edit_params.chorus_mode ^= 1;
            hal_synth_set_param(JUNO_PARAM_CHORUS_MODE, (float)edit_params.chorus_mode);
            snprintf(msg, sizeof(msg), "Chorus I: %s", (edit_params.chorus_mode & 1) ? "ON" : "OFF");
            break;
        }
        case 7: {
            edit_params.chorus_mode ^= 2;
            hal_synth_set_param(JUNO_PARAM_CHORUS_MODE, (float)edit_params.chorus_mode);
            snprintf(msg, sizeof(msg), "Chorus II: %s", (edit_params.chorus_mode & 2) ? "ON" : "OFF");
            break;
        }
        default: break;
        }
        break;
    }

    if (msg[0] != '\0') {
        show_banner(msg);
    }
}

void ui_patch_edit_on_encoder(uint8_t enc_index, int8_t delta)
{
    if (enc_index >= NUM_COLS) return;

    apply_param_delta(current_page, enc_index, delta);
    if (panel_controls) {
        lv_obj_invalidate(panel_controls);
    }
}

static void update_page_view(void)
{
    if (lbl_page) {
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "PAGE %d/4", current_page + 1);
        lv_label_set_text(lbl_page, pbuf);
    }



    /* Update Sub-labels below each column */
    for (int col = 0; col < NUM_COLS; col++) {
        const UiControlDef *def = &PAGE_CONTROLS[current_page][col];
        if (lbl_sub[col]) {
            lv_label_set_text(lbl_sub[col], def->label ? def->label : "---");
        }
    }

    /* Invalidate control panel to trigger re-draw */
    if (panel_controls) {
        lv_obj_invalidate(panel_controls);
    }
}
