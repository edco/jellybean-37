/* Linker script for SMK-37 Pro Flash XIP execution (AC791N / WL82) */

OUTPUT_FORMAT("elf32-pi32v2", "elf32-pi32v2", "elf32-pi32v2")
OUTPUT_ARCH(pi32v2)
ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x02000120, LENGTH = 577600   /* 0x02000120 - 0x0208D160 */
    RAM   (rwx) : ORIGIN = 0x01C00000, LENGTH = 0x45E94  /* Internal SRAM (280 KB) */
}

/* Physical SRAM ceiling for this part. See the longer note in ram_app.ld:
 * the SDK layout (cpu/wl82/sdk_ld_sfc.c) ends the array at 0x1C7FE00 and
 * reserves the top 128 bytes (UPDATA_BEG = 0x1C7FD80) for boot retention.
 * Note the RAM() LENGTH above is deliberately far smaller -- it is the range
 * the flash image actually occupies, not the hardware limit. */
RAM_PHYS_END = 0x01C7FE00;

/* Minimum usable dlmalloc/tlsf arena, in bytes. */
HEAP_MIN = 16K;

SECTIONS
{
    . = ORIGIN(FLASH);

    .text : {
        _text_start = .;
        KEEP(*(.startup))
        *(EXCLUDE_FILE(*synth_engine* *mcu_audio* *juno_shim* *mcuno-32*) .text*)
        *(EXCLUDE_FILE(*synth_engine* *mcu_audio* *juno_shim* *mcuno-32*) .rodata*)
        *(.boot_code)
        *(.dlmalloc_code)
        *(.dlmalloc_const)
        *(.mem_heap_code)
        *(.mem_heap_const)
        *(.os_code)
        *(.os_const)
        *(.os_str)
        *(.os_critical_code)
        *(.os_port_code)
        *(.os_port_const)
        /* Build-id note at the very end of .text so elf2bin.sh (which packs
         * only .text+.data) carries it and .data's LMA follows .text with no
         * gap. A standalone .note section would be dropped from the image. */
        . = ALIGN(4);
        __start_gnu_build_id = .;
        KEEP(*(.note.gnu.build-id))
        __stop_gnu_build_id = .;
        . = ALIGN(4);
        _text_end = .;
    } > FLASH

    _data_lma = .;

    .data : AT(_data_lma) {
        . = ALIGN(4);
        _data_start = .;
        *(.volatile_ram_code)
        *synth_engine*(.text*)
        *synth_engine*(.rodata*)
        *mcu_audio*(.text*)
        *mcu_audio*(.rodata*)
        *juno_shim*(.text*)
        *juno_shim*(.rodata*)
        *mcuno-32*(.text*)
        *mcuno-32*(.rodata*)
        *(.flushinv_icache)
        *(.spi_code)
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

    _stack_top  = 0x01C3A2D4;
    _sstack_top = 0x01C3B2D4;

    HEAP_BEGIN = (_bss_end + 31) / 32 * 32;
    HEAP_END = _stack_top - 32;

    /* ---- Layout guards -------------------------------------------------
     * _stack_top/_sstack_top are absolute symbols declared OUTSIDE any output
     * section while .text/.data/.bss are `> RAM`, so the linker cannot detect
     * an assigned section growing into a hardcoded stack address. See the
     * equivalent block in ram_app.ld for the full rationale. Fail the link
     * rather than emit a silently broken image.
     */
    ASSERT(_bss_end <= _stack_top,
           "FLASH layout: .bss overruns the core stacks (_bss_end > _stack_top). Static RAM has grown past the stack window -- reclaim space or remap the stacks.")
    ASSERT(_sstack_top <= RAM_PHYS_END,
           "FLASH layout: core-1 stack top is above the physical SRAM end.")
    ASSERT(HEAP_BEGIN + HEAP_MIN <= HEAP_END,
           "FLASH layout: heap is inverted or too small (need >= 16 KB between HEAP_BEGIN and HEAP_END).")
    ASSERT(_sstack_top > _stack_top,
           "FLASH layout: _sstack_top must sit above _stack_top (core-1 stack occupies [_stack_top, _sstack_top)).")

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




