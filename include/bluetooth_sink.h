/**
 * @file bluetooth_sink.h
 * @brief Public API for the Bluetooth A2DP sink.
 *
 * This module wraps the ESP32-A2DP library (by Phil Schatzmann) to create
 * a Bluetooth audio receiver.  Once a phone connects and starts streaming,
 * raw PCM audio data flows into the ring buffer where the DSP task picks
 * it up for crossover processing and DAC output.
 *
 * Data flow:
 *   Phone ──(A2DP)──► ESP32 Bluetooth stack ──► a2dp_data_cb()
 *                     ──► xRingbufferSend(audio_rb)
 *                     ──► dsp_task() reads from audio_rb
 *
 * The connection state callbacks update bt_state so the UI can show
 * the current Bluetooth status on the OLED.
 */

#pragma once

#include "BluetoothA2DPSink.h"
#include "dsp_config.h"

// ─────────────────────────────────────────────────────────────
// A2DP sink instance
// ─────────────────────────────────────────────────────────────
// The BluetoothA2DPSink object from the ESP32-A2DP library.
// 'extern' here; defined in bluetooth_sink.cpp.
extern BluetoothA2DPSink a2dp_sink;

/// Bluetooth device name visible to the phone during pairing.
extern const char *BT_DEVICE_NAME;

/**
 * @brief Initialise the Bluetooth A2DP sink.
 *
 * This function:
 *   1. Registers the data callback (feeds incoming audio to the ring buffer).
 *   2. Registers connection-state callbacks (updates bt_state for the UI).
 *   3. Starts the A2DP sink with the configured device name.
 *
 * After this call the ESP32 is discoverable and pairable.
 * Called once from setup() after I2C is initialised.
 */
void bt_init();
