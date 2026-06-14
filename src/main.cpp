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
 * CS4344 notes:
 *   - 24-bit stereo DAC, reads I2S data as 24-bit left-justified
 *   - When ESP32 I2S is set to 16-bit, CS4344 still works (pads LSBs with 0)
 *   - We use 16-bit I2S for simplicity; pins are BCK + LRCK + SDIN
 *   - No MCLK needed — CS4344 is slave mode only
 */

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "BluetoothA2DPSink.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

#include "pins.h"
#include "dsp_config.h"

// ── Display ────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── ADC ────────────────────────────────────────────────
Adafruit_ADS1115 ads;

// ── Bluetooth A2DP Sink ────────────────────────────────
BluetoothA2DPSink a2dp_sink;

// ── DSP parameters (mutex-protected) ───────────────────
static SemaphoreHandle_t dsp_params_mutex = NULL;
static dsp_params_t current_params = {
    .master_volume = 0.8f,
    .crossover_hz  = 2000.0f,
    .low_gain      = 1.0f,
    .high_gain     = 1.0f,
};

// ── Bluetooth state ────────────────────────────────────
volatile bt_conn_state_t bt_state = BT_DISCONNECTED;

// ── Audio ring buffer ──────────────────────────────────
static RingbufHandle_t audio_rb = NULL;

// ── Filter states for crossover (stereo LP) ────────────
static float filt_state[4] = {0};

// ── Forward declarations ───────────────────────────────
static void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1);
static void init_i2s(i2s_port_t port, int bck_pin, int ws_pin, int data_pin);
static void dsp_task(void *param);
static void ui_task(void *param);

// ═══════════════════════════════════════════════════════
// I2S INITIALISATION
// ═══════════════════════════════════════════════════════
static void init_i2s(i2s_port_t port, int bck_pin, int ws_pin, int data_pin)
{
    i2s_config_t i2s_cfg = {
        .mode                   = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate            = SAMPLE_RATE,
        .bits_per_sample        = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT,
        // CS4344 expects I2S Philips standard
        .communication_format   = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count          = DMA_BUF_COUNT,
        .dma_buf_len            = DMA_BUF_LEN,
        .use_apll               = true,
        .tx_desc_auto_clear     = true,
        .fixed_mclk             = 0,
        .mclk_multiple          = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan          = I2S_BITS_PER_CHAN_16BIT,
    };

    esp_err_t err = i2s_driver_install(port, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("I2S driver install failed on port %d: 0x%x\n", port, err);
        return;
    }

    i2s_pin_config_t pin_cfg = {
        .bck_io_num   = bck_pin,
        .ws_io_num    = ws_pin,
        .data_out_num = data_pin,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    i2s_set_pin(port, &pin_cfg);

    // Clear DMA buffers
    i2s_zero_dma_buffer(port);

    Serial.printf("I2S%d initialised (BCK=%d, WS=%d, DATA=%d)\n",
                  port, bck_pin, ws_pin, data_pin);
}

// ═══════════════════════════════════════════════════════
// FILTER COEFFICIENTS  — first-order IIR (6 dB/oct LP)
// ═══════════════════════════════════════════════════════
static void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1)
{
    // Clamp cutoff to avoid instability
    if (cutoff_hz < 10.0f)     cutoff_hz = 10.0f;
    if (cutoff_hz > 20000.0f)  cutoff_hz = 20000.0f;

    float rc  = 1.0f / (2.0f * M_PI * cutoff_hz);
    float dt  = 1.0f / sample_rate;
    float alpha = dt / (rc + dt);
    *a0 = alpha;
    *b1 = 1.0f - alpha;
}

// ═══════════════════════════════════════════════════════
// A2DP CALLBACKS
// ═══════════════════════════════════════════════════════
static void a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    if (audio_rb == NULL) return;
    // Non-blocking send — drop data if ring buffer full
    xRingbufferSend(audio_rb, data, len, 0);
}

static void bt_connection_state_cb(bool connected)
{
    bt_state = connected ? BT_CONNECTED : BT_DISCONNECTED;
}

static void bt_audio_state_cb(esp_a2d_audio_state_t state, void * /*ptr*/)
{
    if (state == ESP_A2D_AUDIO_STATE_STARTED) {
        bt_state = BT_PLAYING;
    } else if (state == ESP_A2D_AUDIO_STATE_STOPPED ||
               state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        if (bt_state == BT_PLAYING) bt_state = BT_CONNECTED;
    }
}

// ═══════════════════════════════════════════════════════
// DSP TASK  (Core 1)
// ═══════════════════════════════════════════════════════
static void dsp_task(void */*param*/)
{
    const size_t frames_per_batch = FRAMES_PER_BATCH;
    const size_t bytes_per_batch  = frames_per_batch * 2 * sizeof(int16_t);

    // DMA-capable buffers
    int16_t *raw_buf  = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);
    int16_t *low_buf  = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);
    int16_t *high_buf = (int16_t *)heap_caps_malloc(bytes_per_batch, MALLOC_CAP_DMA);

    if (!raw_buf || !low_buf || !high_buf) {
        Serial.println("DSP: buffer allocation failed!");
        vTaskDelete(NULL);
        return;
    }

    Serial.println("DSP task started on Core " + String(xPortGetCoreID()));

    while (1) {
        size_t item_size = 0;
        void *received_ptr = xRingbufferReceiveUpTo(audio_rb, &item_size,
                                                     pdMS_TO_TICKS(100),
                                                     bytes_per_batch);
        if (received_ptr == NULL) {
            // No data — send silence to keep DACs active
            memset(low_buf, 0, bytes_per_batch);
            memset(high_buf, 0, bytes_per_batch);
            size_t written = 0;
            i2s_write(I2S1_NUM, low_buf,  bytes_per_batch, &written, pdMS_TO_TICKS(20));
            i2s_write(I2S0_NUM, high_buf, bytes_per_batch, &written, pdMS_TO_TICKS(20));
            continue;
        }

        size_t received = item_size;
        size_t copy_len = min(received, bytes_per_batch);
        memcpy(raw_buf, received_ptr, copy_len);
        vRingbufferReturnItem(audio_rb, received_ptr);

        size_t actual_frames = copy_len / (2 * sizeof(int16_t));
        if (actual_frames == 0) continue;

        // Grab current DSP params
        dsp_params_t params;
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            params = current_params;
            xSemaphoreGive(dsp_params_mutex);
        } else {
            params = dsp_params_default;
        }

        // Compute crossover coefficients
        float a0, b1;
        compute_coeffs(params.crossover_hz, (float)SAMPLE_RATE, &a0, &b1);

        // Run crossover filter
        float l_low_prev  = filt_state[0];
        float r_low_prev  = filt_state[1];
        float l_high_prev = filt_state[2];
        float r_high_prev = filt_state[3];

        for (size_t i = 0; i < actual_frames; i++) {
            float l = (float)raw_buf[2*i];
            float r = (float)raw_buf[2*i + 1];

            // First-order low-pass
            float l_low = a0 * l + b1 * l_low_prev;
            float r_low = a0 * r + b1 * r_low_prev;
            // High-pass = original - low-pass
            float l_high = l - l_low;
            float r_high = r - r_low;

            l_low_prev  = l_low;
            r_low_prev  = r_low;
            l_high_prev = l_high;
            r_high_prev = r_high;

            // Apply gain + master volume
            float ll = l_low  * params.low_gain  * params.master_volume;
            float rl = r_low  * params.low_gain  * params.master_volume;
            float lh = l_high * params.high_gain * params.master_volume;
            float rh = r_high * params.high_gain * params.master_volume;

            // Clip to 16-bit range
            ll = fmaxf(-32768.0f, fminf(32767.0f, ll));
            rl = fmaxf(-32768.0f, fminf(32767.0f, rl));
            lh = fmaxf(-32768.0f, fminf(32767.0f, lh));
            rh = fmaxf(-32768.0f, fminf(32767.0f, rh));

            low_buf[2*i]     = (int16_t)ll;
            low_buf[2*i + 1] = (int16_t)rl;
            high_buf[2*i]    = (int16_t)lh;
            high_buf[2*i + 1]= (int16_t)rh;
        }

        // Save filter states
        filt_state[0] = l_low_prev;
        filt_state[1] = r_low_prev;
        filt_state[2] = l_high_prev;
        filt_state[3] = r_high_prev;

        // Write to both DACs
        size_t write_len = actual_frames * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_write(I2S1_NUM, low_buf,  write_len, &written, pdMS_TO_TICKS(50));
        i2s_write(I2S0_NUM, high_buf, write_len, &written, pdMS_TO_TICKS(50));
    }
}

// ═══════════════════════════════════════════════════════
// UI TASK  (Core 0 — ADS1115 + OLED)
// ═══════════════════════════════════════════════════════
static void ui_task(void */*param*/)
{
    Serial.println("UI task started on Core " + String(xPortGetCoreID()));

    while (1) {
        // ── Read ADS1115 pots ──
        dsp_params_t new_params;

        int16_t raw0 = ads.readADC_SingleEnded(0);
        new_params.master_volume = (float)raw0 / ADS_FULL_SCALE;
        new_params.master_volume = constrain(new_params.master_volume, 0.0f, 1.0f);

        int16_t raw1 = ads.readADC_SingleEnded(1);
        float norm1 = (float)raw1 / ADS_FULL_SCALE;
        norm1 = constrain(norm1, 0.0f, 1.0f);
        new_params.crossover_hz = CROSSOVER_MIN_HZ + norm1 * (CROSSOVER_MAX_HZ - CROSSOVER_MIN_HZ);

        int16_t raw2 = ads.readADC_SingleEnded(2);
        float norm2 = (float)raw2 / ADS_FULL_SCALE;
        norm2 = constrain(norm2, 0.0f, 1.0f);
        new_params.low_gain = norm2 * GAIN_MAX;

        int16_t raw3 = ads.readADC_SingleEnded(3);
        float norm3 = (float)raw3 / ADS_FULL_SCALE;
        norm3 = constrain(norm3, 0.0f, 1.0f);
        new_params.high_gain = norm3 * GAIN_MAX;

        // Update shared params
        if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_params = new_params;
            xSemaphoreGive(dsp_params_mutex);
        }

        // ── Update OLED ──
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
        display.printf("Vol:  %3.0f%%\n", new_params.master_volume * 100.0f);
        display.printf("Xov:  %.0f Hz\n", new_params.crossover_hz);
        display.printf("LoG:  %.2f\n", new_params.low_gain);
        display.printf("HiG:  %.2f\n", new_params.high_gain);

        // VU meter bar (simple — based on last raw reading)
        display.drawRect(0, 56, 128, 7, SSD1306_WHITE);
        int bar_width = (int)(new_params.master_volume * 124.0f);
        if (bar_width > 0)
            display.fillRect(2, 58, bar_width, 3, SSD1306_WHITE);

        display.display();

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
    init_i2s(I2S0_NUM, PIN_I2S0_BCK, PIN_I2S0_WS, PIN_I2S0_DATA);
    init_i2s(I2S1_NUM, PIN_I2S1_BCK, PIN_I2S1_WS, PIN_I2S1_DATA);
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
        // Configure ADS1115: ±4.096V range, 128 SPS, single-shot
        ads.setGain(GAIN_ONE);  // ±4.096V, 1 bit = 0.125mV
        Serial.println("[OK] ADS1115 initialised");
    }

    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_ADDRESS)) {
        Serial.println("[FAIL] SSD1306 OLED not found!");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 24);
        display.println("  ESP32-DSP");
        display.println("  Starting...");
        display.display();
        Serial.println("[OK] SSD1306 OLED initialised");
    }

    // ── Bluetooth A2DP sink ──
    // Route A2DP audio data to our ring buffer (not internal I2S)
    a2dp_sink.set_stream_reader(a2dp_data_cb, false);
    a2dp_sink.set_avrc_connection_state_callback(bt_connection_state_cb);
    a2dp_sink.set_on_audio_state_changed(bt_audio_state_cb);
    a2dp_sink.start(BLUETOOTH_NAME);
    Serial.printf("[OK] A2DP sink started as '%s'\n", BLUETOOTH_NAME);

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
    // Main work happens in tasks. Loop just does light diagnostics.
    static uint32_t last_print = 0;
    if (millis() - last_print > 5000) {
        last_print = millis();

        // Print free heap
        Serial.printf("[diag] Free heap: %u bytes | BT state: %d | Core: %d\n",
                      ESP.getFreeHeap(), bt_state, xPortGetCoreID());

        // Check ring buffer fill level
        if (audio_rb) {
            UBaseType_t items;
            vRingbufferGetInfo(audio_rb, NULL, NULL, NULL, NULL, &items);
            Serial.printf("[diag] Ring buffer items: %u\n", items);
        }
    }

    delay(100);
}
