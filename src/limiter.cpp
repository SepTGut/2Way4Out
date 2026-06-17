/**
 * @file limiter.cpp
 * @brief Per-band brickwall limiter implementation.
 *
 * Uses the same feed-forward compressor algorithm but reads from the
 * limiter's dyn_params_t (very high ratio, fast attack).
 * Also provides a hard-clip safety function as the final output stage.
 */

#include "limiter.h"
#include <math.h>

// ─────────────────────────────────────────────────────────────
// Internal helpers (same math as compressor)
// ─────────────────────────────────────────────────────────────

static inline float linear_to_db(float x)
{
    if (x <= 0.0f) return -120.0f;
    return 20.0f * log10f(x / 32768.0f);
}

static inline float db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

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

void lim_process_sample(float in_l, float in_r, float &out_l, float &out_r,
                        uint8_t driver)
{
    if (driver >= DRIVER_BANDS) {
        out_l = in_l;
        out_r = in_r;
        return;
    }

    const dyn_params_t &params =
        (&current_params.low_driver)[driver].limiter;

    if (!params.enabled) {
        out_l = in_l;
        out_r = in_r;
        return;
    }

    // ── Peak detection ──
    float abs_l = fabsf(in_l);
    float abs_r = fabsf(in_r);
    float peak = (abs_l > abs_r) ? abs_l : abs_r;

    // ── dBFS conversion ──
    float peak_db = linear_to_db(peak);

    // ── Gain reduction ──
    float gr_db = compute_gain_reduction_db(peak_db,
                                             params.threshold_db,
                                             params.ratio);
    float target_gain = db_to_linear(-gr_db + params.makeup_db);

    // ── Attack / release smoothing ──
    // Use the dedicated limiter gain state (separate from compressor).
    float *gain_state = lim_gain[driver];

    float coeff;
    if (target_gain < gain_state[0]) {
        coeff = expf(-1.0f / (params.attack_ms * (float)SAMPLE_RATE / 1000.0f));
    } else {
        coeff = expf(-1.0f / (params.release_ms * (float)SAMPLE_RATE / 1000.0f));
    }

    gain_state[0] = coeff * gain_state[0] + (1.0f - coeff) * target_gain;
    gain_state[1] = gain_state[0];

    // ── Apply gain ──
    out_l = in_l * gain_state[0];
    out_r = in_r * gain_state[1];
}

void lim_hard_clip(float in_l, float in_r, float &out_l, float &out_r,
                   float threshold)
{
    // Hard clip to ±threshold
    out_l = fmaxf(-threshold, fminf(threshold, in_l));
    out_r = fmaxf(-threshold, fminf(threshold, in_r));
}
