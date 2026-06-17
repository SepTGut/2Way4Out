/**
 * @file delay.cpp
 * @brief Per-band delay line implementation.
 *
 * Circular buffer approach:
 *   - Write current sample at write_idx.
 *   - Read delayed sample at (write_idx - delay_samples) mod buffer_size.
 *   - Increment write_idx.
 *
 * One buffer per channel per driver (4 total).
 * Buffer size = DELAY_MAX_SAMPLES + 1 (extra sample for read/write separation).
 */

#include "delay.h"
#include <string.h>

// ─────────────────────────────────────────────────────────────
// Buffer management
// ─────────────────────────────────────────────────────────────

void delay_alloc_buffers()
{
    // Free any existing buffers first
    delay_free_buffers();

    size_t buf_bytes = (DELAY_MAX_SAMPLES + 1) * sizeof(int16_t);

    delay_buf_low_L  = (int16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    delay_buf_low_R  = (int16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    delay_buf_high_L = (int16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    delay_buf_high_R = (int16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);

    delay_reset_state();
}

void delay_free_buffers()
{
    if (delay_buf_low_L)  { free(delay_buf_low_L);  delay_buf_low_L  = NULL; }
    if (delay_buf_low_R)  { free(delay_buf_low_R);  delay_buf_low_R  = NULL; }
    if (delay_buf_high_L) { free(delay_buf_high_L); delay_buf_high_L = NULL; }
    if (delay_buf_high_R) { free(delay_buf_high_R); delay_buf_high_R = NULL; }
    delay_write_idx[0] = 0;
    delay_write_idx[1] = 0;
}

// ─────────────────────────────────────────────────────────────
// Processing
// ─────────────────────────────────────────────────────────────

void delay_process_sample(int16_t in_l, int16_t in_r,
                          int16_t &out_l, int16_t &out_r,
                          uint8_t driver)
{
    if (driver >= DRIVER_BANDS) {
        out_l = in_l;
        out_r = in_r;
        return;
    }

    // Read from smoothed params for click-free parameter changes
    const delay_params_t &params =
        (&smoothed_params.low_driver)[driver].delay;

    int16_t *buf_L = (driver == 0) ? delay_buf_low_L  : delay_buf_high_L;
    int16_t *buf_R = (driver == 0) ? delay_buf_low_R  : delay_buf_high_R;

    if (!params.enabled || buf_L == NULL || buf_R == NULL) {
        out_l = in_l;
        out_r = in_r;
        return;
    }

    uint16_t delay_samples = params.samples;
    if (delay_samples > DELAY_MAX_SAMPLES) delay_samples = DELAY_MAX_SAMPLES;

    uint16_t widx = delay_write_idx[driver];
    uint16_t buf_size = DELAY_MAX_SAMPLES + 1;

    // Write current input
    buf_L[widx] = in_l;
    buf_R[widx] = in_r;

    // Read delayed sample
    uint16_t ridx;
    if (delay_samples == 0) {
        ridx = widx;  // No delay → read what we just wrote
    } else {
        ridx = (widx + buf_size - delay_samples) % buf_size;
    }

    out_l = buf_L[ridx];
    out_r = buf_R[ridx];

    // Advance write index
    delay_write_idx[driver] = (widx + 1) % buf_size;
}

void delay_reset_state()
{
    size_t buf_bytes = (DELAY_MAX_SAMPLES + 1) * sizeof(int16_t);

    if (delay_buf_low_L)  memset(delay_buf_low_L,  0, buf_bytes);
    if (delay_buf_low_R)  memset(delay_buf_low_R,  0, buf_bytes);
    if (delay_buf_high_L) memset(delay_buf_high_L, 0, buf_bytes);
    if (delay_buf_high_R) memset(delay_buf_high_R, 0, buf_bytes);

    delay_write_idx[0] = 0;
    delay_write_idx[1] = 0;
}
