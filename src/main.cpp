/**
 * @file main.cpp
 * @brief Entry point and system orchestrator for the ESP32-DSP 2-Way
 *        Active Crossover.
 *
 * This file is the THIN orchestrator — it initialises all hardware
 * modules in the correct order, creates the two FreeRTOS tasks, and
 * then handles light diagnostics in the idle loop.
 *
 * Architecture overview:
 *   ┌─────────────┐     ┌─────────────┐     ┌───────────────────┐
 *   │  Phone/BT   │────►│  A2DP Sink  │────►│   Ring Buffer     │
 *   │  (source)   │     │  (callback) │     │   (8 KB)          │
 *   └─────────────┘     └─────────────┘     └────────┬──────────┘
 *                                                     │
 *                    ┌────────────────────────────────┘
 *                    │  DSP Task (Core 1, priority 5)
 *                    │  ┌─────────────────────────────────────┐
 *                    ├─►│ 1. Read batch from ring buffer      │
 *                    │  │ 2. Grab DSP params (mutex)          │
 *                    │  │ 3. Smooth parameters                │
 *                    │  │ 4. DC block filter                  │
 *                    │  │ 5. Parametric EQ (4-band biquad)    │
 *                    │  │ 6. Compute crossover coefficients   │
 *                    │  │ 7. Apply LPF/HPF to each frame      │
 *                    │  │ 8. Per-band compressor → limiter    │
 *                    │  │ 9. Per-band delay (time alignment)  │
 *                    │  │10. Apply gain + master volume       │
 *                    │  │11. Clip to 16-bit                   │
 *                    │  │12. Write low-band → I2S1 (woofer)  │
 *                    │  │13. Write high-band → I2S0 (tweeter)│
 *                    │  └─────────────────────────────────────┘
 *                    │
 *   ┌────────────────┴──────────────────────────────────────┐
 *   │  UI Task (Core 0, priority 2)                         │
 *   │  ┌────────────────────────────────────────────────┐   │
 *   │  │ 1. Read ADS1115 (8 pots → DSP params)          │   │
 *   │  │ 2. Read encoder + footswitches                 │   │
 *   │  │ 3. Update current_params (mutex)               │   │
 *   │  │ 4. Render OLED status screen                   │   │
 *   │  │ 5. Sleep 200 ms                                │   │
 *   │  └────────────────────────────────────────────────┘   │
 *   └───────────────────────────────────────────────────────┘
 *
 * Task pinning rationale:
 *   - Core 0: runs the UI task and the Arduino WiFi/BT stack internally.
 *             Keeping UI here avoids competing with DSP for CPU.
 *   - Core 1: dedicated to the DSP task for deterministic audio timing.
 */

#include <Arduino.h>
#include <Wire.h>

#include "globals.h"
#include "i2s_dac.h"
#include "adc_control.h"
#include "ui_display.h"
#include "bluetooth_sink.h"
#include "dsp_core.h"
#include "preset.h"
#include "pins.h"

// ═══════════════════════════════════════════════════════════════
// UI Task  (Core 0 — ADS1115 + Encoder + Footswitches + OLED)
// ═══════════════════════════════════════════════════════════════
//
// This task runs at lower priority than the DSP task so that audio
// processing is never delayed by ADC reads or OLED rendering.
//
// It reads the potentiometers, encoder, and footswitches, updates
// the shared parameters under mutex protection, and refreshes the
// OLED display — all in one loop iteration every 200 ms.

static void ui_task(void *)
{
    Serial.println("UI task started on Core " + String(xPortGetCoreID()));

    while (1) {
        // ── Read potentiometers + encoder + footswitches ──────
        // adc_update_params() reads all ADS1115 channels, processes
        // encoder steps, and handles footswitch flags.
        dsp_params_t params;
        adc_update_params(params);

        // ── Update shared parameters ────────────────────────
        // The mutex protects against the DSP task reading params
        // mid-update.  Critical section is very short (struct copy).
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_params = params;
            xSemaphoreGive(dsp_params_mutex);
        }
        // If the mutex is unavailable (DSP task is reading), we skip
        // this update cycle — the DSP still has the previous values.

        // ── Update OLED display ──────────────────────────────
        // Renders the full status screen: title, BT state, DSP params, VU bar.
        ui_update(params);

        // ── Yield for UI_REFRESH_MS ──────────────────────────
        // 200 ms delay = 5 fps refresh rate.  This is fast enough for
        // human readability while keeping I2C bus and CPU usage low.
        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

// ═══════════════════════════════════════════════════════════════
// SETUP — called once by the Arduino framework after boot
// ═══════════════════════════════════════════════════════════════
//
// Initialisation order matters:
//   1. Serial (debug output)
//   2. DAC mute pins (hold DACs silent during init)
//   3. I2S peripherals (configure DMA and pin routing)
//   4. Ring buffer (audio pipeline between BT and DSP)
//   5. Mutex (protects shared DSP parameters)
//   6. I2C bus (shared between ADS1115×2, SSD1306, EEPROM)
//   7. ADC + encoder + footswitches (adc_init handles all three)
//   8. SSD1306 OLED (status display)
//   9. Bluetooth A2DP sink (phone audio receiver)
//  10. Unmute DACs (now safe — DMA is ready)
//  11. Create DSP task (Core 1, high priority)
//  12. Create UI task  (Core 0, lower priority)

void setup()
{
    // ── 1. Serial debug port ────────────────────────────────
    Serial.begin(115200);
    delay(500);  // allow USB-serial chip to stabilise
    Serial.println("\n========================================");
    Serial.println("  ESP32-DSP  2-Way Active Crossover");
    Serial.println("  Target: ESP32-WROOM-32 / DevKit V4");
    Serial.println("  Features: EQ + Comp + Lim + Delay");
    Serial.println("========================================\n");

    // ── 2. Hold DACs muted during initialisation ───────────
    // The CS4344 has an active-low mute pin.  We keep the DACs muted
    // during setup to prevent audible pops from random I2S data.
    pinMode(PIN_DAC_HIGH_MUTE, OUTPUT);
    pinMode(PIN_DAC_LOW_MUTE, OUTPUT);
    digitalWrite(PIN_DAC_HIGH_MUTE, LOW);   // LOW = muted (active low)
    digitalWrite(PIN_DAC_LOW_MUTE, LOW);

    // ── 3. Initialise I2S for both CS4344 DACs ────────────
    // I2S0 drives the high-band DAC (tweeter).
    // I2S1 drives the low-band DAC (woofer).
    // Each call installs the I2S driver, routes pins, and clears DMA.
    i2s_init(I2S0_NUM, PIN_I2S0_BCK, PIN_I2S0_WS, PIN_I2S0_DATA);
    i2s_init(I2S1_NUM, PIN_I2S1_BCK, PIN_I2S1_WS, PIN_I2S1_DATA);
    Serial.println("[OK] Both I2S peripherals initialised");

    // ── 4. Create audio ring buffer ────────────────────────
    // RINGBUF_TYPE_BYTEBUF = raw byte stream with no framing.
    // Written by the A2DP callback, read by the DSP task.
    audio_rb = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!audio_rb) {
        Serial.println("[FAIL] Ring buffer creation failed!");
    } else {
        Serial.printf("[OK] Ring buffer: %d bytes\n", RINGBUF_SIZE);
    }

    // ── 5. Create mutex for DSP parameters ─────────────────
    // The mutex protects current_params from being read by the DSP
    // task while the UI task is writing updated values from the pots.
    dsp_params_mutex = xSemaphoreCreateMutex();
    Serial.println("[OK] Mutex created");

    // ── 6. Initialise I2C bus ──────────────────────────────
    // 400 kHz Fast Mode is a good balance of speed and reliability
    // on typical breadboard/protoboard wiring.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    // ── 7. Initialise NVS preset storage ───────────────────
    // preset_init() sets up NVS flash and loads preset 0 if available.
    preset_init();

    // ── 8. Initialise ADC + encoder + footswitches ─────────
    // adc_init() sets up both ADS1115 chips, encoder ISRs, and
    // footswitch ISRs.  If the 2nd ADS1115 is not found, the system
    // continues with 4 channels instead of 8.
    adc_init();

    // ── 8. Initialise SSD1306 OLED display ─────────────────
    ui_init();

    // ── 9. Initialise Bluetooth A2DP sink ──────────────────
    // This starts the Bluetooth stack and registers the device as
    // "esp32DSP".  The phone can now discover and connect to it.
    bt_init();

    // ── 10. Unmute DACs ────────────────────────────────────
    // Now that I2S is running and DMA buffers contain silence,
    // it's safe to unmute the DACs.  Audio will flow once the
    // phone starts streaming.
    digitalWrite(PIN_DAC_HIGH_MUTE, HIGH);   // HIGH = unmuted
    digitalWrite(PIN_DAC_LOW_MUTE, HIGH);
    Serial.println("[OK] DACs unmuted");

    // ── 11. Create DSP task on Core 1 ──────────────────────
    // This task does ALL the audio processing.  It must run at high
    // priority on a dedicated core to ensure glitch-free output.
    BaseType_t dsp_ret = xTaskCreatePinnedToCore(
        dsp_task, "dsp_task", 8192, NULL, DSP_TASK_PRIORITY, NULL, 1);
    if (dsp_ret == pdTRUE)
        Serial.println("[OK] DSP task created on Core 1");
    else
        Serial.println("[FAIL] DSP task creation failed!");

    // ── 12. Create UI task on Core 0 ───────────────────────
    // This task reads pots and updates the display.  Lower priority
    // ensures the DSP task always gets CPU time first.
    BaseType_t ui_ret = xTaskCreatePinnedToCore(
        ui_task, "ui_task", 4096, NULL, UI_TASK_PRIORITY, NULL, 0);
    if (ui_ret == pdTRUE)
        Serial.println("[OK] UI task created on Core 0");
    else
        Serial.println("[FAIL] UI task creation failed!");

    Serial.println("\n=== System ready. Connect via Bluetooth! ===\n");
}

// ═══════════════════════════════════════════════════════════════
// LOOP — idle task called repeatedly after setup()
// ═══════════════════════════════════════════════════════════════
//
// All real work happens in the DSP and UI tasks.  This loop just
// prints diagnostic info (free heap, BT state) every 5 seconds
// so the developer can monitor system health via the serial port.

void loop()
{
    static uint32_t last_print = 0;
    if (millis() - last_print > 5000) {
        last_print = millis();

        // Print free heap to detect memory leaks during development.
        // ESP32 has ~320 KB of usable RAM; we use ~40 KB (see build output).
        Serial.printf("[diag] Free heap: %u bytes | BT state: %d | Core: %d\n",
                      ESP.getFreeHeap(), bt_state, xPortGetCoreID());

        // Print ring buffer fill level to monitor pipeline health.
        // A steadily increasing level indicates the DSP task is falling behind.
        if (audio_rb) {
            UBaseType_t items;
            vRingbufferGetInfo(audio_rb, NULL, NULL, NULL, NULL, &items);
            Serial.printf("[diag] Ring buffer items: %u\n", items);
        }
    }

    delay(100);  // yield CPU — the loop task has the lowest priority
}
