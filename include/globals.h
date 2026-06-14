/**
 * @file globals.h
 * @brief Extern declarations for shared global state.
 *
 * Global variables are DEFINED in src/globals.cpp and DECLARED here with
 * `extern` so that any module can include this header and access the
 * shared state.
 *
 * Thread safety:
 *   - audio_rb:       accessed from A2DP callback (ISR-like) and DSP task.
 *                     FreeRTOS ring buffer handles are inherently
 *                     thread-safe for single-producer/single-consumer.
 *   - current_params: protected by dsp_params_mutex (see dsp_config.h).
 *   - bt_state:       marked volatile because it is written from
 *                     Bluetooth callbacks and read from the DSP task
 *                     without a mutex (it's a simple enum, atomic read).
 *   - filt_state[]:   only touched by the DSP task (Core 1), so no
 *                     synchronization needed.
 */

#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "dsp_config.h"

// ─────────────────────────────────────────────────────────────
// Audio ring buffer
// ─────────────────────────────────────────────────────────────
// Written by: A2DP data callback (feeds audio from Bluetooth).
// Read by:    DSP task (consumes audio for crossover processing).
// Type:       RINGBUF_TYPE_BYTEBUF — raw byte stream, no framing.

extern RingbufHandle_t audio_rb;

// ─────────────────────────────────────────────────────────────
// DSP parameters mutex
// ─────────────────────────────────────────────────────────────
// Protects current_params.  The UI task (Core 0) acquires this mutex
// before updating the params after reading the ADS1115.  The DSP task
// (Core 1) acquires it before reading params for the crossover filter.
// Keep critical sections short to avoid audio glitches.

extern SemaphoreHandle_t dsp_params_mutex;

// ─────────────────────────────────────────────────────────────
// Current DSP parameters
// ────────────────────────────────────────────────────────────///
// The "ground truth" for what the user has set via the 4 pots.
// Must be accessed under dsp_params_mutex (except in DSP task fallback
// path where the mutex timeout means the struct is re-read).

extern dsp_params_t current_params;

// ─────────────────────────────────────────────────────────────
// Bluetooth connection state
// ─────────────────────────────────────────────────────────────
// Updated by: Bluetooth library callbacks (connection / audio state).
// Read by:    UI task for display.
// Volatile because it is written from a callback context and read from
// the UI task without a mutex — the enum is small enough for atomic read.

extern volatile bt_conn_state_t bt_state;

// ─────────────────────────────────────────────────────────────
// Crossover filter state
// ─────────────────────────────────────────────────────────────
// Persistent filter memory for the first-order IIR crossover.
//   [0] = left  low-pass previous output  (L_low_prev)
//   [1] = right low-pass previous output  (R_low_prev)
//   [2] = left  high-pass previous output (L_high_prev)
//   [3] = right high-pass previous output (R_high_prev)
//
// Only the DSP task touches these, so no mutex is needed.
// Initialized to 0.0f (silence) at startup.

extern float filt_state[4];
