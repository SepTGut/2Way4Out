/**
 * @file delay.h
 * @brief Public API for the per-band delay line.
 *
 * Simple circular buffer delay for time-alignment between drivers.
 * Supports integer sample delay (0 – DELAY_MAX_SAMPLES).
 *
 * One delay line per channel per driver (4 total: low-L, low-R, high-L, high-R).
 */

#pragma once

#include "dsp_config.h"
#include "globals.h"

/**
 * @brief Allocate delay line buffers.
 *
 * Must be called once during setup after dsp_config is initialised.
 * Buffers are allocated in DMA-capable memory for fast access.
 *
 * @return true if all buffers were allocated successfully.
 */
void delay_alloc_buffers();

/**
 * @brief Free delay line buffers.
 */
void delay_free_buffers();

/**
 * @brief Process one stereo sample pair through the delay for one driver.
 *
 * Reads from the circular buffer at (write_idx - delay_samples) and
 * writes the current input at write_idx.
 *
 * @param in_l    Left input sample.
 * @param in_r    Right input sample.
 * @param out_l   Left output (delayed).
 * @param out_r   Right output (delayed).
 * @param driver  Driver index: 0 = low (woofer), 1 = high (tweeter).
 */
void delay_process_sample(int16_t in_l, int16_t in_r,
                          int16_t &out_l, int16_t &out_r,
                          uint8_t driver);

/**
 * @brief Reset delay line state (fill buffers with zeros, reset write index).
 *
 * Call when delay parameters change to avoid stale data causing transients.
 */
void delay_reset_state();
