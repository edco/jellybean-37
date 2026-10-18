/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* Low-level C runtime startup for JieLi AC791N / WL82 (PI32v2) */

	.section .startup, "ax"
	.globl _start
	.type _start, @function

_start:
	/* 1. Preserve return address and callee-saved registers on stack */
	[--sp] = {rets, r15-r0}

	/* 2. Copy initialized data (.data and .volatile_ram_code) from LMA to VMA */
	r0 = _data_start
	r1 = _data_lma
	if (r0 == r1) goto .Ldata_done
	r2 = _data_end
.Lcopy_data:
	if (r0 >= r2) goto .Ldata_done
	r3 = [r1++=4]
	[r0++=4] = r3
	goto .Lcopy_data
.Ldata_done:

	/* 3. Zero-initialize BSS section */
	r0 = _bss_start
	r1 = _bss_end
	r2 = 0
.Lclear_bss:
	if (r0 >= r1) goto .Lbss_done
	[r0++=4] = r2
	goto .Lclear_bss
.Lbss_done:

	/* 4. Flush and synchronize memory */
	csync

	/* 5. Call C application main() */
	call main

	/* 6. Restore registers and return cleanly to MaskROM */
	{rets, r15-r0} = [sp++]
	rts

	.size _start, . - _start

/* Core 1 Hardware Reset Entry Trampoline */
	.section .volatile_ram_code, "ax"
	.globl cpu1_start
	.type cpu1_start, @function

cpu1_start:
	/* 1. Set Core 1 stack pointer to linker-resolved secondary stack top */
	r0 = _sstack_top
	sp = r0
	usp = r0

	/* 2. Transition from Reset/Exception mode to Thread mode via reti + rti */
	reti = cpu1_entry
	rti

	.size cpu1_start, . - cpu1_start
