/**
 * @file ui_display.cpp
 * @brief SSD1306 OLED rendering with I2C Error Handling, RMS VU Meter,
 *        and multi-page display for all DSP parameters.
 *
 * Display pages (cycled by encoder push button):
 *   Page 0: Main status (volume, crossover, gains, VU meter, BT state)
 *   Page 1: EQ bands 0-1
 *   Page 2: EQ bands 2-3
 *   Page 3: Dynamics (compressor + limiter per band)
 *   Page 4: Delay + mute/bypass status
 */

#include "ui_display.h"
#include "globals.h"
#include <Arduino.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ─────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────

/**
 * @brief Format a frequency value for display.
 * Shows "X Hz" for f < 1000, "X.XX kHz" for f >= 1000.
 */
static void format_freq(char *buf, size_t len, float freq)
{
    if (freq >= 1000.0f) {
        snprintf(buf, len, "%.2fk", freq / 1000.0f);
    } else {
        snprintf(buf, len, "%.0f", freq);
    }
}

/**
 * @brief Draw the RMS VU meter bar at the bottom of the screen.
 */
static void draw_vu_meter(void)
{
    display.drawRect(0, 56, 128, 7, SSD1306_WHITE);

    // Normalize RMS to a 0.0 to 1.0 range.
    // 32767 is the max value for 16-bit audio.
    float normalized_rms = global_rms_level / 32767.0f;

    // Apply a small boost to the visual bar so quiet music is still visible
    float visual_level = normalized_rms * 2.0f;
    if (visual_level > 1.0f) visual_level = 1.0f;

    int bar_width = (int)(visual_level * 124.0f);
    if (bar_width > 0)
        display.fillRect(2, 58, bar_width, 3, SSD1306_WHITE);
}

/**
 * @brief Draw a page header with title and page number.
 */
static void draw_header(const char *title, uint8_t page, uint8_t total_pages)
{
    display.setCursor(0, 0);
    display.print(title);
    // Page indicator on the right
    char pg_buf[8];
    snprintf(pg_buf, sizeof(pg_buf), "%d/%d", page + 1, total_pages);
    display.setCursor(128 - (strlen(pg_buf) * 6), 0);
    display.print(pg_buf);
    // Separator line
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
}

// ─────────────────────────────────────────────────────────────
// Page renderers
// ─────────────────────────────────────────────────────────────

static void draw_page_main(const dsp_params_t &p)
{
    draw_header("ESP32-DSP", 0, 5);

    // Bluetooth state
    display.setCursor(0, 12);
    display.print("BT:");
    if (bt_state == BT_DISCONNECTED)
        display.println(" Disconn");
    else if (bt_state == BT_PLAYING)
        display.println(" >>PLAY<<");
    else
        display.println(" Conn");

    // Volume
    display.printf("Vol: %3.0f%%", p.master_volume * 100.0f);
    if (p.mute) display.print(" [MUTE]");
    display.println();

    // Crossover
    char freq_buf[8];
    format_freq(freq_buf, sizeof(freq_buf), p.crossover_hz);
    display.printf("Xov: %sHz", freq_buf);
    display.println();

    // Gains
    display.printf("LoG: %.2f  HiG: %.2f", p.low_gain, p.high_gain);
    display.println();

    // Bypass indicator
    if (p.bypass) {
        display.setCursor(0, 44);
        display.print("*** BYPASS ***");
    }

    draw_vu_meter();
}

static void draw_page_eq_12(const dsp_params_t &p)
{
    draw_header("EQ Bands 0-1", 1, 5);

    display.setCursor(0, 12);
    for (uint8_t i = 0; i < 2; i++) {
        char freq_buf[8];
        format_freq(freq_buf, sizeof(freq_buf), p.eq_bands[i].freq_hz);
        display.printf("EQ%d:%s %s%.1fQB%.1f",
                       i,
                       p.eq_bands[i].enabled ? "ON " : "OFF",
                       freq_buf,
                       p.eq_bands[i].gain_db,
                       p.eq_bands[i].q);
        display.println();
    }
}

static void draw_page_eq_34(const dsp_params_t &p)
{
    draw_header("EQ Bands 2-3", 2, 5);

    display.setCursor(0, 12);
    for (uint8_t i = 2; i < 4; i++) {
        char freq_buf[8];
        format_freq(freq_buf, sizeof(freq_buf), p.eq_bands[i].freq_hz);
        display.printf("EQ%d:%s %s %.1fdB Q%.1f",
                       i,
                       p.eq_bands[i].enabled ? "ON " : "OFF",
                       freq_buf,
                       p.eq_bands[i].gain_db,
                       p.eq_bands[i].q);
        display.println();
    }
}

static void draw_page_dynamics(const dsp_params_t &p)
{
    draw_header("Dynamics", 3, 5);

    display.setCursor(0, 12);

    // Low driver (woofer)
    display.print("LOW  ");
    if (p.low_driver.compressor.enabled) {
        display.printf("C:%.0fdB R%.0f:1",
                       p.low_driver.compressor.threshold_db,
                       p.low_driver.compressor.ratio);
    } else {
        display.print("C:OFF");
    }
    display.println();

    display.print("      ");
    if (p.low_driver.limiter.enabled) {
        display.printf("L:%.0fdB", p.low_driver.limiter.threshold_db);
    } else {
        display.print("L:OFF");
    }
    display.println();

    // High driver (tweeter)
    display.print("HIGH ");
    if (p.high_driver.compressor.enabled) {
        display.printf("C:%.0fdB R%.0f:1",
                       p.high_driver.compressor.threshold_db,
                       p.high_driver.compressor.ratio);
    } else {
        display.print("C:OFF");
    }
    display.println();

    display.print("      ");
    if (p.high_driver.limiter.enabled) {
        display.printf("L:%.0fdB", p.high_driver.limiter.threshold_db);
    } else {
        display.print("L:OFF");
    }
    display.println();
}

static void draw_page_delay(const dsp_params_t &p)
{
    draw_header("Delay/Status", 4, 5);

    display.setCursor(0, 12);

    // Low driver delay
    float low_ms = (float)p.low_driver.delay.samples * 1000.0f / (float)SAMPLE_RATE;
    display.printf("LoDly: %.1fms %s", low_ms,
                   p.low_driver.delay.enabled ? "ON" : "OFF");
    display.println();

    // High driver delay
    float high_ms = (float)p.high_driver.delay.samples * 1000.0f / (float)SAMPLE_RATE;
    display.printf("HiDly: %.1fms %s", high_ms,
                   p.high_driver.delay.enabled ? "ON" : "OFF");
    display.println();

    // Status
    display.setCursor(0, 36);
    display.printf("Mute: %s  Bypass: %s",
                   p.mute ? "YES" : "NO",
                   p.bypass ? "YES" : "NO");
    display.println();

    // Preset
    display.printf("Preset: %d/%d", p.active_preset + 1, NUM_PRESETS);
    display.println();

    draw_vu_meter();
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

void ui_init()
{
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_ADDRESS)) {
        Serial.println("[FAIL] SSD1306 OLED not found!");
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("  ESP32-DSP");
    display.println("  2-Way Crossover");
    display.println("  EQ+Comp+Lim+Dly");
    display.setCursor(0, 48);
    display.println("  Starting...");
    display.display();
    Serial.println("[OK] SSD1306 OLED initialised");
}

void ui_update(const dsp_params_t &p)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    switch (p.active_page) {
        case 0:
            draw_page_main(p);
            break;
        case 1:
            draw_page_eq_12(p);
            break;
        case 2:
            draw_page_eq_34(p);
            break;
        case 3:
            draw_page_dynamics(p);
            break;
        case 4:
            draw_page_delay(p);
            break;
        default:
            draw_page_main(p);
            break;
    }

    display.display();
}
