/**
 * @file web_portal.h
 * @brief Public API for the WiFi web portal.
 *
 * The web portal runs an ESPAsyncWebServer on Core 0 that serves a
 * single-page web app for controlling all DSP parameters from any
 * browser (phone or desktop).
 *
 * Audio input remains Bluetooth A2DP — the web portal is control-only.
 * All parameter writes go through dsp_params_mutex for thread safety.
 *
 * WiFi mode: AP+STA (creates its own AP AND connects to a known network
 * if credentials are configured).
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>

// ═══════════════════════════════════════════════════════════════
// WiFi Configuration
// ═══════════════════════════════════════════════════════════════

/// WiFi AP name (SSID) the ESP32 creates for direct connection.
#define WIFI_AP_SSID     "ESP32-DSP"

/// WiFi AP password. Minimum 8 characters.
#define WIFI_AP_PASSWORD "dsp12345"

/// WiFi AP channel (1-13).
#define WIFI_AP_CHANNEL  1

/// Maximum number of stations that can connect to the AP.
#define WIFI_AP_MAX_CONN 4

/// Connect to an existing WiFi network as a station (set to "" to disable).
/// When set, the ESP32 will try to connect to this network on boot.
#define WIFI_STA_SSID     ""

/// Password for the station network.
#define WIFI_STA_PASSWORD ""

/// Timeout for station connection attempt (milliseconds).
#define WIFI_STA_TIMEOUT_MS 10000

// ═══════════════════════════════════════════════════════════════
// Web Server Configuration
// ═══════════════════════════════════════════════════════════════

/// HTTP server port.
#define WEB_SERVER_PORT 80

/// Polling interval for the web app's status updates (ms).
#define WEB_POLL_INTERVAL_MS 200

// ═══════════════════════════════════════════════════════════════
// Global State
// ═══════════════════════════════════════════════════════════════

/// true when WiFi AP is active and ready.
extern volatile bool wifi_connected;

/// IP address of the web server (AP IP or STA IP).
extern String web_server_ip;

// ═══════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Initialise WiFi and start the web server.
 *
 * This function:
 *   1. Sets up WiFi in AP+STA mode (AP always active, STA if credentials set)
 *   2. Starts ESPAsyncWebServer on port 80
 *   3. Registers all REST API endpoints
 *   4. Serves the embedded single-page web app
 *
 * Called once from setup() after Bluetooth is initialised.
 * The web server runs asynchronously — no task needed.
 */
void web_init();

/**
 * @brief Get the current web server URL.
 * @return String like "http://192.168.4.1" or "http://192.168.1.105"
 */
String web_get_url();
