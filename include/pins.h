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
// I²C Bus — shared between ADS1115 (×2), SSD1306 (OLED), EEPROM
// ═══════════════════════════════════════════════════════════════
//
// All I2C devices share the same bus (SDA + SCL).  The ESP32's Wire
// library handles clock stretching and arbitration automatically.
//
// 400 kHz Fast Mode is used for reasonable responsiveness without
// excessive noise sensitivity on breadboard/protoboard wiring.
//
// I2C device addresses:
//   0x3C — SSD1306 OLED (128×64)
//   0x48 — ADS1115 #1 (ADDR→GND) — Master vol, crossover, Lo-gain, Hi-gain
//   0x49 — ADS1115 #2 (ADDR→VDD) — EQ freq, EQ gain, EQ Q, exp pedal
//   0x50 — 24LC256 EEPROM (preset storage, optional)

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

// ═══════════════════════════════════════════════════════════════
// Rotary Encoder (with push button)
// ═══════════════════════════════════════════════════════════════
//
// The encoder uses two quadrature signals (A, B) and a momentary push
// switch.  Quadrature decoding is done via GPIO ISR or the ESP32's
// PCNT (pulse counter) peripheral for glitch-free operation.
//
// GPIO 34 and 35 are input-only pins — perfect for encoder channels.
// GPIO 17 is a regular GPIO used for the push button.

/// Encoder channel A (quadrature).  Input-only.
#define PIN_ENC_A       34

/// Encoder channel B (quadrature).  Input-only.
#define PIN_ENC_B       35

/// Encoder push button.  Active LOW with internal pull-up.
#define PIN_ENC_BTN     17

// ═══════════════════════════════════════════════════════════════
// Footswitches (momentary, active LOW)
// ═══════════════════════════════════════════════════════════════
//
// Four momentary switches for live control:
//   FS1 = Preset Next
//   FS2 = Preset Previous
//   FS3 = Mute toggle
//   FS4 = DSP bypass toggle
//
// All use internal pull-ups.  ISR-driven with software debounce.

/// Footswitch 1 — Preset Next (GPIO 32, input-only, INPUT_PULLUP).
#define PIN_FS_PRESET_NEXT   32

/// Footswitch 2 — Preset Previous (GPIO 33, input-only, INPUT_PULLUP).
#define PIN_FS_PRESET_PREV   33

/// Footswitch 3 — Mute Toggle (GPIO 36, input-only, INPUT_PULLUP).
#define PIN_FS_MUTE          36

/// Footswitch 4 — DSP Bypass Toggle (GPIO 39, input-only, INPUT_PULLUP).
#define PIN_FS_BYPASS        39

// ═══════════════════════════════════════════════════════════════
// MIDI Input (UART2)
// ═══════════════════════════════════════════════════════════════
//
// Standard 5-pin DIN MIDI input with 6N138 opto-isolator.
// MIDI data rate: 31250 baud.
//
// UART2 TX is not needed (MIDI is receive-only), freeing GPIO 17
// for the encoder push button.  RX uses GPIO 16... but GPIO 16 is
// I2S1 WS.  We use GPIO 20 (or GPIO 3 for RX0 if debug UART is
// disabled in production) instead.
//
// MIDI RX pin — choose a free pin.  GPIO 20 is available.
// MIDI TX pin — unused (MIDI input only).

/// MIDI UART RX pin (GPIO 20).  UART2.
#define PIN_MIDI_RX          20

/// MIDI UART TX pin — unused (defined for completeness only).
#define PIN_MIDI_TX          -1

/// MIDI UART port number.
#define MIDI_UART_PORT       UART_NUM_2

// ═══════════════════════════════════════════════════════════════
// SPI Bus (optional upgrade display)
// ═══════════════════════════════════════════════════════════════
//
// VSPI bus for an optional TFT or SPI OLED display.
// Only initialised if USE_SPI_DISPLAY is defined at build time.

/// SPI SCK (VSPI clock, GPIO 18).
#define PIN_SPI_SCK          18

/// SPI MOSI (VSPI data out, GPIO 19).
#define PIN_SPI_MOSI         19

/// SPI CS (GPIO 5).  Active LOW.
#define PIN_SPI_CS           5

/// Display data/command select (GPIO 15 — strapping pin, use carefully).
/// Only used as fixed HIGH/LOW after boot for SPI displays.
#define PIN_SPI_DC           15

// ═══════════════════════════════════════════════════════════════
// Pin Summary — Quick Reference
// ═══════════════════════════════════════════════════════════════
//
// USED GPIOs:
//   4   — DAC high-band mute (output)
//   13  — I2S1 data (woofer)
//   14  — I2S1 BCK
//   15  — SPI DC (optional, strapping — fixed level after boot)
//   16  — I2S1 WS
//   17  — Encoder push button (input)
//   18  — SPI SCK (optional display)
//   19  — SPI MOSI (optional display)
//   20  — MIDI RX (UART2)
//   21  — I2C SDA (OLED + ADCs + EEPROM)
//   22  — I2C SCL
//   23  — I2S0 data (tweeter)
//   25  — I2S0 BCK
//   26  — I2S0 WS
//   27  — DAC low-band mute (output)
//   32  — Footswitch: preset next (input)
//   33  — Footswitch: preset prev (input)
//   34  — Encoder A (input-only)
//   35  — Encoder B (input-only)
//   36  — Footswitch: mute (input-only)
//   39  — Footswitch: bypass (input-only)
//
// AVOID (strapping pins — do not drive as outputs):
//   0, 2, 12
