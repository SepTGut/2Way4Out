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

    // Title bar
    display.setCursor(0, 0);
    display.println("=== ESP32DSP ===");

    // Bluetooth status
    display.setCursor(0, 10);
    display.print("BT: ");
    if (bt_state == BT_DISCONNECTED)      display.println("Disconnected");
    else if (bt_state == BT_PLAYING)      display.println(">> Playing <<");
    else                                   display.println("Connected");

    // DSP parameters
    display.setCursor(0, 22);
    display.printf("Vol:  %3.0f%%\n", p.master_volume * 100.0f);
    display.printf("Xov:  %.0f Hz\n", p.crossover_hz);
    display.printf("LoG:  %.2f\n", p.low_gain);
    display.printf("HiG:  %.2f\n", p.high_gain);

    // VU meter bar (based on master volume)
    display.drawRect(0, 56, 128, 7, SSD1306_WHITE);
    int bar_width = (int)(p.master_volume * 124.0f);
    if (bar_width > 0)
        display.fillRect(2, 58, bar_width, 3, SSD1306_WHITE);

    display.display();
}
