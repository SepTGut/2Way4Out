/*
 * ESP32-DSP — 2-Way Active Crossover / DSP Front-End
 *
 * Target: ESP32-WROOM-32 / DevKit V4
 *
 * Hardware:
 *   2x CS4344 DAC  (I2S0 = high-band, I2S1 = low-band)
 *   1x ADS1115 ADC (4 pots: Master Vol, Crossover, Low Gain, High Gain)
 *   1x SSD1306 OLED 128x64 (I2C, addr 0x3C)
 *   Bluetooth A2DP sink (phone → ESP32 → DSP → DACs)
 *
 * Modules:
 *   globals      – shared state
 *   i2s_dac      – I2S initialisation for CS4344
 *   adc_control  – ADS1115 pot reader
 *   ui_display   – SSD1306 OLED rendering
 *   bluetooth_sink – A2DP sink + callbacks
 *   dsp_core     – crossover filter + DSP task
 */

#include <Arduino.h>
#include <Wire.h>

#include "globals.h"
#include "i2s_dac.h"
#include "adc_control.h"
#include "ui_display.h"
#include "bluetooth_sink.h"
#include "dsp_core.h"
#include "pins.h"

// ═══════════════════════════════════════════════════════
// UI task  (Core 0 — ADS1115 + OLED)
// ═══════════════════════════════════════════════════════
static void ui_task(void *)
{
    Serial.println("UI task started on Core " + String(xPortGetCoreID()));

    while (1) {
        dsp_params_t params;
        adc_update_params(params);

        // Update shared params
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_params = params;
            xSemaphoreGive(dsp_params_mutex);
        }

        // Update OLED
        ui_update(params);

        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

// ═══════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════
void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("  ESP32-DSP  2-Way Active Crossover");
    Serial.println("  Target: ESP32-WROOM-32 / DevKit V4");
    Serial.println("========================================\n");

    // ── Mute DACs during initialisation ──
    pinMode(PIN_DAC_HIGH_MUTE, OUTPUT);
    pinMode(PIN_DAC_LOW_MUTE, OUTPUT);
    digitalWrite(PIN_DAC_HIGH_MUTE, LOW);   // mute (active low)
    digitalWrite(PIN_DAC_LOW_MUTE, LOW);    // mute

    // ── I2S for both CS4344 DACs ──
    i2s_init(I2S0_NUM, PIN_I2S0_BCK, PIN_I2S0_WS, PIN_I2S0_DATA);
    i2s_init(I2S1_NUM, PIN_I2S1_BCK, PIN_I2S1_WS, PIN_I2S1_DATA);
    Serial.println("[OK] Both I2S peripherals initialised");

    // ── Ring buffer (audio from BT → DSP) ──
    audio_rb = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!audio_rb) {
        Serial.println("[FAIL] Ring buffer creation failed!");
    } else {
        Serial.printf("[OK] Ring buffer: %d bytes\n", RINGBUF_SIZE);
    }

    // ── Mutex for DSP params ──
    dsp_params_mutex = xSemaphoreCreateMutex();
    Serial.println("[OK] Mutex created");

    // ── I2C (ADS1115 + SSD1306) ──
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    if (!ads.begin(ADS1115_ADDRESS)) {
        Serial.println("[FAIL] ADS1115 not found! Check wiring.");
    } else {
        ads.setGain(GAIN_ONE);  // ±4.096V, 1 bit = 0.125mV
        Serial.println("[OK] ADS1115 initialised");
    }

    ui_init();
    bt_init();

    // ── Unmute DACs ──
    digitalWrite(PIN_DAC_HIGH_MUTE, HIGH);
    digitalWrite(PIN_DAC_LOW_MUTE, HIGH);
    Serial.println("[OK] DACs unmuted");

    // ── Create tasks ──
    // DSP task on Core 1 (audio processing)
    BaseType_t dsp_ret = xTaskCreatePinnedToCore(
        dsp_task, "dsp_task", 8192, NULL, DSP_TASK_PRIORITY, NULL, 1);
    if (dsp_ret == pdTRUE)
        Serial.println("[OK] DSP task created on Core 1");
    else
        Serial.println("[FAIL] DSP task creation failed!");

    // UI task on Core 0 (ADC + OLED)
    BaseType_t ui_ret = xTaskCreatePinnedToCore(
        ui_task, "ui_task", 4096, NULL, UI_TASK_PRIORITY, NULL, 0);
    if (ui_ret == pdTRUE)
        Serial.println("[OK] UI task created on Core 0");
    else
        Serial.println("[FAIL] UI task creation failed!");

    Serial.println("\n=== System ready. Connect via Bluetooth! ===\n");
}

// ═══════════════════════════════════════════════════════
// LOOP  — idle, watchdog / diagnostics
// ═══════════════════════════════════════════════════════
void loop()
{
    static uint32_t last_print = 0;
    if (millis() - last_print > 5000) {
        last_print = millis();

        Serial.printf("[diag] Free heap: %u bytes | BT state: %d | Core: %d\n",
                      ESP.getFreeHeap(), bt_state, xPortGetCoreID());

        if (audio_rb) {
            UBaseType_t items;
            vRingbufferGetInfo(audio_rb, NULL, NULL, NULL, NULL, &items);
            Serial.printf("[diag] Ring buffer items: %u\n", items);
        }
    }

    delay(100);
}
