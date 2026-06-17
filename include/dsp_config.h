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

#include <stdint.h>

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
// The ADS1115 is a 16-bit I2C ADC used to read potentiometers:
//   CH0 = master volume
//   CH1 = crossover frequency
//   CH2 = low-band gain
//   CH3 = high-band gain
//
// A second ADS1115 (address 0x49) adds 4 more channels for EQ,
// dynamics, and other parameters.
//
// At GAIN_ONE (default), the full-scale range is ±4.096 V, and the
// LSB size is 0.125 mV.  In single-ended mode the effective range is
// 0 … +4.096 V → raw value 0 … 26666 (15-bit + sign field at 16-bit
// resolution with sign-extension).

/// I2C address of the first ADS1115 (ADDR pin tied to GND = 0x48).
#define ADS1115_ADDRESS     0x48

/// I2C address of the second ADS1115 (ADDR pin tied to VDD = 0x49).
#define ADS1115_ADDRESS_2   0x49

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
// SECTION 10 — Effect Configuration Limits
// ═══════════════════════════════════════════════════════════════

/// Maximum number of EQ bands (full-range, applied before crossover).
#define EQ_MAX_BANDS        4

/// Maximum number of driver bands (low + high = 2).
#define DRIVER_BANDS        2

/// Maximum delay time in milliseconds (per band).
/// At 44.1 kHz, 20 ms = 882 samples.
#define DELAY_MAX_MS        20.0f

/// Maximum delay samples (DELAY_MAX_MS * SAMPLE_RATE / 1000).
#define DELAY_MAX_SAMPLES   882

/// Number of user presets storable in NVS flash.
#define NUM_PRESETS         8

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

// ─────────────────────────────────────────────────────────────
// Forward declarations for new DSP effect types
// ─────────────────────────────────────────────────────────────

/**
 * @brief One parametric EQ band (biquad filter).
 *
 * Applied to the full-range stereo signal before the crossover.
 * Supported types: low-shelf, peaking PEQ, high-shelf.
 */
typedef struct {
    bool  enabled;     ///< true = band is active
    float freq_hz;     ///< Centre frequency in Hz (20 – 20000)
    float gain_db;     ///< Gain in dB (-12.0 to +12.0)
    float q;           ///< Quality factor / bandwidth (0.1 – 10.0)
} eq_band_t;

/**
 * @brief Compressor / limiter parameters for one driver band.
 *
 * Feed-forward design with peak detector.
 * The limiter uses the same structure but with ratio = ∞ (very high)
 * and a hard knee.
 */
typedef struct {
    bool  enabled;         ///< true = dynamics processing active
    float threshold_db;    ///< Threshold in dBFS (-60 to 0)
    float ratio;           ///< Compression ratio (1:1 to 20:1; 100:1 for limiter)
    float attack_ms;       ///< Attack time in ms (0.1 – 100)
    float release_ms;      ///< Release time in ms (10 – 1000)
    float makeup_db;       ///< Makeup gain in dB (0 – 24)
} dyn_params_t;

/**
 * @brief Delay line parameters for one driver band.
 *
 * Used for time-alignment between woofer and tweeter acoustic centres.
 */
typedef struct {
    bool     enabled;      ///< true = delay active
    uint16_t samples;      ///< Delay length in samples (0 – DELAY_MAX_SAMPLES)
} delay_params_t;

/**
 * @brief Per-driver (per-band) DSP parameters.
 *
 * Each driver (low=woofer, high=tweeter) gets its own compressor,
 * limiter, and delay.
 */
typedef struct {
    dyn_params_t   compressor;   ///< Compressor for this driver
    dyn_params_t   limiter;      ///< Limiter for this driver
    delay_params_t delay;        ///< Delay for this driver
} driver_params_t;

/**
 * @brief Runtime DSP parameters shared between the UI task (reader)
 *        and the DSP task (consumer).
 *
 * All fields are protected by `dsp_params_mutex`.  The UI task reads
 * the ADS1115 pots and writes this struct; the DSP task reads it inside
 * the audio processing loop.
 *
 * Signal flow:
 *   Full-range EQ → Crossover → Low driver (comp/lim/delay) → I2S1
 *                                High driver (comp/lim/delay) → I2S0
 */
typedef struct {
    // ── Legacy parameters (preserved for backward compatibility) ──
    float master_volume;   ///< 0.0 (mute) … 1.0 (full volume)
    float crossover_hz;    ///< Crossover frequency in Hz
    float low_gain;        ///< Linear multiplier, 0.0 … GAIN_MAX
    float high_gain;       ///< Linear multiplier, 0.0 … GAIN_MAX

    // ── Parametric EQ (4 bands, applied to full-range signal) ──
    eq_band_t eq_bands[EQ_MAX_BANDS];

    // ── Per-driver dynamics + delay ──
    driver_params_t low_driver;    ///< Woofer (low band) → I2S1
    driver_params_t high_driver;   ///< Tweeter (high band) → I2S0

    // ── UI state (not audio parameters, but shared) ──
    uint8_t  active_page;          ///< OLED display page (0 = main, 1 = EQ, etc.)
    uint8_t  active_preset;        ///< Currently loaded preset index (0 … NUM_PRESETS-1)
    bool     mute;                 ///< true = all outputs silenced
    bool     bypass;               ///< true = DSP bypass (raw stereo → both DACs)
} dsp_params_t;

/**
 * @brief Complete preset — a snapshottable dsp_params_t with a name.
 */
typedef struct {
    dsp_params_t params;            ///< All DSP parameters
    char         name[12];          ///< Short human-readable label
} preset_t;

/**
 * @brief Safe default values for DSP parameters.
 *
 * Used as a fallback when the mutex cannot be acquired or no preset
 * has been loaded yet.
 */
static const dsp_params_t dsp_params_default = {
    .master_volume = 0.8f,
    .crossover_hz  = 2000.0f,
    .low_gain      = 1.0f,
    .high_gain     = 1.0f,
    .eq_bands = {
        {false,  100.0f, 0.0f, 0.7f},   // Low shelf
        {false,  500.0f, 0.0f, 1.0f},   // Low-mid PEQ
        {false, 2000.0f, 0.0f, 1.0f},   // High-mid PEQ
        {false, 8000.0f, 0.0f, 0.7f},   // High shelf
    },
    .low_driver = {
        .compressor = {false, -12.0f,  2.0f,  10.0f, 100.0f, 0.0f},
        .limiter    = {true,   -3.0f, 100.0f,   1.0f,  50.0f, 0.0f},
        .delay      = {false, 0},
    },
    .high_driver = {
        .compressor = {false, -12.0f,  2.0f,  10.0f, 100.0f, 0.0f},
        .limiter    = {true,   -3.0f, 100.0f,   1.0f,  50.0f, 0.0f},
        .delay      = {false, 0},
    },
    .active_page   = 0,
    .active_preset = 0,
    .mute          = false,
    .bypass        = false,
};
