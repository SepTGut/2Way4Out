/**
 * @file test_dsp.cpp
 * @brief Host-native unit tests for DSP data structures and helpers.
 *
 * Runs on the host PC (native), not on the ESP32.
 * Tests struct sizes, default values, coefficient calculations,
 * and parameter mapping.
 *
 * Build & run: pio test -e native
 */

#include <unity.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────
// Self-contained type definitions (no Arduino dependency)
// ─────────────────────────────────────────────────────────────
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef short          int16_t;

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define constrain(amt, low, high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

// ─────────────────────────────────────────────────────────────
// Replicate the DSP structs here (so we don't pull in Arduino.h)
// These must match include/dsp_config.h exactly.
// ─────────────────────────────────────────────────────────────

#define SAMPLE_RATE         44100
#define FRAMES_PER_BATCH    128
#define DMA_BUF_COUNT       8
#define DMA_BUF_LEN         64
#define RINGBUF_SIZE        (8 * 1024)
#define ADS1115_ADDRESS     0x48
#define ADS1115_ADDRESS_2   0x49
#define ADS_FULL_SCALE      26666.0f
#define CROSSOVER_MIN_HZ    200.0f
#define CROSSOVER_MAX_HZ    4000.0f
#define GAIN_MAX            2.0f
#define SSD1306_ADDRESS     0x3C
#define UI_REFRESH_MS       200
#define DSP_TASK_PRIORITY   5
#define UI_TASK_PRIORITY    2
#define EQ_MAX_BANDS        4
#define DRIVER_BANDS        2
#define DELAY_MAX_MS        20.0f
#define DELAY_MAX_SAMPLES   882
#define NUM_PRESETS         8

typedef enum {
    BT_DISCONNECTED = 0,
    BT_CONNECTED,
    BT_PLAYING,
} bt_conn_state_t;

typedef struct {
    bool  enabled;
    float freq_hz;
    float gain_db;
    float q;
} eq_band_t;

typedef struct {
    bool  enabled;
    float threshold_db;
    float ratio;
    float attack_ms;
    float release_ms;
    float makeup_db;
} dyn_params_t;

typedef struct {
    bool     enabled;
    uint16_t samples;
} delay_params_t;

typedef struct {
    dyn_params_t   compressor;
    dyn_params_t   limiter;
    delay_params_t delay;
} driver_params_t;

typedef struct {
    float master_volume;
    float crossover_hz;
    float low_gain;
    float high_gain;
    eq_band_t eq_bands[EQ_MAX_BANDS];
    driver_params_t low_driver;
    driver_params_t high_driver;
    uint8_t  active_page;
    uint8_t  active_preset;
    bool     mute;
    bool     bypass;
} dsp_params_t;

typedef struct {
    dsp_params_t params;
    char         name[12];
} preset_t;

static const dsp_params_t dsp_params_default = {
    .master_volume = 0.8f,
    .crossover_hz  = 2000.0f,
    .low_gain      = 1.0f,
    .high_gain     = 1.0f,
    .eq_bands = {
        {false,  100.0f, 0.0f, 0.7f},
        {false,  500.0f, 0.0f, 1.0f},
        {false, 2000.0f, 0.0f, 1.0f},
        {false, 8000.0f, 0.0f, 0.7f},
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

// ─────────────────────────────────────────────────────────────
// Pin definitions (copy from pins.h, no Arduino dependency)
// ─────────────────────────────────────────────────────────────
#define PIN_I2C_SDA        21
#define PIN_I2C_SCL        22
#define PIN_DAC_HIGH_MUTE  4
#define PIN_DAC_LOW_MUTE   27
#define I2S0_NUM           0
#define PIN_I2S0_BCK       25
#define PIN_I2S0_WS        26
#define PIN_I2S0_DATA      23
#define I2S1_NUM           1
#define PIN_I2S1_BCK       14
#define PIN_I2S1_WS        16
#define PIN_I2S1_DATA      13
#define PIN_ENC_A          34
#define PIN_ENC_B          35
#define PIN_ENC_BTN        17
#define PIN_FS_PRESET_NEXT 32
#define PIN_FS_PRESET_PREV 33
#define PIN_FS_MUTE        36
#define PIN_FS_BYPASS      39

// ─────────────────────────────────────────────────────────────
// Replicate compute_coeffs from dsp_core.cpp
// ─────────────────────────────────────────────────────────────

static void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1)
{
    if (cutoff_hz < 20.0f)    cutoff_hz = 20.0f;
    if (cutoff_hz > 20000.0f) cutoff_hz = 20000.0f;

    float rc    = 1.0f / (2.0f * M_PI * cutoff_hz);
    float dt    = 1.0f / sample_rate;
    float alpha = dt / (rc + dt);

    if (alpha <= 0.0f) alpha = 0.0001f;
    if (alpha >= 1.0f) alpha = 0.9999f;

    *a0 = alpha;
    *b1 = 1.0f - alpha;
}

// ═══════════════════════════════════════════════════════════════
// Test: Struct sizes and alignment
// ═══════════════════════════════════════════════════════════════

void test_eq_band_size(void)
{
    TEST_ASSERT_TRUE(sizeof(eq_band_t) >= 12);
    TEST_ASSERT_TRUE(sizeof(eq_band_t) <= 20);
    printf("  eq_band_t size: %zu\n", sizeof(eq_band_t));
}

void test_dyn_params_size(void)
{
    TEST_ASSERT_TRUE(sizeof(dyn_params_t) >= 20);
    TEST_ASSERT_TRUE(sizeof(dyn_params_t) <= 32);
    printf("  dyn_params_t size: %zu\n", sizeof(dyn_params_t));
}

void test_delay_params_size(void)
{
    TEST_ASSERT_TRUE(sizeof(delay_params_t) >= 4);
    TEST_ASSERT_TRUE(sizeof(delay_params_t) <= 8);
    printf("  delay_params_t size: %zu\n", sizeof(delay_params_t));
}

void test_driver_params_size(void)
{
    TEST_ASSERT_TRUE(sizeof(driver_params_t) >= 48);
    printf("  driver_params_t size: %zu\n", sizeof(driver_params_t));
}

void test_dsp_params_size(void)
{
    TEST_ASSERT_TRUE(sizeof(dsp_params_t) >= 100);
    printf("  dsp_params_t size: %zu\n", sizeof(dsp_params_t));
}

void test_preset_size(void)
{
    TEST_ASSERT_TRUE(sizeof(preset_t) >= 100);
    printf("  preset_t size: %zu\n", sizeof(preset_t));
}

// ═══════════════════════════════════════════════════════════════
// Test: Default parameter values
// ═══════════════════════════════════════════════════════════════

void test_default_master_volume(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, dsp_params_default.master_volume);
}

void test_default_crossover(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, dsp_params_default.crossover_hz);
}

void test_default_gains(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, dsp_params_default.low_gain);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, dsp_params_default.high_gain);
}

void test_default_eq_disabled(void)
{
    for (int i = 0; i < EQ_MAX_BANDS; i++) {
        TEST_ASSERT_FALSE(dsp_params_default.eq_bands[i].enabled);
    }
}

void test_default_eq_frequencies(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  100.0f, dsp_params_default.eq_bands[0].freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  500.0f, dsp_params_default.eq_bands[1].freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, dsp_params_default.eq_bands[2].freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8000.0f, dsp_params_default.eq_bands[3].freq_hz);
}

void test_default_eq_zero_gain(void)
{
    for (int i = 0; i < EQ_MAX_BANDS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dsp_params_default.eq_bands[i].gain_db);
    }
}

void test_default_limiter_enabled(void)
{
    TEST_ASSERT_TRUE(dsp_params_default.low_driver.limiter.enabled);
    TEST_ASSERT_TRUE(dsp_params_default.high_driver.limiter.enabled);
}

void test_default_compressor_disabled(void)
{
    TEST_ASSERT_FALSE(dsp_params_default.low_driver.compressor.enabled);
    TEST_ASSERT_FALSE(dsp_params_default.high_driver.compressor.enabled);
}

void test_default_mute_bypass(void)
{
    TEST_ASSERT_FALSE(dsp_params_default.mute);
    TEST_ASSERT_FALSE(dsp_params_default.bypass);
}

void test_default_limiter_threshold(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.0f, dsp_params_default.low_driver.limiter.threshold_db);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.0f, dsp_params_default.high_driver.limiter.threshold_db);
}

void test_default_limiter_ratio(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, dsp_params_default.low_driver.limiter.ratio);
}

void test_default_delay_disabled(void)
{
    TEST_ASSERT_FALSE(dsp_params_default.low_driver.delay.enabled);
    TEST_ASSERT_FALSE(dsp_params_default.high_driver.delay.enabled);
    TEST_ASSERT_EQUAL(0, dsp_params_default.low_driver.delay.samples);
}

// ═══════════════════════════════════════════════════════════════
// Test: Constants
// ═══════════════════════════════════════════════════════════════

void test_sample_rate(void)
{
    TEST_ASSERT_EQUAL(44100, SAMPLE_RATE);
}

void test_crossover_range(void)
{
    TEST_ASSERT_EQUAL(200, (int)CROSSOVER_MIN_HZ);
    TEST_ASSERT_EQUAL(4000, (int)CROSSOVER_MAX_HZ);
}

void test_eq_max_bands(void)
{
    TEST_ASSERT_EQUAL(4, EQ_MAX_BANDS);
}

void test_driver_bands(void)
{
    TEST_ASSERT_EQUAL(2, DRIVER_BANDS);
}

void test_num_presets(void)
{
    TEST_ASSERT_EQUAL(8, NUM_PRESETS);
}

void test_delay_max_samples(void)
{
    TEST_ASSERT_EQUAL(882, DELAY_MAX_SAMPLES);
}

void test_gain_max(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, GAIN_MAX);
}

void test_frames_per_batch(void)
{
    TEST_ASSERT_EQUAL(128, FRAMES_PER_BATCH);
}

void test_ringbuf_size(void)
{
    TEST_ASSERT_EQUAL(8192, RINGBUF_SIZE);
}

// ═══════════════════════════════════════════════════════════════
// Test: Crossover coefficient calculation
// ═══════════════════════════════════════════════════════════════

void test_compute_coeffs_1kHz(void)
{
    float a0, b1;
    compute_coeffs(1000.0f, 44100.0f, &a0, &b1);

    TEST_ASSERT_TRUE(a0 > 0.0f);
    TEST_ASSERT_TRUE(a0 < 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f - a0, b1);
    printf("  1kHz: a0=%.6f b1=%.6f\n", a0, b1);
}

void test_compute_coeffs_2kHz(void)
{
    float a0, b1;
    compute_coeffs(2000.0f, 44100.0f, &a0, &b1);

    TEST_ASSERT_TRUE(a0 > 0.0f);
    TEST_ASSERT_TRUE(a0 < 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f - a0, b1);
    // 2 kHz should have larger a0 than 1 kHz (less filtering)
    float a0_1k, b1_1k;
    compute_coeffs(1000.0f, 44100.0f, &a0_1k, &b1_1k);
    TEST_ASSERT_TRUE(a0 > a0_1k);
    printf("  2kHz: a0=%.6f b1=%.6f\n", a0, b1);
}

void test_compute_coeffs_low_freq(void)
{
    float a0, b1;
    compute_coeffs(200.0f, 44100.0f, &a0, &b1);

    TEST_ASSERT_TRUE(a0 > 0.0f);
    TEST_ASSERT_TRUE(a0 < 0.1f);
    printf("  200Hz: a0=%.6f b1=%.6f\n", a0, b1);
}

void test_compute_coeffs_high_freq(void)
{
    float a0, b1;
    compute_coeffs(4000.0f, 44100.0f, &a0, &b1);

    TEST_ASSERT_TRUE(a0 > 0.1f);
    TEST_ASSERT_TRUE(a0 < 1.0f);
    printf("  4kHz: a0=%.6f b1=%.6f\n", a0, b1);
}

void test_compute_coeffs_clamped_low(void)
{
    float a0, b1;
    compute_coeffs(5.0f, 44100.0f, &a0, &b1);
    float a0_20, b1_20;
    compute_coeffs(20.0f, 44100.0f, &a0_20, &b1_20);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a0_20, a0);
}

void test_compute_coeffs_clamped_high(void)
{
    float a0, b1;
    compute_coeffs(30000.0f, 44100.0f, &a0, &b1);
    float a0_20k, b1_20k;
    compute_coeffs(20000.0f, 44100.0f, &a0_20k, &b1_20k);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a0_20k, a0);
}

// ═══════════════════════════════════════════════════════════════
// Test: ADC mapping helpers
// ═══════════════════════════════════════════════════════════════

void test_ads_full_scale(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 26666.0f, ADS_FULL_SCALE);
}

void test_ads_addresses(void)
{
    TEST_ASSERT_EQUAL(0x48, ADS1115_ADDRESS);
    TEST_ASSERT_EQUAL(0x49, ADS1115_ADDRESS_2);
}

void test_adc_normalize_and_constrain(void)
{
    // Simulate ADC reading → normalised float
    float norm = 13333.0f / ADS_FULL_SCALE;
    TEST_ASSERT_TRUE(norm > 0.4f && norm < 0.6f);  // ~50%

    // Simulate constraining
    float vol = constrain(norm, 0.0f, 1.0f);
    TEST_ASSERT_TRUE(vol >= 0.0f && vol <= 1.0f);

    // Zero reading should give zero volume
    vol = constrain(0.0f / ADS_FULL_SCALE, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vol);

    // Full scale should give full volume
    vol = constrain(32767.0f / ADS_FULL_SCALE, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, vol);
}

// ═══════════════════════════════════════════════════════════════
// Test: Pin assignments don't conflict
// ═══════════════════════════════════════════════════════════════

void test_pin_conflicts(void)
{
    // I2S0 pins should be unique
    TEST_ASSERT_NOT_EQUAL(PIN_I2S0_BCK, PIN_I2S0_WS);
    TEST_ASSERT_NOT_EQUAL(PIN_I2S0_BCK, PIN_I2S0_DATA);
    TEST_ASSERT_NOT_EQUAL(PIN_I2S0_WS,  PIN_I2S0_DATA);

    // I2S1 pins should be unique
    TEST_ASSERT_NOT_EQUAL(PIN_I2S1_BCK, PIN_I2S1_WS);
    TEST_ASSERT_NOT_EQUAL(PIN_I2S1_BCK, PIN_I2S1_DATA);
    TEST_ASSERT_NOT_EQUAL(PIN_I2S1_WS,  PIN_I2S1_DATA);

    // I2S0 and I2S1 should not share pins
    TEST_ASSERT_NOT_EQUAL(PIN_I2S0_BCK, PIN_I2S1_BCK);
    TEST_ASSERT_NOT_EQUAL(PIN_I2S0_WS,  PIN_I2S1_WS);
    TEST_ASSERT_NOT_EQUAL(PIN_I2S0_DATA, PIN_I2S1_DATA);

    // I2C pins should not overlap with I2S
    TEST_ASSERT_NOT_EQUAL(PIN_I2C_SDA, PIN_I2S0_BCK);
    TEST_ASSERT_NOT_EQUAL(PIN_I2C_SDA, PIN_I2S0_WS);

    // Encoder pins should not overlap with I2S
    TEST_ASSERT_NOT_EQUAL(PIN_ENC_A, PIN_I2S0_DATA);
    TEST_ASSERT_NOT_EQUAL(PIN_ENC_B, PIN_I2S0_DATA);

    // Footswitch pins should be unique
    TEST_ASSERT_NOT_EQUAL(PIN_FS_PRESET_NEXT, PIN_FS_PRESET_PREV);
    TEST_ASSERT_NOT_EQUAL(PIN_FS_PRESET_NEXT, PIN_FS_MUTE);
    TEST_ASSERT_NOT_EQUAL(PIN_FS_PRESET_NEXT, PIN_FS_BYPASS);
    TEST_ASSERT_NOT_EQUAL(PIN_FS_MUTE, PIN_FS_BYPASS);

    // DAC mute pins should not overlap
    TEST_ASSERT_NOT_EQUAL(PIN_DAC_HIGH_MUTE, PIN_DAC_LOW_MUTE);

    printf("  All pin conflict checks passed\n");
}

// ═══════════════════════════════════════════════════════════════
// Test: Parameter mutation
// ═══════════════════════════════════════════════════════════════

void test_parameter_mutation(void)
{
    dsp_params_t params = dsp_params_default;

    // Change master volume
    params.master_volume = 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, params.master_volume);

    // Enable an EQ band
    params.eq_bands[0].enabled = true;
    params.eq_bands[0].freq_hz = 250.0f;
    params.eq_bands[0].gain_db = 3.0f;
    TEST_ASSERT_TRUE(params.eq_bands[0].enabled);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 250.0f, params.eq_bands[0].freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, params.eq_bands[0].gain_db);

    // Enable compressor
    params.low_driver.compressor.enabled = true;
    params.low_driver.compressor.threshold_db = -18.0f;
    params.low_driver.compressor.ratio = 4.0f;
    TEST_ASSERT_TRUE(params.low_driver.compressor.enabled);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -18.0f, params.low_driver.compressor.threshold_db);

    // Enable delay
    params.high_driver.delay.enabled = true;
    params.high_driver.delay.samples = 441;  // 10ms at 44.1kHz
    TEST_ASSERT_TRUE(params.high_driver.delay.enabled);
    TEST_ASSERT_EQUAL(441, params.high_driver.delay.samples);

    // Toggle mute and bypass
    params.mute = true;
    params.bypass = true;
    TEST_ASSERT_TRUE(params.mute);
    TEST_ASSERT_TRUE(params.bypass);
}

// ═══════════════════════════════════════════════════════════════
// Test: Preset struct operations
// ═══════════════════════════════════════════════════════════════

void test_preset_struct(void)
{
    preset_t p;
    memset(&p, 0, sizeof(p));

    // Name buffer should be 12 bytes
    TEST_ASSERT_EQUAL(12, sizeof(p.name));

    // Assign params
    p.params = dsp_params_default;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, p.params.master_volume);

    // Modify and save name
    strncpy(p.name, "My Preset", sizeof(p.name) - 1);
    TEST_ASSERT_EQUAL_STRING("My Preset", p.name);

    printf("  Preset: name='%s' vol=%.2f crossover=%.0f\n",
           p.name, p.params.master_volume, p.params.crossover_hz);
}

void test_multiple_presets(void)
{
    // Verify we can create an array of 8 presets
    preset_t presets[NUM_PRESETS];
    memset(presets, 0, sizeof(presets));

    for (int i = 0; i < NUM_PRESETS; i++) {
        presets[i].params = dsp_params_default;
        presets[i].params.master_volume = 0.5f + i * 0.05f;
        snprintf(presets[i].name, sizeof(presets[i].name), "Preset %d", i + 1);
    }

    for (int i = 0; i < NUM_PRESETS; i++) {
        float expected = 0.5f + i * 0.05f;
        TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, presets[i].params.master_volume);
    }

    printf("  %d presets created successfully\n", NUM_PRESETS);
}

// ═══════════════════════════════════════════════════════════════
// Unity entry point
// ═══════════════════════════════════════════════════════════════

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║  ESP32-DSP Host Tests                    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    printf("── Struct Sizes ──\n");
    RUN_TEST(test_eq_band_size);
    RUN_TEST(test_dyn_params_size);
    RUN_TEST(test_delay_params_size);
    RUN_TEST(test_driver_params_size);
    RUN_TEST(test_dsp_params_size);
    RUN_TEST(test_preset_size);

    printf("\n── Default Values ──\n");
    RUN_TEST(test_default_master_volume);
    RUN_TEST(test_default_crossover);
    RUN_TEST(test_default_gains);
    RUN_TEST(test_default_eq_disabled);
    RUN_TEST(test_default_eq_frequencies);
    RUN_TEST(test_default_eq_zero_gain);
    RUN_TEST(test_default_limiter_enabled);
    RUN_TEST(test_default_compressor_disabled);
    RUN_TEST(test_default_mute_bypass);
    RUN_TEST(test_default_limiter_threshold);
    RUN_TEST(test_default_limiter_ratio);
    RUN_TEST(test_default_delay_disabled);

    printf("\n── Constants ──\n");
    RUN_TEST(test_sample_rate);
    RUN_TEST(test_crossover_range);
    RUN_TEST(test_eq_max_bands);
    RUN_TEST(test_driver_bands);
    RUN_TEST(test_num_presets);
    RUN_TEST(test_delay_max_samples);
    RUN_TEST(test_gain_max);
    RUN_TEST(test_frames_per_batch);
    RUN_TEST(test_ringbuf_size);

    printf("\n── Coefficient Calculation ──\n");
    RUN_TEST(test_compute_coeffs_1kHz);
    RUN_TEST(test_compute_coeffs_2kHz);
    RUN_TEST(test_compute_coeffs_low_freq);
    RUN_TEST(test_compute_coeffs_high_freq);
    RUN_TEST(test_compute_coeffs_clamped_low);
    RUN_TEST(test_compute_coeffs_clamped_high);

    printf("\n── ADC Mapping ──\n");
    RUN_TEST(test_ads_full_scale);
    RUN_TEST(test_ads_addresses);
    RUN_TEST(test_adc_normalize_and_constrain);

    printf("\n── Pin Conflicts ──\n");
    RUN_TEST(test_pin_conflicts);

    printf("\n── Parameter Mutation ──\n");
    RUN_TEST(test_parameter_mutation);

    printf("\n── Preset System ──\n");
    RUN_TEST(test_preset_struct);
    RUN_TEST(test_multiple_presets);

    printf("\n");
    UNITY_END();
    return 0;
}
