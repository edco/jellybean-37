#ifndef __SDRAM_H__
#define __SDRAM_H__

#include "generic/typedef.h"

enum {
    SDRAM,
    DDR1,
    DDR2,
};

struct sdram_cfg_info_t {
    u32 sdram_size;
    u32 sdram_test_size;
    u32 sdram_config_val;//i:0-3, j:4-7; k:8-11; m:12-15; p2:16-19; q3:20-23; sdram_cl:24-27

    u32 sdram_mode: 2;
    u32 sdram_pll3_en: 1;
    u32 sdram_pll3_nousb_en: 1;
    u32 sdram_cl: 4;
    u32 sdram_rlcnt: 4;
    u32 sdram_d_dly: 1;
    u32 sdram_q_dly: 1;
    u32 sdram_phase: 2;
    u32 sdram_dq_dly_trm: 4;
    u32 dummy : 12;
};

void sdram_init(const struct sdram_cfg_info_t *sdram_info);

void sdram_uninit(void);

#endif
