        *(.jlsp_code)
        . = ALIGN(4);
        *(.jlsp_const)

#if defined CONFIG_DNS_ENC_ENABLE
        . = ALIGN(4);
        *(.dns_common_data)
        . = ALIGN(4);
        *(.dns_16k_data)
        . = ALIGN(4);
        *(.dns_8k_data)
        . = ALIGN(4);
        *(.dns_param_data_single)
        . = ALIGN(4);
        *(.dns_param_data_dual)
#endif

#if defined CONFIG_AEC_ENC_ENABLE
        *(.sms_code)
        *(.sms_sparse_code)
        . = ALIGN(4);
        *(.opcore_maskrom)
        . = ALIGN(4);
        *(.sms_const)
#endif

#if defined CONFIG_REVERB_MODE_ENABLE
        *(.echo_cal_code)
        *(.echo_cal_const)
        *(.reverb_cal_code)
        *(.reverb_cal_const)
        *(.reverb0_code)
        *(.reverb0_const)
        *(.platereverb_code)
        *(.platereverb_const)
        *(.reverb_cal_sparse_code)
        *(.howlings_phf_sparse_code)
        *(.howlings_phf_code)
        *(.howlings_phf_const)
#endif

#ifdef CONFIG_SPECTRUM_FFT_EFFECT_ENABLE
        *(.specshow_code)
        *(.specshow_const)
        *(.specshow_sparse_code)
        *(.specshow_sparse_const)
#endif

#ifdef CONFIG_VIRTUAL_BASS_ENABLE
		*(.audio_vbass_code)
		*(.audio_vbass_const)
		*(.vbss_code)
		*(.vbss_const)
		*(.vbss_sparse_code)
		*(.vbss_sparse_const)
#endif

#if defined CONFIG_JLA_ENC_ENABLE || defined CONFIG_JLA_DEC_ENABLE
        . = ALIGN(4);
        *(.jla_codec_c_const)
        *(.jla_codec_ari_c_const)

        *(.jla_codec_c_sparse_code)
        *(.jla_codec_ari_c_code)
        *(.jla_codec_d_code)
        *(.jla_codec_c_code)
        *(.jla_codec_e_code)

        *(.jla_codec_t8k_code)
        *(.jla_codec_t16k_code)
        *(.jla_codec_t24k_code)
        *(.jla_codec_t32k_code)
        *(.jla_codec_t441k_code)
#endif

#if defined CONFIG_ADPCM_ENC_ENABLE
        *(.adpcm_encode_code)
        *(.adpcm_encode_sparse_code)
        *(.adpcm_encode_const)
        *(.adpcm_encode_sparse_const)
#endif

#if defined CONFIG_DTS_DEC_ENABLE
        *(.dts_dec_code)
        *(.dts_dec_const)
        *(.dts_dec_ff_const)
        *(.dts_dec_sparse_code)
        *(.dts_dec_sparse_const)
#endif

#if defined CONFIG_OPUS_DEC_ENABLE
        *(.wtgv2dec_code)
        *(.wtg_decv2_sparse_code)
        *(.wtgv2dec_const)
        *(.wtgv2dec_str)
        
        *(.ogg_opus_dec.text.cache.L2.run)
        *(.ogg_opus_dec.text)
        *(.ogg_opus_dec.text.cache.L2.const)
#endif

#if defined CONFIG_OPUS_ENC_ENABLE
        *(.opus_silk_enc.data)
        *(.opus_silk_enc.data.bss)
        *(.opus_silk_enc.text.cache.L2.const)
        *(.opus_silk_enc.text.cache.L2.run)

        *(.opus_stenc.data.bss)
        *(.opus_stenc.data)
        *(.opus_stenc.text.cache.L2.const)
        *(.opus_stenc.text.cache.L2.run)
        *(.opus_stenc.text)
        *(.opus_stenc.text.const)
#endif

#if defined CONFIG_MP3_DEC_ENABLE
        *(.mp3_dec_sparse_code)
        *(.mp3_dec_sparse_const)
        *(.mp3_dec_code)
        *(.mp3_dec_const)
#endif

#if defined CONFIG_FLAC_DEC_ENABLE
        *(.flac_dec_sparse_code)
        *(.flac_dec_sparse_const)
        *(.flac_dec_code)
        *(.flac_dec_const)
#endif

#if TCFG_EQ_ENABLE
        *(.compressor_sparse_code)
        *(.compressor_sparse_const)
        *(.limiter_sparse_code)
        *(.limiter_sparse_const)
        *(.crossOver_sparse_const)
        *(.limiter_code)
        *(.limiter_const)
        *(.compressor_code)
        *(.compressor_const)
#endif
        *(.crossOver_sparse_code)
	
#if defined CONFIG_WAV_DEC_ENABLE
        *(.wav_dec_sparse_code)
        *(.wav_dec_sparse_const)
        *(.wav_dec_code)
        *(.wav_dec_const)
#endif

#if defined CONFIG_WMA_DEC_ENABLE
        *(.wma_dec_code)
        *(.wma_dec_const)
        *(.wma_dec_sparse_code)
        *(.wma_dec_sparse_const)
#endif

#if defined CONFIG_APE_DEC_ENABLE
        *(.ape_dec_code)
        *(.ape_dec_const)
        *(.ape_dec_sparse_code)
        *(.ape_dec_sparse_const)
#endif

#ifdef CONFIG_MSBC_DEC_ENABLE
        *(.bt_audioplc_code)
        *(.bt_audioplc_sparse_code)
        *(.bt_audioplc_const)
        *(.plc_code)
        *(.plc_const)
#endif

#if defined CONFIG_BT_ENABLE
        *(.sbc_eng_code)
#endif

#if defined CONFIG_ASR_ALGORITHM && defined CONFIG_NO_SDRAM_ENABLE
        . = ALIGN(4);
        *(.libaisp.rodata)
        *(.libaisp.text)
#endif

#if defined CONFIG_MIDI_CTRL_DEC_ENABLE
    *(.midi.text.cache.L2.code)
    *(.midi.text.cache.L2.const)
    *(.midi.text)
    *(.midi.text.const)
#endif
