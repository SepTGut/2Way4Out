/**
 * @file pins.h
 * @brief Physical GPIO pin assignments for the ESP32-WROOM-32 / DevKit V4.
 *
 * This header maps logical function names (e.g. I2S0 clock pin) to
 * actual ESP32 GPIO numbers.  All peripheral drivers reference these
 * macros so that hardware changes only require edits in this one file.
 *
 * ⚠️  Strapping pins (GPIO 0, 2, 4, 12, 15) must NOT be used as
 *     general-purpose outputs because their state at boot determines
 *     critical chip configuration (flash voltage, boot mode, etc.).
 *     Using them incorrectly can prevent the board from booting.
 *
 * I2S routing note: on the ESP32, I2S BCK and WS signals can be routed
 * to ANY GPIO via the GPIO matrix.  We deliberately choose non-strapping
 * pins to avoid boot issues.
 */

#pragma once

// ═══════════════════════════════════════════════════════════════
// I²C Bus — shared between ADS1115 (ADC) and SSD1306 (OLED)
// ═══════════════════════════════════════════════════════════════
//
// Both the ADS1115 and SSD1306 communicate over I2C and share the same
// bus (SDA + SCL).  The ESP32's Wire library handles clock stretching
// and arbitration automatically.
//
// 400 kHz Fast Mode is used for reasonable responsiveness without
// excessive noise sensitivity on breadboard/protoboard wiring.

/// I2C data line (GPIO 21 = default SDA on most ESP32 boards).
#define PIN_I2C_SDA  21

/// I2C clock line (GPIO 22 = default SCL on most ESP32 boards).
#define PIN_I2C_SCL  22

// ═══════════════════════════════════════════════════════════════
// CS4344 DAC Mute Pins
// ═══════════════════════════════════════════════════════════════
//
// The CS4344 has an active-low mute input.  During initialization the
// DACs are held muted (LOW) to prevent pops/clicks, then unmuted (HIGH)
// once I2S is running and DMA buffers are primed with silence.
//
// Two separate mute pins are used — one per DAC — so each output can be
// independently controlled if needed in the future.
//
// GPIO 4  — chosen because it's near the DACs on our board layout and
//           is NOT a strapping pin (unlike GPIO 0, 2, 12, 15).
// GPIO 27 — another safe non-strapping GPIO.

/// Mute pin for the high-band CS4344 (I2S0 output).  LOW = muted.
#define PIN_DAC_HIGH_MUTE  GPIO_NUM_4

/// Mute pin for the low-band CS4344 (I2S1 output).  LOW = muted.
#define PIN_DAC_LOW_MUTE   GPIO_NUM_27

// ═══════════════════════════════════════════════════════════════
// I²S0 — High-Band DAC (CS4344 #1) → drives the tweeter amplifier
// ═══════════════════════════════════════════════════════════════
//
// Signals:
//   BCK  (Bit Clock)     — serial clock for the I2S data line
//   WS   (Word Select)   — left/right channel select (LRCK)
//   DATA (Serial Data)   — the actual PCM audio data stream
//
// The CS4344 requires only these 3 signals — it is slave-mode only and
// derives its internal MCLK from the I2S clock, so no separate MCLK
// GPIO is needed.

/// I2S port number for the high-band DAC output.
#define I2S0_NUM       I2S_NUM_0

/// I2S0 bit clock pin (GPIO 25).
#define PIN_I2S0_BCK   25

/// I2S0 word-select / LRCK pin (GPIO 26).
#define PIN_I2S0_WS    26

/// I2S0 serial data output pin (GPIO 23).
#define PIN_I2S0_DATA  23

// ═══════════════════════════════════════════════════════════════
// I²S1 — Low-Band DAC (CS4344 #2) → drives the woofer amplifier
// ═══════════════════════════════════════════════════════════════
//
// Same signal definitions as I2S0.  Using a separate I2S peripheral
// allows independent DMA channels and avoids contention between the
// two audio streams.
//
// Pin selection notes:
//   - GPIO 14 (BCK): safe, not a strapping pin.
//   - GPIO 16 (WS):  was originally GPIO 15, but GPIO 15 is a strapping
//                   pin (MTDO) that must be LOW at boot.  Moved to GPIO 16.
//   - GPIO 13 (DATA): safe, not a strapping pin.

/// I2S port number for the low-band DAC output.
#define I2S1_NUM       I2S_NUM_1

/// I2S1 bit clock pin (GPIO 14).
#define PIN_I2S1_BCK   14

/// I2S1 word-select / LRCK pin (GPIO 16 — moved from strapping pin GPIO 15).
#define PIN_I2S1_WS    16

/// I2S1 serial data output pin (GPIO 13).
#define PIN_I2S1_DATA  13
