/**
 * @file dsp_core.cpp
 * @brief DSP crossover filter implementation with Smoothing, Stability, DC Blocking and RMS calculation.
 */

#include "dsp_core.h"
#include "pins.h"
#include <Arduino.h>

#define PARAM_SMOOTHING_FACTOR 0.02f
#define DC_BLOCK_CUTOFF 20.0f

static dsp_params_t smoothed_params = dsp_params_default;
static float dc_block_state[2] = {0.0f, 0.0f};

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

    float dc_rc = 1.0f / (2.0f * M_PI * DC_BLOCK_CUTOFF);
    float dc_dt = 1.0f / (float)SAMPLE_RATE;
    float dc_alpha = dc_dt / (dc_rc + dc_dt);

    Serial.println("DSP task started on Core " + String(xPortGetCoreID()));

    while (1) {
        size_t item_size = 0;
        void *received_ptr = xRingbufferReceiveUpTo(audio_rb, &item_size,
                                                     pdMS_TO_TICKS(100),
                                                     bytes_per_batch);

        if (received_ptr == NULL) {
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

        dsp_params_t target_params;
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            target_params = current_params;
            xSemaphoreGive(dsp_params_mutex);
        } else {
            target_params = dsp_params_default;
        }

        smoothed_params.master_volume += (target_params.master_volume - smoothed_params.master_volume) * PARAM_SMOOTHING_FACTOR;
        smoothed_params.crossover_hz  += (target_params.crossover_hz  - smoothed_params.crossover_hz)  * PARAM_SMOOTHING_FACTOR;
        smoothed_params.low_gain     += (target_params.low_gain     - smoothed_params.low_gain)     * PARAM_SMOOTHING_FACTOR;
        smoothed_params.high_gain     += (target_params.high_gain     - smoothed_params.high_gain)     * PARAM_SMOOTHING_FACTOR;

        float a0, b1;
        compute_coeffs(smoothed_params.crossover_hz, (float)SAMPLE_RATE, &a0, &b1);

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
            
            // RMS tracking (Input level)
            sum_sq += (double)l_raw * l_raw;
            sum_sq += (double)r_raw * r_raw;

            // DC Block
            float l_low_dc = dc_alpha * l_raw + (1.0f - dc_alpha) * dc_block_state[0];
            float r_low_dc = dc_alpha * r_raw + (1.0f - dc_alpha) * dc_block_state[1];
            dc_block_state[0] = l_low_dc;
            dc_block_state[1] = r_low_dc;
            float l = l_raw - l_low_dc;
            float r = r_raw - r_low_dc;

            // Crossover
            float l_low = a0 * l + b1 * l_low_prev;
            float r_low = a0 * r + b1 * r_low_prev;
            float l_high = l - l_low;
            float r_high = r - r_low;

            l_low_prev = l_low; r_low_prev = r_low;
            l_high_prev = l_high; r_high_prev = r_high;

            float ll = l_low  * smoothed_params.low_gain  * smoothed_params.master_volume;
            float rl = r_low  * smoothed_params.low_gain  * smoothed_params.master_volume;
            float lh = l_high * smoothed_params.high_gain * smoothed_params.master_volume;
            float rh = r_high * smoothed_params.high_gain * smoothed_params.master_volume;

            ll = fmaxf(-32768.0f, fminf(32767.0f, ll));
            rl = fmaxf(-32768.0f, fminf(32767.0f, rl));
            lh = fmaxf(-32768.0f, fminf(32767.0f, lh));
            rh = fmaxf(-32768.0f, fminf(32767.0f, rh));

            *p_low++  = (int16_t)ll;
            *p_low++  = (int16_t)rl;
            *p_high++ = (int16_t)lh;
            *p_high++ = (int16_t)rh;
        }

        // Calculate RMS for this batch (averaged over L+R)
        global_rms_level = sqrtf((float)(sum_sq / (actual_frames * 2)));

        filt_state[0] = l_low_prev; filt_state[1] = r_low_prev;
        filt_state[2] = l_high_prev; filt_state[3] = r_high_prev;

        size_t write_len = actual_frames * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_write(I2S1_NUM, low_buf,  write_len, &written, pdMS_TO_TICKS(50));
        i2s_write(I2S0_NUM, high_buf, write_len, &written, pdMS_TO_TICKS(50));
    }
}

