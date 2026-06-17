/**
 * @file bluetooth_sink.cpp
 * @brief Bluetooth A2DP sink implementation — receives audio from a phone
 *        and forwards it to the DSP pipeline via the ring buffer.
 *
 * Architecture:
 *   The ESP32 Bluetooth stack delivers PCM audio chunks via the
 *   a2dp_data_cb() callback.  This callback runs in a high-priority
 *   Bluetooth task context, so it must return quickly — it just copies
 *   the data into the ring buffer and returns.
 *
 *   The DSP task (on Core 1) reads from the ring buffer at its own pace,
 *   processes the audio through the crossover filter, and writes the
 *   results to the two CS4344 DACs via I2S.
 *
 *   Connection state transitions are tracked via bt_state so the UI task
 *   can display "Disconnected" / "Connected" / "Playing" on the OLED.
 */

#include "bluetooth_sink.h"
#include "globals.h"
#include <Arduino.h>

// ── A2DP sink instance definition ────────────────────────────
BluetoothA2DPSink a2dp_sink;
const char *BT_DEVICE_NAME = "esp32DSP";

// ─────────────────────────────────────────────────────────────
// Audio data callback
// ─────────────────────────────────────────────────────────────
// Called by the ESP32 Bluetooth stack whenever a new chunk of PCM
// audio arrives from the phone.  The data is 16-bit stereo (interleaved
// left/right) at 44.1 kHz.
//
// This callback MUST be fast — it runs in the Bluetooth task context
// and blocking here would cause audio glitches or Bluetooth disconnection.
// We simply push the raw bytes into the ring buffer; the DSP task will
// consume them later.
//
// If the ring buffer is full (e.g. DSP task is not keeping up), the data
// is silently dropped (xRingbufferSend timeout = 0).  A small amount of
// drop is acceptable; the DSP task runs at high priority and reads quickly.

static void a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    if (audio_rb == NULL) return;  // ring buffer not yet created
    xRingbufferSend(audio_rb, data, len, 0);  // 0 = no wait if full
}

// ─────────────────────────────────────────────────────────────
// Bluetooth connection state callback
// ─────────────────────────────────────────────────────────────
// Called when a phone connects or disconnects from the ESP32.
// We update bt_state so the UI task can reflect the change on the OLED.

static void bt_conn_state_cb(bool connected)
{
    bt_state = connected ? BT_CONNECTED : BT_DISCONNECTED;
}

// ─────────────────────────────────────────────────────────────
// Audio streaming state callback
// ─────────────────────────────────────────────────────────────
// Called when the audio stream starts (play/pause on the phone).
// We distinguish between "Connected but not streaming" and "Playing"
// so the user gets accurate feedback on the OLED display.

static void bt_audio_state_cb(esp_a2d_audio_state_t state, void *)
{
    if (state == ESP_A2D_AUDIO_STATE_STARTED) {
        bt_state = BT_PLAYING;
    } else if (state == ESP_A2D_AUDIO_STATE_STOPPED ||
               state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        // If we were playing and now stopped, go back to "Connected"
        // (not "Disconnected" — the Bluetooth link is still active).
        if (bt_state == BT_PLAYING) bt_state = BT_CONNECTED;
    }
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

void bt_init()
{
    // Register the audio data callback.  The second parameter (false)
    // means we do NOT want the library to use its internal I2S output —
    // we handle I2S ourselves via the DSP task.
    a2dp_sink.set_stream_reader(a2dp_data_cb, false);

    // Register connection and audio state callbacks for UI feedback.
    a2dp_sink.set_avrc_connection_state_callback(bt_conn_state_cb);
    a2dp_sink.set_on_audio_state_changed(bt_audio_state_cb);

    // Start the A2DP sink.  The ESP32 becomes discoverable and
    // pairable under the name "esp32DSP".
    a2dp_sink.start(BT_DEVICE_NAME);
    Serial.printf("[OK] A2DP sink started as '%s'\n", BT_DEVICE_NAME);
}
