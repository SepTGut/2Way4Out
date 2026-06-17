/**
 * @file dsp_core.cpp
 * @brief DSP crossover filter implementation with full effects chain.
 *
 * Signal flow per sample (per band):
 *   Raw Input
 *     → DC Block (high-pass at 20 Hz)
 *     → Parametric EQ ×4 bands (biquad IIR, full-range)
 *     → Crossover (1st-order IIR LPF 6 dB/oct; HPF = signal - LPF)
 *     → Low Band  → Compressor → Limiter → Delay → Gain → Clip → I2S1 (woofer)
 *     → High Band → Compressor → Limiter → Delay → Gain → Clip → I2S0 (tweeter)
 *
 * Features added in this version:
 *   - Parameter smoothing (linear interpolation, 10-20ms glide)
 *   - DC block filter (20 Hz high-pass)
 *   - 4-band parametric EQ (biquad IIR, before crossover)
 *   - Per-driver compressor (feed-forward, peak detector)
 *   - Per-driver brickwall limiter (infinite ratio)
 *   - Per-driver delay line (time-alignment)
 *   - RMS VU meter calculation
 *   - Mute/Bypass support
 */

#include "dsp_core.h"
#include "pins.h"
#include "eq.h"
#include "compressor.h"
#include "limiter.h"
#include "delay.h"
#include <Arduino.h>

#define PARAM_SMOOTHING_FACTOR 0.02f
#define DC_BLOCK_CUTOFF 20.0f

static dsp_params_t smoothed_params = dsp_params_default;
static float dc_block_state[2] = {0.0f, 0.0f};
static dsp_params_t prev_params = dsp_params_default;  // For change detection

void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1)
{
    if (cutoff_hz < 20.0f)    cutoff_hz = 20.0f;
    if (cutoff_hz > 20000.0f) cutoff_hz = 20000.0f;

    float rc    = 1.0f / (2.0f * M_PI * cutoff_hz);
    float dt    = 1.0f / sample_rate;
    float alpha = dt / (rc + dt);

    if (alpha <= 0.0f) alpha = 0.0001f;
    if (alpha >= 1.0f) alpha = 0.9999f;

    *a0 = alpha;
    *b1 = 1.0f - alpha;
}

void dsp_task(void *)
{
    const size_t frames_per_batch = FRAMES_PER_BATCH;
    const size_t bytes_per_batch  = frames_per_batch * 2 * sizeof(int16_t);

    int16_t *raw_buf  = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);
    int16_t *low_buf  = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);
    int16_t *high_buf = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);

    if (!raw_buf || !low_buf || !high_buf) {
        Serial.println("DSP: buffer allocation failed!");
        vTaskDelete(NULL);
        return;
    }

    // Pre-compute DC block coefficient
    float dc_rc = 1.0f / (2.0f * M_PI * DC_BLOCK_CUTOFF);
    float dc_dt = 1.0f / (float)SAMPLE_RATE;
    float dc_alpha = dc_dt / (dc_rc + dc_dt);

    // Allocate delay line buffers
    delay_alloc_buffers();

    // Initialize EQ coefficients
    eq_update_all_coeffs();
    eq_reset_state();

    // Initialize compressor/limiter state
    comp_reset_state();

    Serial.println("DSP task started on Core " + String(xPortGetCoreID()));

    while (1) {
        size_t item_size = 0;
        void *received_ptr = xRingbufferReceiveUpTo(audio_rb, &item_size,
                                                     pdMS_TO_TICKS(100),
                                                     bytes_per_batch);

        if (received_ptr == NULL) {
            // No audio data — output silence
            memset(low_buf,  0, bytes_per_batch);
            memset(high_buf, 0, bytes_per_batch);
            size_t written = 0;
            i2s_write(I2S1_NUM, low_buf,  bytes_per_batch, &written, pdMS_TO_TICKS(20));
            i2s_write(I2S0_NUM, high_buf, bytes_per_batch, &written, pdMS_TO_TICKS(20));
            global_rms_level = 0.0f;
            continue;
        }

        size_t received = item_size;
        size_t copy_len = min(received, bytes_per_batch);
        memcpy(raw_buf, received_ptr, copy_len);
        vRingbufferReturnItem(audio_rb, received_ptr);

        size_t actual_frames = copy_len / (2 * sizeof(int16_t));
        if (actual_frames == 0) continue;

        // ── Read target parameters from shared state ──
        dsp_params_t target_params;
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            target_params = current_params;
            xSemaphoreGive(dsp_params_mutex);
        } else {
            target_params = dsp_params_default;
        }

        // ── Parameter smoothing (glide over 10-20 ms) ──
        smoothed_params.master_volume += (target_params.master_volume - smoothed_params.master_volume) * PARAM_SMOOTHING_FACTOR;
        smoothed_params.crossover_hz  += (target_params.crossover_hz  - smoothed_params.crossover_hz)  * PARAM_SMOOTHING_FACTOR;
        smoothed_params.low_gain     += (target_params.low_gain     - smoothed_params.low_gain)     * PARAM_SMOOTHING_FACTOR;
        smoothed_params.high_gain     += (target_params.high_gain     - smoothed_params.high_gain)     * PARAM_SMOOTHING_FACTOR;
        smoothed_params.mute          = target_params.mute;
        smoothed_params.bypass        = target_params.bypass;

        // Smooth EQ band parameters
        for (uint8_t i = 0; i < EQ_MAX_BANDS; i++) {
            smoothed_params.eq_bands[i].enabled = target_params.eq_bands[i].enabled;
            smoothed_params.eq_bands[i].freq_hz += (target_params.eq_bands[i].freq_hz - smoothed_params.eq_bands[i].freq_hz) * PARAM_SMOOTHING_FACTOR;
            smoothed_params.eq_bands[i].gain_db += (target_params.eq_bands[i].gain_db - smoothed_params.eq_bands[i].gain_db) * PARAM_SMOOTHING_FACTOR;
            smoothed_params.eq_bands[i].q       += (target_params.eq_bands[i].q       - smoothed_params.eq_bands[i].q)       * PARAM_SMOOTHING_FACTOR;
        }

        // Smooth per-driver dynamics + delay
        for (uint8_t drv = 0; drv < DRIVER_BANDS; drv++) {
            const driver_params_t *src = (drv == 0) ? &target_params.low_driver  : &target_params.high_driver;
            driver_params_t       *dst = (drv == 0) ? &smoothed_params.low_driver : &smoothed_params.high_driver;
            const driver_params_t *prev = (drv == 0) ? &prev_params.low_driver  : &prev_params.high_driver;

            // Smooth compressor params
            dst->compressor.enabled      = src->compressor.enabled;
            dst->compressor.threshold_db += (src->compressor.threshold_db - dst->compressor.threshold_db) * PARAM_SMOOTHING_FACTOR;
            dst->compressor.ratio        += (src->compressor.ratio        - dst->compressor.ratio)        * PARAM_SMOOTHING_FACTOR;
            dst->compressor.attack_ms    += (src->compressor.attack_ms    - dst->compressor.attack_ms)    * PARAM_SMOOTHING_FACTOR;
            dst->compressor.release_ms   += (src->compressor.release_ms   - dst->compressor.release_ms)   * PARAM_SMOOTHING_FACTOR;
            dst->compressor.makeup_db    += (src->compressor.makeup_db    - dst->compressor.makeup_db)    * PARAM_SMOOTHING_FACTOR;

            // Smooth limiter params
            dst->limiter.enabled         = src->limiter.enabled;
            dst->limiter.threshold_db    += (src->limiter.threshold_db    - dst->limiter.threshold_db)    * PARAM_SMOOTHING_FACTOR;

            // Delay (no smoothing needed, but handle on/off transitions)
            if (src->delay.samples != prev->delay.samples) {
                dst->delay.samples = src->delay.samples;
            }
            dst->delay.enabled = src->delay.enabled;
        }

        // ── Detect EQ coefficient changes ──
        bool eq_changed = false;
        for (uint8_t i = 0; i < EQ_MAX_BANDS; i++) {
            if (target_params.eq_bands[i].enabled   != prev_params.eq_bands[i].enabled ||
                fabsf(target_params.eq_bands[i].freq_hz - prev_params.eq_bands[i].freq_hz) > 1.0f ||
                fabsf(target_params.eq_bands[i].gain_db - prev_params.eq_bands[i].gain_db) > 0.1f ||
                fabsf(target_params.eq_bands[i].q       - prev_params.eq_bands[i].q)       > 0.01f) {
                eq_changed = true;
            }
        }
        if (eq_changed) {
            eq_update_all_coeffs();
        }
        prev_params = target_params;

        // ── Compute crossover coefficients ──
        float a0, b1;
        compute_coeffs(smoothed_params.crossover_hz, (float)SAMPLE_RATE, &a0, &b1);

        // ── Persistent filter state ──
        float l_low_prev  = filt_state[0];
        float r_low_prev  = filt_state[1];
        float l_high_prev = filt_state[2];
        float r_high_prev = filt_state[3];

        int16_t *p_raw  = raw_buf;
        int16_t *p_low  = low_buf;
        int16_t *p_high = high_buf;

        // RMS Calculation variables
        double sum_sq = 0;

        for (size_t i = 0; i < actual_frames; i++) {
            float l_raw = (float)(*p_raw++);
            float r_raw = (float)(*p_raw++);

            // ── RMS tracking (input level) ──
            sum_sq += (double)l_raw * l_raw;
            sum_sq += (double)r_raw * r_raw;

            // ── Bypass mode: send raw signal to both outputs ──
            if (smoothed_params.bypass) {
                *p_low++  = (int16_t)l_raw;
                *p_low++  = (int16_t)r_raw;
                *p_high++ = (int16_t)l_raw;
                *p_high++ = (int16_t)r_raw;
                continue;
            }

            // ── DC Block ──
            float l_dc_out = dc_alpha * l_raw + (1.0f - dc_alpha) * dc_block_state[0];
            float r_dc_out = dc_alpha * r_raw + (1.0f - dc_alpha) * dc_block_state[1];
            dc_block_state[0] = l_dc_out;
            dc_block_state[1] = r_dc_out;
            float l_dcb = l_raw - l_dc_out;
            float r_dcb = r_raw - r_dc_out;

            // ── Parametric EQ (full-range, before crossover) ──
            float l_eq = l_dcb, r_eq = r_dcb;
            eq_process_sample(l_dcb, r_dcb, l_eq, r_eq);

            // ── Crossover ──
            float l_low = a0 * l_eq + b1 * l_low_prev;
            float r_low = a0 * r_eq + b1 * r_low_prev;
            float l_high = l_eq - l_low;
            float r_high = r_eq - r_low;

            l_low_prev = l_low;
            r_low_prev = r_low;
            l_high_prev = l_high;
            r_high_prev = r_high;

            // ── Per-driver dynamics chain: Compressor → Limiter → Delay → Gain ──

            // Low band (driver 0 = woofer)
            float ll = l_low, rl = r_low;
            comp_process_sample(ll, rl, ll, rl, 0);   // Compressor
            lim_process_sample(ll, rl, ll, rl, 0);    // Limiter
            int16_t ll_d, rl_d;
            delay_process_sample((int16_t)ll, (int16_t)rl, ll_d, rl_d, 0);  // Delay
            ll = (float)ll_d * smoothed_params.low_gain * smoothed_params.master_volume;
            rl = (float)rl_d * smoothed_params.low_gain * smoothed_params.master_volume;

            // High band (driver 1 = tweeter)
            float lh = l_high, rh = r_high;
            comp_process_sample(lh, rh, lh, rh, 1);   // Compressor
            lim_process_sample(lh, rh, lh, rh, 1);    // Limiter
            int16_t lh_d, rh_d;
            delay_process_sample((int16_t)lh, (int16_t)rh, lh_d, rh_d, 1);  // Delay
            lh = (float)lh_d * smoothed_params.high_gain * smoothed_params.master_volume;
            rh = (float)rh_d * smoothed_params.high_gain * smoothed_params.master_volume;

            // ── Mute ──
            if (smoothed_params.mute) {
                ll = rl = lh = rh = 0.0f;
            }

            // ── Final hard clip (safety limiter) ──
            ll = fmaxf(-32768.0f, fminf(32767.0f, ll));
            rl = fmaxf(-32768.0f, fminf(32767.0f, rl));
            lh = fmaxf(-32768.0f, fminf(32767.0f, lh));
            rh = fmaxf(-32768.0f, fminf(32767.0f, rh));

            *p_low++  = (int16_t)ll;
            *p_low++  = (int16_t)rl;
            *p_high++ = (int16_t)lh;
            *p_high++ = (int16_t)rh;
        }

        // ── Calculate RMS for this batch ──
        global_rms_level = sqrtf((float)(sum_sq / (actual_frames * 2)));

        // ── Save filter state ──
        filt_state[0] = l_low_prev;
        filt_state[1] = r_low_prev;
        filt_state[2] = l_high_prev;
        filt_state[3] = r_high_prev;

        // ── Write to I2S DACs ──
        size_t write_len = actual_frames * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_write(I2S1_NUM, low_buf,  write_len, &written, pdMS_TO_TICKS(50));
        i2s_write(I2S0_NUM, high_buf, write_len, &written, pdMS_TO_TICKS(50));
    }
}
