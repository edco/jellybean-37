/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

MEMORY
{
    ram (rwx) : ORIGIN = 0x01C0A800, LENGTH = 48K
}

ENTRY(_start)

SECTIONS
{
    .text : {
        KEEP(*(.startup))
        *(.text*)
        *(.rodata*)
        *(.data*)
        . = ALIGN(4);
    } > ram

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    _stack_top = 0x01C16000;
}
