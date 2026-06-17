/**
 * @file compressor.h
 * @brief Public API for the per-band compressor / limiter.
 *
 * Feed-forward design with peak detector and smooth gain envelope.
 * The same code serves as both compressor (configurable ratio) and
 * limiter (ratio = 100:1, hard knee).
 *
 * The compressor is applied AFTER the crossover, per driver band.
 */

#pragma once

#include "dsp_config.h"
#include "globals.h"

/**
 * @brief Process one stereo sample pair through the compressor for one driver.
 *
 * @param in_l    Left input sample (float).
 * @param in_r    Right input sample (float).
 * @param out_l   Left output sample (overwritten).
 * @param out_r   Right output sample (overwritten).
 * @param driver  Driver index: 0 = low (woofer), 1 = high (tweeter).
 */
void comp_process_sample(float in_l, float in_r, float &out_l, float &out_r,
                         uint8_t driver);

/**
 * @brief Reset compressor envelope and gain state for both drivers.
 *
 * Call when the compressor is first enabled or after a long silence
 * to avoid stale envelope values.
 */
void comp_reset_state();
