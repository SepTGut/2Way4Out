# Planning.md – ESP32 DSP Front‑End

## Goal
Create firmware for an ESP32‑WROOM‑32 (or DevKit‑C V4) that:
- Receives stereo audio via Bluetooth A2DP sink.
- Splits the audio into low and high frequency bands using a crossover.
- Sends low‑band to one CS4344 DAC and high‑band to a second CS4344 DAC (4‑channel output: L‑low, L‑high, R‑low, R‑high).
- Reads four potentiometers via an ADS1115 for Master Volume, Crossover Frequency, Low‑gain, High‑gain.
- Shows current parameters and Bluetooth status on a 128×64 I²C OLED.

## Hardware Summary
| Component | Interface | ESP‑32 Pins (suggested) |
|-----------|-----------|--------------------------|
| CS4344 (x2) | I²S0 (high‑band) + I²S1 (low‑band) + GPIO mute | I²S0: BCK‑GPIO25, WS‑GPIO26, DATA‑GPIO23; I²S1: BCK‑GPIO14, WS‑GPIO15, DATA‑GPIO13; MUTE‑HIGH‑GPIO4, MUTE‑LOW‑GPIO5 |
| ADS1115 | I²C | SDA‑GPIO21, SCL‑GPIO22 |
| OLED (SSD1306) | I²C | Same SDA/SCL as ADS1115 |
| Potentiometers (x4) | Connected to ADS1115 AIN0‑AIN3 | – |
| Bluetooth | Built‑in | – |

## Software Architecture (FreeRTOS tasks)
- **Core 0 (PRO_CPU)** – Bluetooth A2DP sink → Ring‑buffer.
- **Core 1 (APP_CPU)** – DSP task (crossover, gain), UI task (ADS1115 + OLED), optional watchdog.
- **Ring‑buffer** for audio frames shared between cores.

## Phase‑by‑Phase Implementation Plan
### Phase 1 – Project Scaffold & Core Drivers
1. Create PlatformIO project (`pio project init --ide <your-ide> --board esp32dev`).
2. Add library dependencies in `platformio.ini` (Adafruit ADS1X15, u8g2).
3. Implement I²C driver for ADS1115 (via Adafruit library).
4. Implement OLED driver (SSD1306) using `u8g2`.
5. Set up two I²S peripherals (I2S0 for high band, I2S1 for low band) with proper pin mapping.
6. Write a test program that outputs a constant sine wave to both DACs to verify I²S wiring.

### Phase 2 – Bluetooth A2DP Sink
1. Initialise BT controller and Bluedroid stack.
2. Register A2DP sink callbacks; forward incoming PCM to a ring‑buffer.
3. Verify audio reception by routing PCM directly to a single DAC (skip crossover for now).

### Phase 3 – DSP Core (Crossover & Gain)
1. Implement a 2‑band crossover (first‑order IIR, replace later with Butterworth if needed).
2. Add gain stage for Master, Low‑gain, High‑gain (multiply samples, apply clipping).
3. Make crossover frequency configurable (default 2 kHz) – expose as a variable.
4. Pull audio frames from the ring‑buffer, process, write low‑band to I2S1, high‑band to I2S0.

### Phase 4 – Control Interface (ADS1115 + OLED)
1. Poll ADS1115 at 10 Hz; map channels:
   - CH0 → Master Volume (0‑1.0)
   - CH1 → Crossover Frequency (0.5 k‑4 k Hz)
   - CH2 → Low‑band Gain (0‑2.0)
   - CH3 → High‑band Gain (0‑2.0)
2. Update global DSP parameters (protected by a mutex).
3. Refresh OLED (5 Hz) showing:
   - Bluetooth status (Connected / Disconnected)
   - Volume, crossover freq, low/high gains.
   - Simple menu for future extensions.

### Phase 5 – Integration & Testing
1. Run full system: connect a phone via Bluetooth, play music, verify four‑channel output with oscilloscope or headphones.
2. Adjust filter coefficients; ensure no audible clicks when parameters change.
3. Add soft‑mute during parameter changes to avoid pops.
4. Stress‑test: change pots rapidly while streaming audio.

### Phase 6 – Polishing & Optional Features
- OTA update support.
- Wi‑Fi web UI to tweak parameters.
- Store last settings in NVS.
- Power‑down mute when BT disconnected.

## Build & Flash (PlatformIO)
```bash
# Build
pio run

# Upload
pio run --target upload

# Monitor serial output
pio device monitor
```

## Deliverables
- `platformio.ini` – PlatformIO project configuration.
- `partitions.csv` – custom partition table.
- `include/` – shared headers (`pins.h`, `config.h`).
- `src/` – source files (`main.c`, `bluetooth.c`, `dsp.c`, `ui.c`).
- `components/ads1115/` – ADS1115 driver (if custom).
- `components/ssd1306/` – SSD1306 driver (if custom).
- `README.md` – project overview and build steps.
- `Planning.md` – this document.
