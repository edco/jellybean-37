
        *(.LOG_TAG_CONST*)

        *(.fat_data_code)

        . = ALIGN(4);
        *(.update_const)
        *(.update_code)
        *(.bt_updata_ram_code)

        . = ALIGN(4);
        __VERSION_BEGIN = .;
        KEEP(*(.sys.version))
        __VERSION_END = .;

#if defined CONFIG_CXX_SUPPORT
        . = ALIGN(4);
        _ctors_begin = .;
        PROVIDE(ctors_begin = .);
	    KEEP(*(SORT(.ctors.*)))
	   	KEEP(*(.ctors))
        _ctors_end = .;
        PROVIDE(ctors_end = .);
        . = ALIGN(4);
#endif

#if defined CONFIG_NO_SDRAM_ENABLE
        *(.dlmalloc_code)
        *(.dlmalloc_const)
        *(.mem_heap_code)
        *(.mem_heap_const)

        _os_begin = .;
        PROVIDE(os_begin = .);
        *(.os_code)
        *(.os_const)
        *(.os_str)
        *(.os_critical_code)
        *(.os_port_code)
        *(.os_port_const)
        _os_end = .;
        PROVIDE(os_end = .);
#endif

#if defined CONFIG_BT_ENABLE
        crypto_begin = .;
        *(.crypto_ecdh_code)
        *(.crypto_ecdh_const)

        *(.crypto_bigint_code)
        *(.crypto_bigint_const)

        *(.crypto_code)
        *(.crypto_const)

        *(.ECDH_sample_code)
        *(.ECDH_sample_const)

        *(.uECC_code)
        *(.uECC_const)

        *(.hmac_code)
        *(.hmac_const)

        *(.hash_sample_code)
        *(.hash_sample_const)

        *(.aes_cmac_sample_code)
        *(.aes_cmac_sample_const)
        crypto_end = .;
        crypto_size = . - crypto_begin;
#endif

#if defined CONFIG_FINSH_ENABLE
        PROVIDE(__fsymtab_start = .);
        KEEP(*(.FSymTab))
        PROVIDE(__fsymtab_end = .);
        PROVIDE(__vsymtab_start = .);
        KEEP(*(.VSymTab))
        PROVIDE(__vsymtab_end = .);
        PROVIDE(__rtatcmdtab_start = .);
        KEEP(*(.RtAtCmdTab))
        PROVIDE(__rtatcmdtab_end = .);
#endif

