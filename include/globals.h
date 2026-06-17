/**
 * @file globals.h
 * @brief Extern declarations for shared global state.
 */

#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "dsp_config.h"

// ── Audio pipeline ──
extern RingbufHandle_t audio_rb;

// ── DSP parameter protection ──
extern SemaphoreHandle_t dsp_params_mutex;
extern dsp_params_t current_params;

// ── Bluetooth state ──
extern volatile bt_conn_state_t bt_state;

// ── Crossover filter state (persists across batches) ──
// [0] = L low-prev, [1] = R low-prev, [2] = L high-prev, [3] = R high-prev
extern float filt_state[4];

// ── Audio metrics for UI ──
extern volatile float global_rms_level;

// ── EQ biquad coefficient cache ──
// Pre-computed coefficients for each EQ band to avoid per-sample trig.
// Layout: [band][coeff] where coeffs are: b0, b1, b2, a1, a2 (a0 normalised to 1)
extern float eq_coeffs[EQ_MAX_BANDS][5];
// Per-band filter state for Direct-Form-II biquad:
//   [band][channel][0] = w_z1 (w[n-1])
//   [band][channel][1] = w_z2 (w[n-2])
// channel 0 = left, channel 1 = right
extern float eq_state[EQ_MAX_BANDS][2][2];

// ── Compressor state per driver ──
// [driver][channel] where driver 0=low, 1=high; channel 0=L, 1=R
extern float comp_env[2][2];       // Envelope follower state (peak)
extern float comp_gain[2][2];      // Current smoothed gain factor (compressor)

// ── Limiter state per driver ──
extern float lim_gain[2][2];       // Current smoothed gain factor (limiter)

// ── Delay line state per driver ──
// Circular buffers allocated in dsp_core.cpp; pointers stored here
extern int16_t *delay_buf_low_L;   // Woofer left delay line
extern int16_t *delay_buf_low_R;   // Woofer right delay line
extern int16_t *delay_buf_high_L;  // Tweeter left delay line
extern int16_t *delay_buf_high_R;  // Tweeter right delay line
extern uint16_t delay_write_idx[2]; // Write position [low, high]

// ── UI / control state ──
extern volatile int8_t  enc_count;     // Accumulated encoder steps (cleared by UI)
extern volatile bool    enc_pressed;   // Encoder button pressed flag
extern volatile bool    fs_flags[4];   // Footswitch press flags [next, prev, mute, bypass]
extern uint8_t          adc_channels;  // Total ADC channels (4 or 8)
