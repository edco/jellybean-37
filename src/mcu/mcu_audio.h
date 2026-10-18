/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_audio.h
 * SMK-37 Pro Audio Output Driver - Direct DMA & Sub-Buffer Interface
 */

#ifndef MCU_AUDIO_H
#define MCU_AUDIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_BLOCK_FRAMES      128
#define AUDIO_DMA_SUB_WORDS     (AUDIO_BLOCK_FRAMES * 2)

/**
 * Get pointer to the 32-bit DMA sub-buffer (0 or 1).
 * @param subbuf_idx Sub-buffer index (0 or 1).
 * @return Pointer to 256 words of 32-bit DMA audio data.
 */
int32_t *mcu_audio_get_subbuf_ptr(uint8_t subbuf_idx);

/**
 * Render and format-scale one quantum of 16-bit PCM stereo audio directly
 * into the 32-bit DMA target buffer, applying master volume scaling in a single pass.
 * @param dma_dst Destination pointer in DMA buffer (interleaved 32-bit stereo).
 * @param pcm16 Source interleaved 16-bit stereo PCM samples.
 * @param frames Number of stereo frames to convert and scale.
 */
void mcu_audio_render_quantum(int32_t *dma_dst, const int16_t *pcm16, size_t frames);

/**
 * Flush dirty cache lines of the given DMA sub-buffer to physical SRAM.
 * @param subbuf_idx Sub-buffer index (0 or 1).
 */
void mcu_audio_flush_subbuf(uint8_t subbuf_idx);

/**
 * Legacy block render entry point for monolithic 128-frame buffers.
 * @param subbuf_idx Sub-buffer index (0 or 1).
 * @param pcm16 Source interleaved 16-bit stereo PCM samples.
 * @param frames Number of stereo frames.
 */
void mcu_audio_render_block(uint8_t subbuf_idx, const int16_t *pcm16, size_t frames);

#ifdef __cplusplus
}
#endif

#endif /* MCU_AUDIO_H */
