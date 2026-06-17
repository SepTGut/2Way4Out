/**
 * @file limiter.h
 * @brief Public API for the per-band brickwall limiter.
 *
 * The limiter is a specialised compressor with:
 *   - Very high ratio (100:1 ≈ ∞:1)
 *   - Hard knee
 *   - Fast attack (≤ 1 ms)
 *   - Configurable threshold (ceiling)
 *
 * It is applied AFTER the compressor in the per-driver chain.
 * The limiter uses the same comp_process_sample() code path but reads
 * from the limiter's dyn_params_t instead of the compressor's.
 */

#pragma once

#include "dsp_config.h"
#include "globals.h"

/**
 * @brief Process one stereo sample pair through the limiter for one driver.
 *
 * @param in_l    Left input sample (float).
 * @param in_r    Right input sample (float).
 * @param out_l   Left output sample (overwritten).
 * @param out_r   Right output sample (overwritten).
 * @param driver  Driver index: 0 = low (woofer), 1 = high (tweeter).
 */
void lim_process_sample(float in_l, float in_r, float &out_l, float &out_r,
                        uint8_t driver);

/**
 * @brief Hard-limit a sample pair to ±threshold (safety clipper).
 *
 * This is a last-resort brick-wall clipper that runs after all other
 * processing.  It guarantees the output never exceeds ±threshold,
 * preventing DAC overflow regardless of what the compressor/limiter
 * settings are.
 *
 * @param in_l     Left input.
 * @param in_r     Right input.
 * @param out_l    Left output.
 * @param out_r    Right output.
 * @param threshold  Absolute sample value ceiling (e.g., 32000 for -0.2 dBFS).
 */
void lim_hard_clip(float in_l, float in_r, float &out_l, float &out_r,
                   float threshold);
