#pragma once

// ──────────────────────────────────────────────
// ESP32-WROOM-32 / DevKit V4 pin assignments
// ──────────────────────────────────────────────
//
// Strapping pins to avoid for outputs:
//   GPIO0  — BOOT (must be HIGH to boot)
//   GPIO2  — BOOT (must be LOW to boot)
//   GPIO4  — (ok but weak pull-down)
//   GPIO12 — internal VDD_SDIO (must be LOW at boot for 3.3V flash)
//   GPIO15 — MTDO (must be LOW at boot)
//
// I2S1 on ESP32: BCK and WS can be routed to any GPIO via GPIO matrix.
// We avoid strapping pins for I2S1 signals.

// ──────────────────────────────────────────────
// I2C pins (shared for ADS1115 and OLED)
// ──────────────────────────────────────────────
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

// ──────────────────────────────────────────────
// CS4344 DAC mute pins (active low = muted)
// Avoid strapping pins (GPIO0, GPIO2, GPIO12, GPIO15)
// ──────────────────────────────────────────────
#define PIN_DAC_HIGH_MUTE GPIO_NUM_4
#define PIN_DAC_LOW_MUTE  GPIO_NUM_27

// ──────────────────────────────────────────────
// I2S0 – high‑band DAC (CS4344 #1)
// ──────────────────────────────────────────────
#define I2S0_NUM         I2S_NUM_0
#define PIN_I2S0_BCK     25
#define PIN_I2S0_WS      26
#define PIN_I2S0_DATA    23

// ──────────────────────────────────────────────
// I2S1 – low‑band DAC (CS4344 #2)
// ──────────────────────────────────────────────
// NOTE: GPIO15 is a strapping pin — moved WS to GPIO16
// NOTE: GPIO12 is a strapping pin — avoid for outputs
#define I2S1_NUM         I2S_NUM_1
#define PIN_I2S1_BCK     14
#define PIN_I2S1_WS      16
#define PIN_I2S1_DATA    13
