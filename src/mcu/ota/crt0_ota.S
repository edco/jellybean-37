/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

	.section .startup, "ax"
	.globl _start
	.type _start, @function

_start:
	/* 1. Disable all interrupts */
	cli

	/* 2. Initialize private OTA stack in SRAM */
	r0 = _stack_top
	sp = r0

	/* 3. Zero-initialize BSS */
	r0 = _bss_start
	r1 = _bss_end
	r2 = 0
.Lclear_bss:
	if (r0 >= r1) goto .Lbss_done
	[r0++=4] = r2
	goto .Lclear_bss
.Lbss_done:

	/* 4. Memory barriers and jump to OTA entry */
	csync
	call ota_main

.Lhalt:
	goto .Lhalt

	.size _start, . - _start
