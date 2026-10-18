/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file patch_banks.c
 * Bank and Patch metadata for Synthesizer Engines (Dexed, MCUno-60)
 */

#include "patch_banks.h"
#include "mcuno60_patches.h"
#include <stddef.h>

static const char *ENGINE_NAMES[SYNTH_NUM_ENGINES] = {
    "Dexed (FM)",
    "MCUno-60"
};

static const char *BANK_NAMES[PATCH_NUM_BANKS] = {
    "RitCh1.syx",
    "RitCh2.syx",
    "rom1a.syx",
    "rom1b.syx",
    "rom2a.syx",
    "rom2b.syx",
    "rom3a.syx",
    "rom3b.syx",
    "rom4a.syx",
    "rom4b.syx",
};

static const char *PATCH_NAMES[PATCH_NUM_BANKS][PATCH_NUM_PATCHES_PER_BANK] = {
    /* Bank 0 */
    {
        "FM-Rhodes",
        "FM-Rhodes2",
        "Meinecke",
        "CCCP-70",
        "Steinway",
        "Korg CX3",
        "Leadharp",
        "Synclavier",
        "ELEC VANG",
        "FATSO. RT.",
        "FATSYNTH A",
        "JUPITER 1",
        "JUPITER 8",
        "Jupiter",
        "ANALOG 2",
        "JUNO 106 6",
        "ALG.2. RT.",
        "Mooger Low",
        "Soft-Moog",
        "Mooger",
        "Mini-Moog",
        "Mini-Moog2",
        "Moog Strng",
        "ANASTRINGS",
        "ANALOG  1",
        "ANALOG",
        "Horn+Str.",
        "St Pad  4c",
        "St Pad  5c",
        "STRINGS M2",
        "SAMPLESTRG",
        "MID.STRG B",
    },
    /* Bank 1 */
    {
        "HORNS 5A W",
        "80 F",
        "8RASS   3",
        "JUMP",
        "JUMP 2SENS",
        "FLUTE   1",
        "CORNET HS",
        "CP 9O",
        "B-R-ASS 1",
        "BASS    1",
        "SCHMIDT 2",
        "FUNK-RHYT.",
        "KLA5 HS",
        "D 5-6.....",
        "CLIC HS",
        "JUMP 3",
        "ORGEL 1",
        "B 3 ORGAN",
        "PIANO   2",
        "BREAK FREE",
        "BASCHMIDT",
        "RHODES1 RH",
        "RHODES 2",
        "HOHNER D 6",
        "SYNTH R.T.",
        "STRING SYN",
        "FANFARE H1",
        "JUNK",
        "--OBERHEIM",
        "CHOIR.",
        "BRS.STRS.",
        "LAAZCHRD1",
    },
    /* Bank 2 */
    {
        "BRASS   1",
        "BRASS   2",
        "BRASS   3",
        "STRINGS 1",
        "STRINGS 2",
        "STRINGS 3",
        "ORCHESTRA",
        "PIANO   1",
        "PIANO   2",
        "PIANO   3",
        "E.PIANO 1",
        "GUITAR  1",
        "GUITAR  2",
        "SYN-LEAD 1",
        "BASS    1",
        "BASS    2",
        "E.ORGAN 1",
        "PIPES   1",
        "HARPSICH 1",
        "CLAV    1",
        "VIBE    1",
        "MARIMBA",
        "KOTO",
        "FLUTE   1",
        "ORCH-CHIME",
        "TUB BELLS",
        "STEEL DRUM",
        "TIMPANI",
        "REFS WHISL",
        "VOICE   1",
        "TRAIN",
        "TAKE OFF",
    },
    /* Bank 3 */
    {
        "PIANO   4",
        "PIANO   5",
        "E.PIANO 2",
        "E.PIANO 3",
        "E.PIANO 4",
        "PIANO 5THS",
        "CELESTE",
        "TOY PIANO",
        "HARPSICH 2",
        "HARPSICH 3",
        "CLAV    2",
        "CLAV    3",
        "E.ORGAN 2",
        "E.ORGAN 3",
        "E.ORGAN 4",
        "E.ORGAN 5",
        "PIPES   2",
        "PIPES   3",
        "PIPES   4",
        "CALIOPE",
        "ACCORDION",
        "SITAR",
        "GUITAR  3",
        "GUITAR  4",
        "GUITAR  5",
        "GUITAR  6",
        "LUTE",
        "BANJO",
        "HARP    1",
        "HARP    2",
        "BASS    3",
        "BASS    4",
    },
    /* Bank 4 */
    {
        "PICCOLO",
        "FLUTE   2",
        "OBOE",
        "CLARINET",
        "SAX BC",
        "BASSOON",
        "STRINGS 4",
        "STRINGS 5",
        "STRINGS 6",
        "STRINGS 7",
        "STRINGS 8",
        "BRASS   4",
        "BRASS   5",
        "BRASS 6 BC",
        "BRASS   7",
        "BRASS   8",
        "RECORDER",
        "HARMONICA1",
        "HRMNCA2 BC",
        "VOICE   2",
        "VOICE   3",
        "GLOKENSPL",
        "VIBE    2",
        "XYLOPHONE",
        "CHIMES",
        "GONG    1",
        "GONG    2",
        "BELLS",
        "COW BELL",
        "BLOCK",
        "FLEXATONE",
        "LOG DRUM",
    },
    /* Bank 5 */
    {
        "SYN-LEAD 2",
        "SYN-LEAD 3",
        "SYN-LEAD 4",
        "SYN-LEAD 5",
        "SYN-CLAV 1",
        "SYN-CLAV 2",
        "SYN-CLAV 3",
        "SYN-PIANO",
        "SYNBRASS 1",
        "SYNBRASS 2",
        "SYNORGAN 1",
        "SYNORGAN 2",
        "SYN-VOX",
        "SYN-ORCH",
        "SYN-BASS 1",
        "SYN-BASS 2",
        "HARP-FLUTE",
        "BELL-FLUTE",
        "E.P-BRS BC",
        "T.BL-EXPA",
        "CHIME-STRG",
        "B.DRM-SNAR",
        "SHIMMER",
        "EVOLUTION",
        "WATER GDN",
        "WASP STING",
        "LASER GUN",
        "DESCENT",
        "OCTAVE WAR",
        "GRAND PRIX",
        "ST.HELENS",
        "EXPLOSION",
    },
    /* Bank 6 */
    {
        "FLUTE   1",
        "HARPSICH 1",
        "STRG ENS 1",
        "BRIGHT BOW",
        "BRASSHORNS",
        "BR TRUMPET",
        "MARIMBA",
        "E.PIANO 1",
        "PIANO   1",
        "PIPES   1",
        "E.ORGAN 1",
        "E.BASS  1",
        "CLAV    1",
        "HARMONICA1",
        "JAZZ GUIT1",
        "PRC SYNTH1",
        "SAX BC",
        "FRETLESS 1",
        "HARP    1",
        "TIMPANI",
        "HEAVYMETAL",
        "STEEL DRUM",
        "SYN-LEAD 1",
        "VOICES BC",
        "CLAV ENS",
        "LASERSWEEP",
        "TUB ERUPT",
        "GRAND PRIX",
        "REFS WHISL",
        "TRAIN",
        "BRASS S H",
        "TAKE OFF",
    },
    /* Bank 7 */
    {
        "PIANO   2",
        "E.GRAND 1",
        "E.GRAND 2",
        "HONKY TONK",
        "E.PIANO 2",
        "E.PIANO 3",
        "E.PIANO 4",
        "CELESTE",
        "FUNK CLAV",
        "CLAV ENS 2",
        "PERC CLAV",
        "HARPSICH 2",
        "E.ORGAN 2",
        "E.ORGAN 3",
        "60-S ORGAN",
        "PIPES   2",
        "PIPES   3",
        "CALIOPE",
        "ACCORDION",
        "TOY PIANO",
        "SITAR",
        "KOTO",
        "JAZZ GUIT2",
        "SPANISHGTR",
        "FOLK GUIT",
        "LUTE",
        "BANJO",
        "CLAS.GUIT",
        "HARP    2",
        "E.BASS  2",
        "FRETLESS 2",
        "PLUCK BASS",
    },
    /* Bank 8 */
    {
        "PICCOLO",
        "FLUTE   2",
        "OBOE",
        "CLARINET",
        "BASSOON",
        "PAN FLUTE",
        "LEAD BRASS",
        "HORNS",
        "SOLO TBONE",
        "BRASS BC",
        "BRASS 5THS",
        "SYNTHBRASS",
        "STRG QRT 1",
        "STRG ENS 2",
        "VIOLA SECN",
        "STRGS LOW",
        "HIGH STRGS",
        "PIZZ STGS",
        "STG CRSNDO",
        "STGS 5THS",
        "BELLS",
        "TUB BELLS",
        "RECORDERS",
        "CHIMES",
        "VOICES",
        "XYLOPHONE",
        "COWBELL",
        "WOOD BLOCK",
        "FLEXATONE",
        "LOG DRUM",
        "GLOKENSPL",
        "VIBE",
    },
    /* Bank 9 */
    {
        "CLAV-E.PNO",
        "PERC BRASS",
        "PRC SYNTH2",
        "HARPSI-STG",
        "CHIME-STRG",
        "HARP-FLUTE",
        "BELL-FLUTE",
        "STRG-CHIME",
        "STRG-MARIM",
        "STRG-PIZZT",
        "ORCHESTRA",
        "LEAD GUITR",
        "PIANO-BRS",
        "BRS-CHIME",
        "B.DRM-SNAR",
        "E.P-BRS BC",
        "ORG-BRS BC",
        "CLV-BRS BC",
        "WHISTLES",
        "FILTER SWP",
        "FUNKY RISE",
        "WILD BOAR",
        "SHIMMER",
        "EVOLUTION",
        "WATER GDN",
        "WASP STING",
        "MULTI NOTE",
        "DESCENT",
        "OCTAVE WAR",
        "..GOTCHA..",
        "ST.HELENS",
        "EXPLOSION",
    },
};

uint8_t patch_banks_get_num_engines(void)
{
    return SYNTH_NUM_ENGINES;
}

const char *patch_banks_get_engine_name(uint8_t engine_idx)
{
    if (engine_idx >= SYNTH_NUM_ENGINES) {
        return "Unknown";
    }
    return ENGINE_NAMES[engine_idx];
}

uint8_t patch_banks_get_num_banks_for_engine(uint8_t engine_idx)
{
    if (engine_idx == SYNTH_ENGINE_MCUNO60) {
        return MCUNO60_NUM_BANKS;
    }
    return PATCH_NUM_BANKS;
}

uint8_t patch_banks_get_num_patches_for_engine(uint8_t engine_idx, uint8_t bank_idx)
{
    if (engine_idx == SYNTH_ENGINE_MCUNO60) {
        return mcuno60_get_bank_patch_count(bank_idx);
    }
    return PATCH_NUM_PATCHES_PER_BANK;
}

const char *patch_banks_get_bank_name_for_engine(uint8_t engine_idx, uint8_t bank_idx)
{
    if (engine_idx == SYNTH_ENGINE_MCUNO60) {
        return mcuno60_get_bank_name(bank_idx);
    }
    if (bank_idx >= PATCH_NUM_BANKS) {
        return "Unknown";
    }
    return BANK_NAMES[bank_idx];
}

const char *patch_banks_get_patch_name_for_engine(uint8_t engine_idx, uint8_t bank_idx, uint8_t patch_idx)
{
    if (engine_idx == SYNTH_ENGINE_MCUNO60) {
        return mcuno60_get_patch_name(bank_idx, patch_idx);
    }
    if (bank_idx >= PATCH_NUM_BANKS || patch_idx >= PATCH_NUM_PATCHES_PER_BANK) {
        return "Unknown";
    }
    return PATCH_NAMES[bank_idx][patch_idx];
}

const char *patch_banks_get_bank_name(uint8_t bank_idx)
{
    return patch_banks_get_bank_name_for_engine(SYNTH_ENGINE_DEXED, bank_idx);
}

const char *patch_banks_get_patch_name(uint8_t bank_idx, uint8_t patch_idx)
{
    return patch_banks_get_patch_name_for_engine(SYNTH_ENGINE_DEXED, bank_idx, patch_idx);
}
