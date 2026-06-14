/**
 * @file dsp_core.cpp
 * @brief DSP crossover filter implementation — the heart of the 2-way
 *        active crossover.
 *
 * This module runs as a high-priority FreeRTOS task on Core 1.
 * It continuously pulls audio from the ring buffer, splits it into
 * low and high frequency bands, applies gain and volume, and sends
 * the results to the two CS4344 DACs via I2S.
 *
 * ── Filter design ──────────────────────────────────────────
 *
 * The crossover uses a first-order IIR low-pass filter (6 dB/octave):
 *
 *   y[n] = alpha · x[n] + (1 - alpha) · y[n-1]
 *
 * where:
 *   alpha = dt / (RC + dt)
 *   RC    = 1 / (2π · fc)
 *   dt    = 1 / sample_rate
 *
 * The high-pass output is derived by subtraction (complementary filter):
 *   high[n] = x[n] - low[n]
 *
 * This approach is simple, low-CPU, and guaranteed to sum to unity
 * (low + high = original), which means the combined output of both
 * drivers recreates the full-range signal.
 *
 * ── DMA buffers ────────────────────────────────────────────
 *
 * Three DMA-capable heap buffers are allocated once at startup:
 *   raw_buf[]  : incoming stereo samples from the ring buffer
 *   low_buf[]  : filtered low-band output → I2S1 (woofer)
 *   high_buf[] : filtered high-band output → I2S0 (tweeter)
 *
 * DMA-capable memory is required because the ESP32 I2S peripheral
 * uses DMA to transfer data directly from RAM without CPU involvement.
 */

#include "dsp_core.h"
#include "pins.h"
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
// Filter coefficient calculation
// ─────────────────────────────────────────────────────────────

void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1)
{
    // Clamp cutoff to prevent filter instability.
    // Below 10 Hz the filter would heavily attenuate the entire audio band.
    // Above 20000 Hz the filter would pass almost everything (no crossover effect).
    if (cutoff_hz < 10.0f)    cutoff_hz = 10.0f;
    if (cutoff_hz > 20000.0f) cutoff_hz = 20000.0f;

    // RC time constant of the equivalent analog filter
    float rc    = 1.0f / (2.0f * M_PI * cutoff_hz);

    // Discretisation time step
    float dt    = 1.0f / sample_rate;

    // IIR coefficient (smoothing factor)
    float alpha = dt / (rc + dt);

    *a0 = alpha;        // feedforward: how much of the new sample passes through
    *b1 = 1.0f - alpha; // feedback: how much of the previous output is retained
}

// ─────────────────────────────────────────────────────────────
// DSP task (runs on Core 1)
// ─────────────────────────────────────────────────────────────

void dsp_task(void *)
{
    // ── Allocate DMA-capable audio buffers ───────────────────
    // MALLOC_CAP_DMA ensures the memory is accessible by the DMA controller.
    // Three buffers: raw input, low-band output, high-band output.
    // Each buffer holds FRAMES_PER_BATCH stereo frames (left + right, 16-bit each).
    const size_t frames_per_batch = FRAMES_PER_BATCH;
    const size_t bytes_per_batch  = frames_per_batch * 2 * sizeof(int16_t);

    int16_t *raw_buf  = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);
    int16_t *low_buf  = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);
    int16_t *high_buf = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);

    if (!raw_buf || !low_buf || !high_buf) {
        Serial.println("DSP: buffer allocation failed!");
        vTaskDelete(NULL);  // terminate this task — nothing we can do without buffers
        return;
    }

    Serial.println("DSP task started on Core " + String(xPortGetCoreID()));

    // ── Main processing loop ────────────────────────────────
    while (1) {
        // ── Read audio from the ring buffer ──────────────────
        // Blocks for up to 100 ms waiting for data.  If no data arrives
        // (Bluetooth not streaming), we write silence to the DACs.
        size_t item_size = 0;
        void *received_ptr = xRingbufferReceiveUpTo(audio_rb, &item_size,
                                                     pdMS_TO_TICKS(100),
                                                     bytes_per_batch);

        if (received_ptr == NULL) {
            // No audio data available — output silence to keep DACs active.
            // This prevents the CS4344 from entering an undefined state
            // that could cause a pop when audio resumes.
            memset(low_buf,  0, bytes_per_batch);
            memset(high_buf, 0, bytes_per_batch);
            size_t written = 0;
            i2s_write(I2S1_NUM, low_buf,  bytes_per_batch, &written, pdMS_TO_TICKS(20));
            i2s_write(I2S0_NUM, high_buf, bytes_per_batch, &written, pdMS_TO_TICKS(20));
            continue;
        }

        // Copy received data into our DMA buffer.  The ring buffer item
        // may be smaller than a full batch (last chunk of a packet), so
        // we handle partial batches gracefully.
        size_t received = item_size;
        size_t copy_len = min(received, bytes_per_batch);
        memcpy(raw_buf, received_ptr, copy_len);
        vRingbufferReturnItem(audio_rb, received_ptr);  // return item to ring buffer

        size_t actual_frames = copy_len / (2 * sizeof(int16_t));
        if (actual_frames == 0) continue;  // empty chunk, skip processing

        // ── Read current DSP parameters ──────────────────────
        // Acquire the mutex to safely read current_params.
        // If the mutex is held by the UI task (rare, short critical section),
        // we wait up to 10 ms before falling back to defaults.
        dsp_params_t params;
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            params = current_params;
            xSemaphoreGive(dsp_params_mutex);
        } else {
            // Mutex timeout — use safe defaults rather than risking audio glitch.
            params = dsp_params_default;
        }

        // ── Compute crossover filter coefficients ────────────
        // The coefficients depend only on the crossover frequency and
        // sample rate, so we compute them once per batch (not per sample).
        float a0, b1;
        compute_coeffs(params.crossover_hz, (float)SAMPLE_RATE, &a0, &b1);

        // ── Apply crossover filter to each stereo frame ──────
        // Load persistent filter state from the previous batch.
        // These are updated at the end of each batch to maintain continuity.
        float l_low_prev  = filt_state[0];
        float r_low_prev  = filt_state[1];
        float l_high_prev = filt_state[2];
        float r_high_prev = filt_state[3];

        for (size_t i = 0; i < actual_frames; i++) {
            // De-interleave stereo samples (left = even, right = odd)
            float l = (float)raw_buf[2*i];
            float r = (float)raw_buf[2*i + 1];

            // First-order IIR low-pass filter
            // y[n] = alpha · x[n] + (1 - alpha) · y[n-1]
            float l_low = a0 * l + b1 * l_low_prev;
            float r_low = a0 * r + b1 * r_low_prev;

            // High-pass = original - low-pass (complementary filter)
            // This guarantees: low + high = original (unity sum)
            float l_high = l - l_low;
            float r_high = r - r_low;

            // Shift state for next sample
            l_low_prev  = l_low;
            r_low_prev  = r_low;
            l_high_prev = l_high;
            r_high_prev = r_high;

            // ── Apply gain and master volume ──────────────────
            // Per-band gain allows balancing woofer vs tweeter output.
            // Master volume is a global level control.
            float ll = l_low  * params.low_gain  * params.master_volume;
            float rl = r_low  * params.low_gain  * params.master_volume;
            float lh = l_high * params.high_gain * params.master_volume;
            float rh = r_high * params.high_gain * params.master_volume;

            // ── Clip to 16-bit signed range ───────────────────
            // Without clipping, the float values could exceed the DAC's
            // input range, causing harsh digital distortion.
            ll = fmaxf(-32768.0f, fminf(32767.0f, ll));
            rl = fmaxf(-32768.0f, fminf(32767.0f, rl));
            lh = fmaxf(-32768.0f, fminf(32767.0f, lh));
            rh = fmaxf(-32768.0f, fminf(32767.0f, rh));

            // Interleave back into output buffers
            low_buf[2*i]     = (int16_t)ll;
            low_buf[2*i + 1] = (int16_t)rl;
            high_buf[2*i]    = (int16_t)lh;
            high_buf[2*i + 1]= (int16_t)rh;
        }

        // ── Save filter state for next batch ─────────────────
        // The IIR filter is recursive — each sample depends on the
        // previous output.  We must preserve state across batches to
        // maintain filter continuity and prevent clicks/pops.
        filt_state[0] = l_low_prev;
        filt_state[1] = r_low_prev;
        filt_state[2] = l_high_prev;
        filt_state[3] = r_high_prev;

        // ── Write filtered audio to both DACs ───────────────
        // I2S1 = low-band DAC (woofer amplifier)
        // I2S0 = high-band DAC (tweeter amplifier)
        // Both writes use the same length (actual_frames worth of data).
        size_t write_len = actual_frames * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_write(I2S1_NUM, low_buf,  write_len, &written, pdMS_TO_TICKS(50));
        i2s_write(I2S0_NUM, high_buf, write_len, &written, pdMS_TO_TICKS(50));
    }
}
