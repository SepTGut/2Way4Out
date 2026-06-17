/**
 * @file compressor.cpp
 * @brief Per-band compressor / limiter implementation.
 *
 * Feed-forward design:
 *   1. Compute peak level of L+R input.
 *   2. Convert to dBFS.
 *   3. Apply compression curve (threshold, ratio, hard knee).
 *   4. Smooth the gain with attack/release time constants.
 *   5. Apply gain to both channels.
 *
 * The same code handles both compressor and limiter — the only
 * difference is the ratio parameter (2:1 for compressor, 100:1 for
 * limiter) and the threshold.
 *
 * State is stored in the global comp_env[] and comp_gain[] arrays.
 */

#include "compressor.h"
#include <math.h>

// ─────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────

/**
 * @brief Convert linear amplitude to dBFS (relative to full-scale 32768).
 */
static inline float linear_to_db(float x)
{
    if (x <= 0.0f) return -120.0f;
    return 20.0f * log10f(x / 32768.0f);
}

/**
 * @brief Convert dBFS to linear gain.
 */
static inline float db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

/**
 * @brief Compute the gain reduction in dB for a given input level.
 *
 * Compression curve (hard knee):
 *   If level_dB < threshold → no reduction (0 dB)
 *   If level_dB >= threshold → reduction = (level - threshold) × (1 - 1/ratio)
 *
 * For a limiter (ratio = 100), this effectively caps the output at
 * the threshold.
 */
static inline float compute_gain_reduction_db(float level_db,
                                               float threshold_db,
                                               float ratio)
{
    if (level_db <= threshold_db) return 0.0f;
    return (level_db - threshold_db) * (1.0f - 1.0f / ratio);
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

void comp_process_sample(float in_l, float in_r, float &out_l, float &out_r,
                         uint8_t driver)
{
    if (driver >= DRIVER_BANDS) {
        out_l = in_l;
        out_r = in_r;
        return;
    }

    // Read the compressor parameters for this driver
    const dyn_params_t &params =
        (&current_params.low_driver)[driver].compressor;

    if (!params.enabled) {
        out_l = in_l;
        out_r = in_r;
        return;
    }

    // ── 1. Peak detection (use absolute max of L, R) ──
    float abs_l = fabsf(in_l);
    float abs_r = fabsf(in_r);
    float peak = (abs_l > abs_r) ? abs_l : abs_r;

    // ── 2. Convert to dBFS ──
    float peak_db = linear_to_db(peak);

    // ── 3. Compute desired gain reduction ──
    float gr_db = compute_gain_reduction_db(peak_db,
                                             params.threshold_db,
                                             params.ratio);

    // ── 4. Convert target gain reduction to linear gain ──
    float target_gain = db_to_linear(-gr_db + params.makeup_db);

    // ── 5. Smooth gain with attack / release envelope ──
    // Attack: fast response when gain is decreasing (compression kicking in)
    // Release: slow recovery when gain is increasing (compression releasing)
    float coeff;
    if (target_gain < comp_gain[driver][0]) {
        // Gain is decreasing → use attack time
        coeff = expf(-1.0f / (params.attack_ms * (float)SAMPLE_RATE / 1000.0f));
    } else {
        // Gain is increasing → use release time
        coeff = expf(-1.0f / (params.release_ms * (float)SAMPLE_RATE / 1000.0f));
    }

    // Smooth per-channel (both channels share the same envelope)
    comp_gain[driver][0] = coeff * comp_gain[driver][0] + (1.0f - coeff) * target_gain;
    comp_gain[driver][1] = comp_gain[driver][0];  // Link L and R

    // ── 6. Apply gain ──
    out_l = in_l * comp_gain[driver][0];
    out_r = in_r * comp_gain[driver][1];
}

void comp_reset_state()
{
    memset(comp_env, 0, sizeof(comp_env));
    comp_gain[0][0] = 1.0f; comp_gain[0][1] = 1.0f;
    comp_gain[1][0] = 1.0f; comp_gain[1][1] = 1.0f;
}
