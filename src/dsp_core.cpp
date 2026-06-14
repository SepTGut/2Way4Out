#include "dsp_core.h"
#include "pins.h"
#include <Arduino.h>

// ── Filter coefficients ────────────────────────────────
void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1)
{
    if (cutoff_hz < 10.0f)    cutoff_hz = 10.0f;
    if (cutoff_hz > 20000.0f) cutoff_hz = 20000.0f;

    float rc    = 1.0f / (2.0f * M_PI * cutoff_hz);
    float dt    = 1.0f / sample_rate;
    float alpha = dt / (rc + dt);
    *a0 = alpha;
    *b1 = 1.0f - alpha;
}

// ── DSP task (runs on Core 1) ──────────────────────────
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

    Serial.println("DSP task started on Core " + String(xPortGetCoreID()));

    while (1) {
        size_t item_size = 0;
        void *received_ptr = xRingbufferReceiveUpTo(audio_rb, &item_size,
                                                     pdMS_TO_TICKS(100),
                                                     bytes_per_batch);
        if (received_ptr == NULL) {
            // No data — send silence to keep DACs active
            memset(low_buf,  0, bytes_per_batch);
            memset(high_buf, 0, bytes_per_batch);
            size_t written = 0;
            i2s_write(I2S1_NUM, low_buf,  bytes_per_batch, &written, pdMS_TO_TICKS(20));
            i2s_write(I2S0_NUM, high_buf, bytes_per_batch, &written, pdMS_TO_TICKS(20));
            continue;
        }

        size_t received = item_size;
        size_t copy_len = min(received, bytes_per_batch);
        memcpy(raw_buf, received_ptr, copy_len);
        vRingbufferReturnItem(audio_rb, received_ptr);

        size_t actual_frames = copy_len / (2 * sizeof(int16_t));
        if (actual_frames == 0) continue;

        // Grab current DSP params
        dsp_params_t params;
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            params = current_params;
            xSemaphoreGive(dsp_params_mutex);
        } else {
            params = dsp_params_default;
        }

        // Compute crossover coefficients
        float a0, b1;
        compute_coeffs(params.crossover_hz, (float)SAMPLE_RATE, &a0, &b1);

        // Run crossover filter
        float l_low_prev  = filt_state[0];
        float r_low_prev  = filt_state[1];
        float l_high_prev = filt_state[2];
        float r_high_prev = filt_state[3];

        for (size_t i = 0; i < actual_frames; i++) {
            float l = (float)raw_buf[2*i];
            float r = (float)raw_buf[2*i + 1];

            // First-order low-pass
            float l_low = a0 * l + b1 * l_low_prev;
            float r_low = a0 * r + b1 * r_low_prev;
            // High-pass = original - low-pass
            float l_high = l - l_low;
            float r_high = r - r_low;

            l_low_prev  = l_low;
            r_low_prev  = r_low;
            l_high_prev = l_high;
            r_high_prev = r_high;

            // Apply gain + master volume
            float ll = l_low  * params.low_gain  * params.master_volume;
            float rl = r_low  * params.low_gain  * params.master_volume;
            float lh = l_high * params.high_gain * params.master_volume;
            float rh = r_high * params.high_gain * params.master_volume;

            // Clip to 16-bit range
            ll = fmaxf(-32768.0f, fminf(32767.0f, ll));
            rl = fmaxf(-32768.0f, fminf(32767.0f, rl));
            lh = fmaxf(-32768.0f, fminf(32767.0f, lh));
            rh = fmaxf(-32768.0f, fminf(32767.0f, rh));

            low_buf[2*i]     = (int16_t)ll;
            low_buf[2*i + 1] = (int16_t)rl;
            high_buf[2*i]    = (int16_t)lh;
            high_buf[2*i + 1]= (int16_t)rh;
        }

        // Save filter states
        filt_state[0] = l_low_prev;
        filt_state[1] = r_low_prev;
        filt_state[2] = l_high_prev;
        filt_state[3] = r_high_prev;

        // Write to both DACs
        size_t write_len = actual_frames * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_write(I2S1_NUM, low_buf,  write_len, &written, pdMS_TO_TICKS(50));
        i2s_write(I2S0_NUM, high_buf, write_len, &written, pdMS_TO_TICKS(50));
    }
}
