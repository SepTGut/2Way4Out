/**
 * @file eq.h
 * @brief Public API for the 4-band parametric EQ (biquad IIR).
 *
 * The EQ is applied to the full-range stereo signal BEFORE the crossover.
 * Each band uses a standard biquad (second-order IIR) filter designed with
 * Robert Bristow-Johnson's "Audio EQ Cookbook" formulas.
 *
 * Supported band types (selected by frequency + gain combination):
 *   - Low-shelf  (first band default)
 *   - Peaking EQ (mid bands default)
 *   - High-shelf (last band default)
 *
 * Coefficients are pre-computed whenever a parameter changes, so the
 * per-sample inner loop is just a few multiply-accumulates.
 */

#pragma once

#include "dsp_config.h"
#include "globals.h"

/**
 * @brief Recompute biquad coefficients for a single EQ band.
 *
 * This function reads the band parameters from the global current_params
 * and writes the computed coefficients into the global eq_coeffs cache.
 * It should be called whenever the user changes an EQ parameter — NOT
 * per sample.
 *
 * @param band  EQ band index (0 … EQ_MAX_BANDS-1).
 */
void eq_compute_coeffs(uint8_t band);

/**
 * @brief Pre-compute all EQ band coefficients.
 *
 * Calls eq_compute_coeffs() for every band.  Typically called once per
 * parameter update cycle (from the DSP task when it detects a parameter
 * change, or from the UI task after ADC reads).
 */
void eq_update_all_coeffs();

/**
 * @brief Process one stereo sample through all active EQ bands.
 *
 * Applies each enabled EQ band in series (cascade).  The output of band N
 * feeds the input of band N+1.
 *
 * @param in_l   Left channel input sample (float, pre-DC-block range).
 * @param in_r   Right channel input sample.
 * @param out_l  Left channel output (overwritten in-place).
 * @param out_r  Right channel output (overwritten in-place).
 */
void eq_process_sample(float &in_l, float &in_r, float &out_l, float &out_r);

/**
 * @brief Reset all EQ filter state (z-delay elements).
 *
 * Call this when the EQ is first enabled or when the user requests a
 * "reset" to avoid stale filter state causing transients.
 */
void eq_reset_state();
