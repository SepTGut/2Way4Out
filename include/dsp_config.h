#pragma once

// M_PI may not be defined on all ESP32 Arduino cores
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ═══════════════════════════════════════════════════════
// DSP Configuration Constants
// ═══════════════════════════════════════════════════════

// ── Audio ──────────────────────────────────────────────
#define SAMPLE_RATE         44100       // Hz — A2DP default
#define FRAMES_PER_BATCH    128         // DSP frames per processing batch
#define DMA_BUF_COUNT       8           // I2S DMA buffer count
#define DMA_BUF_LEN         64          // I2S DMA buffer length (samples)

// ── Ring buffer ────────────────────────────────────────
#define RINGBUF_SIZE        (8 * 1024)  // 8 KB audio ring buffer

// ── Bluetooth ──────────────────────────────────────────
#define BLUETOOTH_NAME      "esp32DSP"

// ── ADS1115 ────────────────────────────────────────────
#define ADS1115_ADDRESS     0x48        // ADDR pin tied to GND
#define ADS_FULL_SCALE      26666.0f    // Max raw reading (15-bit + sign)

// ── Crossover ──────────────────────────────────────────
#define CROSSOVER_MIN_HZ    200.0f      // Minimum crossover frequency
#define CROSSOVER_MAX_HZ    4000.0f     // Maximum crossover frequency

// ── Gain ───────────────────────────────────────────────
#define GAIN_MAX            2.0f        // Maximum gain multiplier

// ── OLED (SSD1306) ──────────────────────────────────────
#define SSD1306_ADDRESS     0x3C        // I2C address (0x3C or 0x3D)

// ── UI ─────────────────────────────────────────────────
#define UI_REFRESH_MS       200         // OLED refresh interval (ms)

// ── Task priorities ────────────────────────────────────
#define DSP_TASK_PRIORITY   5           // Audio DSP (high priority)
#define UI_TASK_PRIORITY    2           // ADC + OLED (lower priority)

// ═══════════════════════════════════════════════════════
// Type definitions
// ═══════════════════════════════════════════════════════

// Bluetooth connection state
typedef enum {
    BT_DISCONNECTED = 0,
    BT_CONNECTED,
    BT_PLAYING,
} bt_conn_state_t;

// Runtime DSP parameters — protected by mutex
typedef struct {
    float master_volume;      // 0.0 – 1.0
    float crossover_hz;       // CROSSOVER_MIN_HZ … CROSSOVER_MAX_HZ
    float low_gain;           // 0.0 – GAIN_MAX
    float high_gain;          // 0.0 – GAIN_MAX
} dsp_params_t;

// Default values
static const dsp_params_t dsp_params_default = {
    .master_volume = 0.8f,
    .crossover_hz  = 2000.0f,
    .low_gain      = 1.0f,
    .high_gain     = 1.0f,
};
