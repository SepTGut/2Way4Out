/**
 * @file ui_display.cpp
 * @brief SSD1306 OLED rendering — draws the status screen.
 *
 * Layout (128×64 pixels, 8-pixel-tall text rows):
 *
 *   Row 0   : "=== ESP32DSP ==="        ← title bar
 *   Row 10  : "BT: Disconnected"        ← Bluetooth state
 *   Row 22  : "Vol:  80%"               ← master volume (0–100%)
 *   Row 30  : "Xov:  2000 Hz"           ← crossover frequency
 *   Row 38  : "LoG:  1.00"              ← low-band gain
 *   Row 46  : "HiG:  1.00"              ← high-band gain
 *   Row 56–62: ▓▓▓▓▓░░░░░             ← VU bar (volume-proportional)
 *
 * The Adafruit_GFX library uses a monospace 5×7 font at text size 1,
 * which gives 8 pixels per character row with 1-pixel spacing.
 * With 128 pixels wide we get ~21 characters per line.
 *
 * I2C considerations:
 *   - The SSD1306 is relatively slow on I2C (400 kHz max).
 *   - We update at 5 Hz (200 ms) which is more than fast enough
 *     for readability and keeps I2C bus traffic modest.
 *   - display.display() sends the entire 128×64/8 = 1024-byte
 *     buffer over I2C in a single transaction.
 */

#include "ui_display.h"
#include "globals.h"
#include <Arduino.h>

// ── Display object definition ────────────────────────────────
// Hardware I2C, no reset pin (-1), 128×64 resolution.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void ui_init()
{
    // Attempt to initialise the display.  If the SSD1306 doesn't respond
    // on the I2C bus (wrong address, wiring issue, etc.) we log the error
    // but continue — audio processing doesn't depend on the display.
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_ADDRESS)) {
        Serial.println("[FAIL] SSD1306 OLED not found!");
        return;
    }

    // Show a brief splash message during boot so the user gets feedback
    // that the system is starting up.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 24);
    display.println("  ESP32-DSP");
    display.println("  Starting...");
    display.display();
    Serial.println("[OK] SSD1306 OLED initialised");
}

void ui_update(const dsp_params_t &p)
{
    // ── Clear the entire frame buffer ────────────────────────
    // (necessary because we're doing a full redraw each frame)
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // ── Row 0: Title bar ─────────────────────────────────────
    display.setCursor(0, 0);
    display.println("=== ESP32DSP ===");

    // ── Row 10: Bluetooth state ──────────────────────────────
    display.setCursor(0, 10);
    display.print("BT: ");
    if (bt_state == BT_DISCONNECTED)
        display.println("Disconnected");
    else if (bt_state == BT_PLAYING)
        display.println(">> Playing <<");
    else
        display.println("Connected");

    // ── Row 22–48: DSP parameters ────────────────────────────
    // printf-style formatting keeps the layout aligned.
    display.setCursor(0, 22);
    display.printf("Vol:  %3.0f%%\n", p.master_volume * 100.0f);
    display.printf("Xov:  %.0f Hz\n", p.crossover_hz);
    display.printf("LoG:  %.2f\n", p.low_gain);
    display.printf("HiG:  %.2f\n", p.high_gain);

    // ── Row 56–62: VU meter bar ─────────────────────────────
    // Draw a hollow rectangle as the bar background, then fill a
    // proportional inner rectangle representing the master volume.
    // Bar width = 128 px, inner fill = 124 px max (2 px border each side).
    display.drawRect(0, 56, 128, 7, SSD1306_WHITE);
    int bar_width = (int)(p.master_volume * 124.0f);
    if (bar_width > 0)
        display.fillRect(2, 58, bar_width, 3, SSD1306_WHITE);

    // ── Commit the frame buffer to the OLED via I2C ─────────
    display.display();
}
