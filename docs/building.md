# Building & Debugging Guide

## Prerequisites

### Software
- [VS Code](https://code.visualstudio.com/) with [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension
- Or: [PlatformIO CLI](https://platformio.org/install/cli) (`pip install platformio`)
- USB driver for your ESP32 board (usually CP2102 or CH340)

### Hardware
- ESP32-DevKit V4 (or any ESP32-WROOM-32 board)
- USB cable (micro-USB or USB-C depending on board)
- The assembled hardware (see [hardware.md](hardware.md))

---

## Building

### Build for ESP32
```bash
pio run -e esp32dev
```

### Build and Upload
```bash
pio run -t upload -e esp32dev
```

### Upload and Monitor Serial
```bash
pio run -t upload -t monitor -e esp32dev
```

### Clean Build
```bash
pio run -t clean -e esp32dev
pio run -e esp32dev
```

---

## Running Tests

### Host-Native Unit Tests
Tests run on your PC (not on the ESP32). They validate data structures, math, pin assignments, and parameter mapping.

```bash
# Run all tests
pio test -e native

# Verbose output
pio test -e native -v

# Very verbose (per-assertion)
pio test -e native -vv
```

### Expected Output
```
Collected 1 tests

Processing * in native environment
Building...
Testing...
test\test_dsp.cpp:593: test_eq_band_size    [PASSED]
test\test_dsp.cpp:594: test_dyn_params_size [PASSED]
...
test\test_dsp.cpp:646: test_multiple_presets [PASSED]

--------------------- native:* [PASSED] Took 1.00 seconds ---------------------

=================================== SUMMARY ===================================
Environment    Test    Status    Duration
-------------  ------  --------  ------------
native         *       PASSED    00:00:01.001
================= 40 test cases: 40 succeeded in 00:00:01.001 ==================
```

---

## Serial Monitor

### Default Settings
- **Baud rate**: 115200
- **Line ending**: None (raw output)

### PlatformIO Monitor
```bash
pio device monitor -b 115200
```

### Expected Boot Output
```
========================================
  ESP32-DSP  2-Way Active Crossover
  Target: ESP32-WROOM-32 / DevKit V4
  Features: EQ+Comp+Lim+Dly
========================================

[OK] Both I2S peripherals initialised
[OK] Ring buffer: 8192 bytes
[OK] Mutex created
[OK] NVS preset storage initialised
[OK] Loaded preset 0 on boot
[OK] ADS1115 #1 (0x48) initialised
[OK] SSD1306 OLED initialised
[OK] Encoder initialised (GPIO 34/35/17)
[OK] Footswitches initialised (GPIO 32/33/36/39)
[OK] A2DP sink started as 'esp32DSP'
[OK] DACs unmuted
[OK] DSP task created on Core 1
[OK] UI task created on Core 0

=== System ready. Connect via Bluetooth! ===
```

### Diagnostic Output (every 5 seconds)
```
[diag] Free heap: 286032 bytes | BT state: 2 | Core: 0
[diag] Ring buffer items: 1
```

| Field | Meaning |
|-------|---------|
| `Free heap` | Available RAM (should stay > 200 KB) |
| `BT state` | 0=Disconnected, 1=Connected, 2=Playing |
| `Ring buffer items` | Audio chunks waiting (1–3 is normal) |

---

## Troubleshooting

### Build Errors

#### `error: 'uint16_t' does not name a type`
**Cause**: Missing `#include <stdint.h>` in a header.  
**Fix**: Add `#include <stdint.h>` at the top of the header file.

#### `error: 'nvs_handle' redeclared as different kind of symbol`
**Cause**: ESP32 Arduino framework has a deprecated `typedef nvs_handle_t nvs_handle`.  
**Fix**: Rename your variable to something else (e.g., `nvs_hnd`).

#### `error: 'current_params' was not declared in this scope`
**Cause**: Missing `#include "globals.h"` in the source file.  
**Fix**: Add `#include "globals.h"` to the file.

#### `warning: #warning "AudioTools library is not included first or installed"`
**Cause**: The ESP32-A2DP library checks for AudioTools.  
**Fix**: This is harmless and can be ignored. It doesn't affect functionality.

### Runtime Issues

#### No Audio Output
1. Check Bluetooth connection — look for `BT state: 2` in serial output
2. Check DAC mute pins — GPIO 4 and 27 should be HIGH (unmuted)
3. Check I2S wiring — BCK, WS, DATA must match pin assignments
4. Check ring buffer — if `items` is 0, BT data isn't arriving

#### Audio Glitches / Pops
1. Check power supply — ESP32 needs clean 3.3V, 500mA+
2. Check I2S clock — keep wires short, use twisted pairs if possible
3. Check ring buffer level — if consistently > 5 items, DSP task may be overloaded
4. Add decoupling caps — 100nF on each IC's VCC pin

#### OLED Not Displaying
1. Check I2C address — run an I2C scanner to confirm 0x3C
2. Check pull-ups — SDA and SCL need 4.7kΩ pull-ups to 3.3V
3. Check wiring — SDA→GPIO 21, SCL→GPIO 22

#### ADC Not Reading
1. Check I2C address — ADS1115 #1 at 0x48, #2 at 0x49
2. Check ADDR pin — GND for 0x48, VDD for 0x49
3. Check pot wiring — wiper to AINx, ends to 3.3V and GND

#### Encoder Not Responding
1. Check GPIO 34/35 — these are input-only, cannot be used as outputs
2. Check pull-ups — internal pull-ups are enabled in software
3. Check debounce — 150ms debounce means rapid clicks may be ignored

### Memory Issues

#### Heap Running Low
- Check `Free heap` in diagnostic output
- If < 100 KB: reduce delay buffer count or disable unused effects
- If < 50 KB: check for memory leaks (allocations without frees)

#### Stack Overflow
- Increase stack size in `xTaskCreatePinnedToCore()` call
- Default: UI task 4 KB, DSP task 8 KB
- Add 1–2 KB if using deep call chains

---

## PlatformIO Configuration

The project has two build environments in `platformio.ini`:

### `[env:esp32dev]` — Main firmware
- Platform: ESP32 (espressif32@6.7.0)
- Board: esp32dev
- Framework: Arduino
- Libraries: SPI, Wire, Adafruit (BusIO, GFX, SSD1306, ADS1X15), ESP32-A2DP
- Build flags: Legacy I2S support, BT suspend state mapping

### `[env:native]` — Host tests
- Platform: Native (your PC)
- Build flags: Include paths for all headers
- Library compatibility: Off (no Arduino dependencies)

---

## File Locations

| File | Purpose |
|------|---------|
| `platformio.ini` | Build configuration |
| `include/*.h` | All header files (shared across project) |
| `src/*.cpp` | All implementation files |
| `test/test_dsp.cpp` | Host-native unit tests |
| `DWNlib/` | Third-party libraries (local copies) |
| `.pio/build/` | Build output (auto-generated) |

---

## Updating Dependencies

```bash
# Update all libraries
pio pkg update

# Update platform
pio platform update espressif32

# List installed packages
pio pkg list
```

---

## Debugging Tips

### Enable Verbose Logging
Add to `build_flags` in `platformio.ini`:
```ini
build_flags =
    -DCORE_DEBUG_LEVEL=5
    ...
```

### Check FreeRTOS Task Status
Add to the idle loop in `main.cpp`:
```cpp
// Print task info
TaskHandle_t tasks[10];
UBaseType_t count = uxTaskGetSystemState(tasks, 10, NULL);
for (UBaseType_t i = 0; i < count; i++) {
    Serial.printf("Task: %s  Stack high: %u\n",
                  tasks[i].pcTaskName,
                  tasks[i].usStackHighWaterMark);
}
```

### I2C Bus Scanner
Use this to verify all I2C devices are detected:
```cpp
// Add to setup() after Wire.begin()
for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
        Serial.printf("I2C device found at 0x%02X\n", addr);
    }
}
```
