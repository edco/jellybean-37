/* Linker script for SMK-37 Pro Direct-to-RAM execution (AC791N / WL82) */

OUTPUT_FORMAT("elf32-pi32v2", "elf32-pi32v2", "elf32-pi32v2")
OUTPUT_ARCH(pi32v2)
ENTRY(_start)

MEMORY
{
    /* 0x01C02000 .. 0x01C7FD80 = 0x7DD80 = 515,456 bytes (503.4 KB).
     * Was ORIGIN 0x01C02000 / LENGTH 480K, which ended at 0x01C7A000 and so
     * threw away 23,936 bytes of real SRAM above it. The new end is exactly
     * UPDATA_BEG, the bottom of the boot-status retention window that must not
     * be used as ordinary RAM. */
    RAM (rwx) : ORIGIN = 0x01C02000, LENGTH = 0x7DD80  /* SRAM window: origin..UPDATA_BEG */
}

/* Physical SRAM ceiling for this part, from the SDK's own layout
 * (cpu/wl82/sdk_ld_sfc.c): RAM0_SIZE = 0x1c7fe00 - 0x1c00000 - TLB_SIZE
 * - BOOT_INFO_SIZE - 128, i.e. the array ends at 0x1C7FE00 with the top 128
 * bytes (UPDATA_BEG = 0x1C7FD80) reserved for update-mode/boot-status
 * retention. Cross-checked against the stock firmware image: it stores the
 * boot status word at 0x1C7FD80 (mcu_midi.c) and its control-surface IRQ
 * mask table at 0x1C7FE00.
 *
 * 0x01C02000 is the MMU TLB base (TLB_SIZE = 2 x 0x1000), which is why the
 * app origin sits above it rather than at 0x01C00000.
 */
SRAM_PHYS_END = 0x01C7FE00;

/* Minimum usable dlmalloc/tlsf arena, in bytes.
 *
 * The firmware's measured floor is ~35.4 KB (see docs/RAM_BUDGET.md):
 * 23,552 B of FreeRTOS task stacks + 448 B of TCBs + 11,392 B of Dexed voice
 * objects, all carved out of this one arena because the port runs on heap_3
 * (sbrk-backed; configTOTAL_HEAP_SIZE is inert).
 *
 * 48 KB leaves ~13 KB of headroom over that floor, so the guard trips while
 * there is still somewhere to put newly added static RAM rather than when the
 * heap has already been starved. Current heap is 93,984 B. */
HEAP_MIN = 48K;

SECTIONS
{
    . = ORIGIN(RAM);

    .text : {
        _text_start = .;
        KEEP(*(.startup))
        *(.text*)
        *(.rodata*)
        *(.boot_code)
        *(.flushinv_icache)
        *(.spi_code)
        *(.dlmalloc_code)
        *(.dlmalloc_const)
        *(.mem_heap_code)
        *(.mem_heap_const)
        *(.volatile_ram_code)
        *(.os_code)
        *(.os_const)
        *(.os_str)
        *(.os_critical_code)
        *(.os_port_code)
        *(.os_port_const)
        /* Build-id note at the very end of .text: elf2bin.sh packs only
         * .text+.data contiguously and run_ram.sh loads that blob at the RAM
         * origin, so the note must sit inside .text (not a standalone .note
         * section) and .data must begin at _text_end with no gap. */
        . = ALIGN(4);
        __start_gnu_build_id = .;
        KEEP(*(.note.gnu.build-id))
        __stop_gnu_build_id = .;
        . = ALIGN(4);
        _text_end = .;
    } > RAM

    .data : {
        . = ALIGN(4);
        _data_start = .;
        _data_lma = _data_start;
        *(.data*)
        *(.boot_info)
        *(.sys_cpu_timer)
        *(.volatile_ram)
        *(.os_data)
        *(.sram)
        . = ALIGN(4);
        _data_end = .;
    } > RAM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        *(.os_bss)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    /* Core 0 stack top. Core 0's stack grows DOWN from here, into the heap
     * region (the original design never reserved a separate slot for it, and
     * that has always worked -- see the comment on _sstack_top below).
     *
     * This was 0x01C78000 until MCUno-60 landed in 7c7492be. That commit added
     * ~50 KB of .text, which pushed .bss (allocated contiguously after .data)
     * up to 0x01C79BB8 -- 7,096 bytes PAST the old _stack_top. The build still
     * linked cleanly and produced an image in which 104 live globals, including
     * g_core_event_ring, were allocated inside the core-1 stack window. It hung
     * at the diagnostic screen.
     *
     * The old value was derived from ORIGIN(RAM) + 480K = 0x01C7A000, but that
     * 480K ceiling was simply wrong: the SDK's own layout for this part ends the
     * SRAM array at 0x01C7FE00 (see SRAM_PHYS_END above). The stack window now
     * sits in the real headroom instead.
     */
    _stack_top  = 0x01C7E000;
    /* Core 1 stack. synth_init() runs the Dexed C++ constructor chain
     * (DexedEngine ctor -> new ProcessorVoice[16] -> 16x new Dx7Note(692B),
     * then activate() and decodeVoice() with a 156-byte local) plus the
     * whole real-time render path. The previous 4 KB (_sstack_top =
     * _stack_top + 0x1000) overflowed: a core 1 overflow runs off the top of
     * RAM into unmapped space and faults the whole SoC. 6 KB is the floor.
     *
     * Core 1 occupies [_stack_top, _sstack_top) and grows down from
     * _sstack_top, so keep the two symbols ordered this way and keep the size
     * >= 0x1800. The ASSERTs below enforce the ordering.
     *
     * 0x01C7F800 leaves 1,408 bytes clear below UPDATA_BEG (0x01C7FD80), where
     * mcu_midi.c stores the boot-status word that must survive a soft reset.
     * Do not raise this into that window. */
    _sstack_top = 0x01C7F800;

    HEAP_BEGIN = (_bss_end + 31) / 32 * 32;
    HEAP_END = _stack_top - 32;

    /* ---- Layout guards -------------------------------------------------
     * These stacks are absolute symbols declared OUTSIDE any output section,
     * while .text/.data/.bss are all `> RAM`. The linker therefore has no
     * reason to complain when an assigned section grows up into a stack that
     * was hardcoded at a fixed address -- it happily produces an image whose
     * .bss overlaps live core stacks and whose heap has inverted. Both failure
     * modes are silent at runtime (hang, no diagnostic output).
     *
     * Refuse to link instead. MCUno-60 added ~50 KB of .text in 7c7492be,
     * which pushed _bss_end from 0x01C6D4F8 to 0x01C79BB8 -- 7,096 bytes past
     * _stack_top -- and the build still succeeded silently.
     */
    ASSERT(_bss_end <= _stack_top,
           "RAM layout: .bss overruns the core stacks (_bss_end > _stack_top). Static RAM has grown past the stack window -- reclaim space or remap the stacks.")
    ASSERT(_sstack_top <= SRAM_PHYS_END,
           "RAM layout: core-1 stack top is above the physical SRAM end.")
    ASSERT(HEAP_BEGIN + HEAP_MIN <= HEAP_END,
           "RAM layout: heap is inverted or too small (need >= 16 KB between HEAP_BEGIN and HEAP_END).")
    ASSERT(_sstack_top > _stack_top,
           "RAM layout: _sstack_top must sit above _stack_top (core-1 stack occupies [_stack_top, _sstack_top)).")

    FREE_IACHE_WAY = 0;
    FREE_DACHE_WAY = 7;
    cache_ram_data_vma = 0;
    cache_ram_data_lma = 0;
    cache_ram_data_size = 0;
    cache_ram_bss_vma = 0;
    cache_ram_bss_size = 0;
    lp_target_begin = .;
    lp_target_end = .;
}




