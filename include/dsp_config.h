/**
 * @file dsp_config.h
 * @brief Compile-time configuration constants and type definitions for the
 *        ESP32-DSP 2-Way Active Crossover.
 *
 * This header is shared across ALL modules (globals, dsp_core, adc_control,
 * ui_display, bluetooth_sink, i2s_dac, main).  It should contain ONLY
 * constants and type declarations — no function definitions or extern
 * variable declarations.
 *
 * Hardware summary:
 *   - ESP32-WROOM-32 / DevKit V4  (dual-core Xtensa LX6, up to 240 MHz)
 *   - 2× CS4344  I2S DACs  (stereo 24-bit, slave-mode, no MCLK needed)
 *   - 1× ADS1115 I2C ADC  (4 channels for potentiometer inputs)
 *   - 1× SSD1306 I2C OLED (128×64, used as status display)
 */

#pragma once

// ─────────────────────────────────────────────────────────────
// M_PI fallback — some ESP32 Arduino cores do not define M_PI,
// but we need it for crossover filter coefficient calculations.
// ─────────────────────────────────────────────────────────────
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ═══════════════════════════════════════════════════════════════
// SECTION 1 — Audio Stream Configuration
// ═══════════════════════════════════════════════════════════════
//
// The Bluetooth A2DP profile on ESP32 delivers PCM audio at a fixed
// 44.1 kHz sample rate.  All DACs and the DSP processing chain must
// operate at this rate.

/// Sample rate in Hz.  Must match the A2DP source output rate.
#define SAMPLE_RATE         44100

/// Number of audio frames processed per DSP batch.
/// One frame = one stereo sample (left + right, each 16-bit).
/// 128 frames × 2 channels × 2 bytes = 512 bytes per batch.
#define FRAMES_PER_BATCH    128

/// Number of DMA buffers used by the I2S peripheral.
/// More buffers = more latency but safer against underruns.
/// 8 buffers × 64 samples = 512 samples total DMA capacity.
#define DMA_BUF_COUNT       8

/// Length of each DMA buffer in samples.
/// Smaller values = lower latency but more CPU interrupts.
#define DMA_BUF_LEN         64

// ═══════════════════════════════════════════════════════════════
// SECTION 2 — Ring Buffer
// ═══════════════════════════════════════════════════════════════
//
// The ring buffer decouples the Bluetooth A2DP callback (which runs in
// an ISR-like context and must return quickly) from the DSP task (which
// runs at high priority on Core 1 and processes audio in batches).
//
// Size trade-off: larger = more tolerance for jitter, but more RAM used.
// 8 KB ≈ 46 ms of stereo 16-bit audio at 44.1 kHz, which is sufficient
// to absorb typical FreeRTOS scheduling jitter.

/// Ring buffer capacity in bytes.
/// Stereo 16-bit at 44.1 kHz = 176 bytes/ms, so 8 KB ≈ 46 ms of audio.
#define RINGBUF_SIZE        (8 * 1024)

// ═══════════════════════════════════════════════════════════════
// SECTION 3 — Bluetooth A2DP
// ═══════════════════════════════════════════════════════════════

/// Bluetooth device name shown to the phone/tablet when scanning.
#define BLUETOOTH_NAME      "esp32DSP"

// ═══════════════════════════════════════════════════════════════
// SECTION 4 — ADS1115 ADC
// ═══════════════════════════════════════════════════════════════
//
// The ADS1115 is a 16-bit I2C ADC used to read 4 potentiometers:
//   CH0 = master volume
//   CH1 = crossover frequency
//   CH2 = low-band gain
//   CH3 = high-band gain
//
// At GAIN_ONE (default), the full-scale range is ±4.096 V, and the
// LSB size is 0.125 mV.  In single-ended mode the effective range is
// 0 … +4.096 V → raw value 0 … 26666 (15-bit + sign field at 16-bit
// resolution with sign-extension).

/// I2C address of the ADS1115 (ADDR pin tied to GND = 0x48).
#define ADS1115_ADDRESS     0x48

/// Maximum raw ADC reading in single-ended mode at GAIN_ONE.
/// At ±4.096 V range the 16-bit signed output spans −32767 … +32767,
/// but in single-ended mode (0 … +FS) the positive limit is +26666
/// (4.096 V / 0.125 mV per LSB − 1 sign bit overhead).
#define ADS_FULL_SCALE      26666.0f

// ═══════════════════════════════════════════════════════════════
// SECTION 5 — Crossover Filter
// ═══════════════════════════════════════════════════════════════
//
// The crossover is a first-order IIR low-pass (6 dB/oct).  The high-pass
// is derived by subtracting the low-pass output from the original signal.
// This is simple but effective for a 2-way active speaker.
//
// Frequency limits prevent the filter from becoming too aggressive
// (very low cutoff → excessive attenuation) or unstable (Nyquist).

/// Minimum crossover frequency in Hz.  Below this the filter is clamped.
#define CROSSOVER_MIN_HZ    200.0f

/// Maximum crossover frequency in Hz.  Above this the filter is clamped.
#define CROSSOVER_MAX_HZ    4000.0f

// ═══════════════════════════════════════════════════════════════
// SECTION 6 — Output Gain
// ═══════════════════════════════════════════════════════════════
//
// Per-band gain allows balancing the output levels of the two drivers
// (woofer and tweeter).  A value of 1.0 = unity gain (no boost/cut).

/// Maximum gain multiplier (2.0 = +6 dB boost).
#define GAIN_MAX            2.0f

// ═══════════════════════════════════════════════════════════════
// SECTION 7 — SSD1306 OLED Display
// ═══════════════════════════════════════════════════════════════

/// I2C address of the SSD1306 OLED module.
/// 0x3C when SA0 pin is tied LOW, 0x3D when tied HIGH.
#define SSD1306_ADDRESS     0x3C

// ═══════════════════════════════════════════════════════════════
// SECTION 8 — UI Refresh Rate
// ═══════════════════════════════════════════════════════════════

/// How often the OLED is redrawn (in milliseconds).
/// 200 ms = 5 fps.  Human eyes can't read faster than ~10 Hz,
/// and updating too frequently wastes I2C bus bandwidth.
#define UI_REFRESH_MS       200

// ═══════════════════════════════════════════════════════════════
// SECTION 9 — FreeRTOS Task Priorities
// ═══════════════════════════════════════════════════════════════
//
// ESP32 FreeRTOS priority range: 0 (idle) … 31 (highest).
// The DSP task must have higher priority than the UI task so that
// audio processing is never delayed by ADC/OLED operations.
//
// Core assignment:
//   DSP task  → Core 1 (high priority)  — deterministic audio processing
//   UI task   → Core 0 (lower priority) — ADC reads + OLED rendering

/// DSP audio processing task priority (higher = more urgent).
#define DSP_TASK_PRIORITY   5

/// ADC reader + OLED display task priority (lower than DSP).
#define UI_TASK_PRIORITY    2

// ═══════════════════════════════════════════════════════════════
// Type Definitions
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Bluetooth connection state machine.
 *
 * State transitions:
 *   DISCONNECTED ──(phone connects)──► CONNECTED
 *   CONNECTED    ──(starts streaming)──► PLAYING
 *   PLAYING      ──(pauses/stops)──► CONNECTED
 *   CONNECTED    ──(phone disconnects)──► DISCONNECTED
 */
typedef enum {
    BT_DISCONNECTED = 0,   ///< No phone paired/connected
    BT_CONNECTED,          ///< Paired and connected, but not streaming audio
    BT_PLAYING,            ///< Actively streaming audio data
} bt_conn_state_t;

/**
 * @brief Runtime DSP parameters shared between the UI task (reader)
 *        and the DSP task (consumer).
 *
 * All fields are protected by `dsp_params_mutex`.  The UI task reads
 * the ADS1115 pots and writes this struct; the DSP task reads it inside
 * the audio processing loop.
 *
 * Units:
 *   master_volume  — linear scale, 0.0 (silent) … 1.0 (full)
 *   crossover_hz   — Hz, clamped to [CROSSOVER_MIN_HZ … CROSSOVER_MAX_HZ]
 *   low_gain       — linear multiplier, 0.0 … GAIN_MAX
 *   high_gain      — linear multiplier, 0.0 … GAIN_MAX
 */
typedef struct {
    float master_volume;   ///< 0.0 (mute) … 1.0 (full volume)
    float crossover_hz;    ///< Crossover frequency in Hz
    float low_gain;        ///< Gain applied to the low-frequency band
    float high_gain;       ///< Gain applied to the high-frequency band
} dsp_params_t;

/**
 * @brief Safe default values for DSP parameters.
 *
 * Used as a fallback when the mutex cannot be acquired (e.g. during
 * startup before the mutex is created, or in rare contention cases).
 */
static const dsp_params_t dsp_params_default = {
    .master_volume = 0.8f,    ///< 80% volume — loud but not maxed out
    .crossover_hz  = 2000.0f, ///< 2 kHz — typical 2-way speaker crossover
    .low_gain      = 1.0f,    ///< Unity gain on low band
    .high_gain     = 1.0f,    ///< Unity gain on high band
};
