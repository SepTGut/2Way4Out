/**
 * @file eq.cpp
 * @brief 4-band parametric EQ — biquad IIR implementation.
 *
 * Uses Robert Bristow-Johnson's "Audio EQ Cookbook" formulas:
 *   https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
 *
 * Each band uses Direct-Form-II Transposed structure:
 *   y[n] = b0·x[n] + w1
 *   w1   = b1·x[n] - a1·y[n] + w2
 *   w2   = b2·x[n] - a2·y[n]
 *
 * This form has better numerical precision and only needs 2 state
 * variables per band per channel.
 */

#include "eq.h"
#include <math.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────
// Internal helper: compute normalised biquad coefficients
// ─────────────────────────────────────────────────────────────

static void compute_biquad_coeffs(const eq_band_t &band, float (&c)[5])
{
    // If disabled → identity filter (pass-through).
    if (!band.enabled) {
        c[0] = 1.0f;  // b0
        c[1] = 0.0f;  // b1
        c[2] = 0.0f;  // b2
        c[3] = 0.0f;  // a1
        c[4] = 0.0f;  // a2
        return;
    }

    // Clamp frequency to valid range
    float freq = band.freq_hz;
    if (freq < 20.0f)   freq = 20.0f;
    if (freq > 20000.0f) freq = 20000.0f;

    float A  = powf(10.0f, band.gain_db / 40.0f);
    float w0 = 2.0f * M_PI * freq / (float)SAMPLE_RATE;
    float sin_w0 = sinf(w0);
    float cos_w0 = cosf(w0);
    float alpha = sin_w0 / (2.0f * band.q);

    float b0, b1, b2, a0, a1, a2;

    // Select filter type by frequency heuristic:
    //   < 200 Hz → low-shelf
    //   > 4000 Hz → high-shelf
    //   otherwise → peaking EQ
    if (freq < 200.0f) {
        // ── Low-shelf ──
        float sqrtA = sqrtf(A);
        b0 =        A * ( (A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtA * alpha);
        b1 =  2.0f * A * ( (A - 1.0f) - (A + 1.0f) * cos_w0);
        b2 =        A * ( (A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtA * alpha);
        a0 =              (A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtA * alpha;
        a1 = -2.0f       * ( (A - 1.0f) + (A + 1.0f) * cos_w0);
        a2 =              (A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtA * alpha;
    } else if (freq > 4000.0f) {
        // ── High-shelf ──
        float sqrtA = sqrtf(A);
        b0 =        A * ( (A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtA * alpha);
        b1 = -2.0f * A * ( (A - 1.0f) + (A + 1.0f) * cos_w0);
        b2 =        A * ( (A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtA * alpha);
        a0 =              (A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtA * alpha;
        a1 =  2.0f       * ( (A - 1.0f) - (A + 1.0f) * cos_w0);
        a2 =              (A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtA * alpha;
    } else {
        // ── Peaking EQ ──
        b0 =   1.0f + alpha * A;
        b1 =  -2.0f * cos_w0;
        b2 =   1.0f - alpha * A;
        a0 =   1.0f + alpha / A;
        a1 =  -2.0f * cos_w0;
        a2 =   1.0f - alpha / A;
    }

    // Normalise so a0 = 1
    float inv_a0 = 1.0f / a0;
    c[0] = b0 * inv_a0;
    c[1] = b1 * inv_a0;
    c[2] = b2 * inv_a0;
    c[3] = a1 * inv_a0;
    c[4] = a2 * inv_a0;
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

void eq_compute_coeffs(uint8_t band)
{
    if (band < EQ_MAX_BANDS) {
        // Use smoothed params for click-free coefficient updates
        compute_biquad_coeffs(smoothed_params.eq_bands[band], eq_coeffs[band]);
    }
}

void eq_update_all_coeffs()
{
    for (uint8_t i = 0; i < EQ_MAX_BANDS; i++) {
        eq_compute_coeffs(i);
    }
}

void eq_process_sample(float &in_l, float &in_r, float &out_l, float &out_r)
{
    out_l = in_l;
    out_r = in_r;

    for (uint8_t band = 0; band < EQ_MAX_BANDS; band++) {
        if (!current_params.eq_bands[band].enabled) continue;

        const float *c = eq_coeffs[band];

        // ── Left channel (ch=0) — Direct-Form-II Transposed ──
        float x_l = out_l;
        float y_l = c[0] * x_l + eq_state[band][0][0];
        eq_state[band][0][0] = c[1] * x_l - c[3] * y_l + eq_state[band][0][1];
        eq_state[band][0][1] = c[2] * x_l - c[4] * y_l;
        out_l = y_l;

        // ── Right channel (ch=1) — Direct-Form-II Transposed ──
        float x_r = out_r;
        float y_r = c[0] * x_r + eq_state[band][1][0];
        eq_state[band][1][0] = c[1] * x_r - c[3] * y_r + eq_state[band][1][1];
        eq_state[band][1][1] = c[2] * x_r - c[4] * y_r;
        out_r = y_r;
    }
}

void eq_reset_state()
{
    memset(eq_state, 0, sizeof(eq_state));
}
