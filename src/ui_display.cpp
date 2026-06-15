/**
 * @file ui_display.cpp
 * @brief SSD1306 OLED rendering with I2C Error Handling and RMS VU Meter.
 */

#include "ui_display.h"
#include "globals.h"
#include <Arduino.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void ui_init()
{
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_ADDRESS)) {
        Serial.println("[FAIL] SSD1306 OLED not found!");
        return;
    }

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
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("=== ESP32-DSP ===");

    display.setCursor(0, 10);
    display.print("BT: ");
    if (bt_state == BT_DISCONNECTED)
        display.println("Disconnected");
    else if (bt_state == BT_PLAYING)
        display.println(">> Playing <<");
    else
        display.println("Connected");

    display.setCursor(0, 22);
    display.printf("Vol:  %3.0f%%\n", p.master_volume * 100.0f);
    display.printf("Xov:  %.0f Hz\n", p.crossover_hz);
    display.printf("LoG:  %.2f\n", p.low_gain);
    display.printf("HiG:  %.2f\n", p.high_gain);

    // --- IMPROVEMENT: RMS VU Meter ---
    // We draw a background bar, then fill it based on the global_rms_level
    // calculated in the DSP task.
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

    display.display();
}

