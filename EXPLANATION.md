# ESP32-DSP: Complete System Explanation

## Table of Contents

1. [What Is This?](#what-is-this)
2. [System Overview](#system-overview)
3. [Hardware Architecture](#hardware-architecture)
4. [Software Architecture](#software-architecture)
5. [Audio Pipeline — Step by Step](#audio-pipeline--step-by-step)
6. [DSP Effects Chain](#dsp-effects-chain)
7. [Parameter System](#parameter-system)
8. [WiFi Web Portal](#wifi-web-portal)
9. [Memory Map](#memory-map)
10. [Build & Deploy](#build--deploy)

---

## What Is This?

The **ESP32-DSP** is a professional-grade 2-way active crossover and DSP platform built on the ESP32 microcontroller. It receives audio via Bluetooth from a phone, processes it through a full DSP chain (EQ, compressor, limiter, delay), splits it into low and high frequency bands, and outputs to two separate amplifiers (woofer and tweeter).

**Key specs:**
- **Processor**: ESP32-WROOM-32, dual-core @ 240 MHz
- **Audio input**: Bluetooth A2DP (44.1 kHz, 16-bit stereo)
- **Audio output**: 2× CS4344 I2S DACs (woofer + tweeter)
- **DSP**: 4-band parametric EQ, per-band compressor/limiter, per-band delay
- **Controls**: OLED display, rotary encoder, 4 footswitches, 4–8 potentiometers
- **Web portal**: Full DSP control from any browser via WiFi
- **Presets**: 8 user presets stored in flash

---

## System Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           ESP32-DSP SYSTEM                                  │
│                                                                             │
│  ┌──────────┐    ┌──────────┐    ┌──────────────┐    ┌──────────┐           │
│  │  Phone   │    │  ESP32   │    │   DSP Task   │    │  DACs    │           │
│  │Bluetooth │───►│  BT Stack│───►│   Core 1     │───►│ CS4344×2 │──►Amps    │
│  │  A2DP    │    │  Core 0  │    │   Priority 5 │    │ I2S0/1   │           │ 
│  └──────────┘    └──────────┘    └──────────────┘    └──────────┘           │
│                       │                ▲                                    │
│                       │                │                                    │
│  ┌──────────┐    ┌────┴─────┐    ┌────┴─────┐                               │
│  │  Phone   │    │  UI Task │    │  Shared  │                               │
│  │  WiFi    │───►│  Core 0  │───►│  Params  │                               │
│  │  Browser │    │ Priority2│    │  Mutex   │                               │
│  └──────────┘    └──────────┘    └──────────┘                               │
│       ▲              ▲                                                      │
│       │              │                                                      │
│  ┌────┴────┐    ┌────┴──────┐                                               │
│  │  Web    │    │  OLED +   │                                               │
│  │ Server  │    │  Encoder  │                                               │
│  │ AP:80   │    │  + Pots   │                                               │
│  └─────────┘    └───────────┘                                               │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Hardware Architecture

### Core Components

| Component | Model | Interface | Purpose |
|-----------|-------|-----------|---------|
| Microcontroller | ESP32-WROOM-32 | — | Dual-core DSP + control |
| DAC ×2 | CS4344 | I2S | Tweeter + woofer output |
| ADC ×1–2 | ADS1115 | I2C | Potentiometer inputs |
| Display | SSD1306 | I2C | 128×64 OLED status |
| Encoder | EC11 | GPIO | Parameter navigation |
| Footswitches | SPST ×4 | GPIO | Preset/mute/bypass |

### GPIO Allocation

```
I2S0 (Tweeter):  BCK=25  WS=26  DATA=23
I1S1 (Woofer):   BCK=14  WS=16  DATA=13
I2C Bus:         SDA=21  SCL=22  (400 kHz)
  ├─ 0x3C: SSD1306 OLED
  ├─ 0x48: ADS1115 #1 (primary pots)
  └─ 0x49: ADS1115 #2 (EQ/expression)
Encoder:         A=34  B=35  SW=17
Footswitches:    FS1=32  FS2=33  FS3=36  FS4=39
DAC Mute:        HIGH=4  LOW=27
MIDI RX:         GPIO 20 (UART2)
```

### I2C Bus

All I2C devices share the same SDA/SCL bus at 400 kHz:

| Address | Device | Purpose |
|---------|--------|---------|
| 0x3C | SSD1306 OLED | Status display |
| 0x48 | ADS1115 #1 | Master vol, crossover, gains |
| 0x49 | ADS1115 #2 | EQ freq/gain, expression pedal |

---

## Software Architecture

### Dual-Core FreeRTOS Design

The ESP32 has two CPU cores. The system splits work between them:

**Core 0 — Communication + UI (low priority)**
- Bluetooth A2DP stack (receives audio from phone)
- UI task (reads pots, encoder, renders OLED)
- WiFi web server (serves web portal)
- All run at priority 2 or lower

**Core 1 — Audio DSP (high priority)**
- DSP task processes all audio
- Runs at priority 5 (higher than UI)
- Never blocked by communication tasks
- Deterministic timing for glitch-free audio

### Task Timing

```
Time ──────────────────────────────────────────────────────►

Core 1: │DSP batch│      │DSP batch│      │DSP batch│
        │ 1.45ms  │      │ 1.45ms  │      │ 1.45ms  │
        └─────────┘      └─────────┘      └─────────┘
        ←─ ~34% CPU ─→

Core 0: │BT+UI+Web│                    │BT+UI+Web│
        │  200ms  │                    │  200ms  │
        └─────────┘                    └─────────┘
        ←─ ~15% CPU ─→
```

### Ring Buffer — The Decoupler

The ring buffer is the critical link between BT input and DSP output:

```
BT Callback (Core 0)          DSP Task (Core 1)
       │                              │
       ▼                              ▼
  xRingbufferSend()           xRingbufferReceiveUpTo()
       │                              │
       ▼                              ▼
  ┌──────────────────────────────────────┐
  │         Ring Buffer (8 KB)           │
  │  ┌───┬───┬───┬───┬───┬───┬───┬───┐   │
  │  │   │   │   │   │   │   │   │   │   │
  │  └───┴───┴───┴───┴───┴───┴───┴───┘   │
  │  Capacity: 8192 bytes ≈ 46 ms audio  │
  └──────────────────────────────────────┘
```

- **Written by**: BT A2DP callback (non-blocking, drops if full)
- **Read by**: DSP task (blocks up to 100 ms waiting for data)
- **Underrun**: If empty, DSP outputs silence (no glitches)

### Mutex — Thread-Safe Parameters

All DSP parameters are stored in `current_params`, protected by a FreeRTOS mutex:

```
UI Task / Web Server                DSP Task
        │                                │
        ▼                                ▼
  xSemaphoreTake(mutex)           xSemaphoreTake(mutex)
        │                                │
        ▼                                ▼
  current_params = new_values     target = current_params
        │                                │
        ▼                                ▼
  xSemaphoreGive(mutex)           xSemaphoreGive(mutex)
                                         │
                                         ▼
                                   Smooth parameters
                                   Process audio
```

If the mutex can't be acquired within 10 ms, the reader uses fallback values (previous params or defaults). This prevents deadlocks.

---

## Audio Pipeline — Step by Step

### 1. Bluetooth Input

```
Phone ──A2DP──► ESP32 BT Stack ──a2dp_data_cb()──► Ring Buffer
```

- Phone pairs with "esp32DSP" via standard Bluetooth
- Audio is 16-bit stereo PCM at 44.1 kHz
- The BT stack delivers chunks via `a2dp_data_cb()` callback
- Callback runs in BT task context — must be fast
- Simply calls `xRingbufferSend(audio_rb, data, len, 0)`

### 2. DSP Task Processing

The DSP task processes audio in batches of 128 frames (512 bytes = ~1.45 ms of audio):

```
while(1) {
    1. Read batch from ring buffer (wait up to 100ms)
    2. Read current_params under mutex
    3. Smooth all parameters (linear interpolation)
    4. Detect EQ coefficient changes → recompute if needed
    5. For each sample:
       a. DC Block (20 Hz HPF)
       b. Parametric EQ (4-band biquad)
       c. Crossover (LPF → low, signal-LPF → high)
       d. Low band: Compressor → Limiter → Delay → Gain → Clip
       e. High band: Compressor → Limiter → Delay → Gain → Clip
    6. Write low band to I2S1 (woofer DAC)
    7. Write high band to I2S0 (tweeter DAC)
}
```

### 3. I2S DAC Output

```
DSP Task ──i2s_write()──► I2S1 ──CS4344 #2──► Woofer Amp
DSP Task ──i2s_write()──► I2S0 ──CS4344 #1──► Tweeter Amp
```

- Both I2S peripherals configured as master transmitters
- 16-bit stereo, 44.1 kHz, I2S Philips format
- APLL clock source for low jitter
- 8 DMA buffers × 64 samples each

---

## DSP Effects Chain

### Signal Flow Per Sample

```
Raw PCM Input (16-bit stereo)
  │
  ▼
┌─────────────────────────────────────────────────────────────┐
│ DC Block — 1st-order HPF @ 20 Hz                            │
│ Removes DC offset, protects drivers                         │
└─────────────────────────┬───────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ Parametric EQ — 4-band biquad IIR (Direct-Form-II)          │
│ Band 0: Low-shelf    (default 100 Hz)                       │
│ Band 1: Peaking EQ   (default 500 Hz)                       │
│ Band 2: Peaking EQ   (default 2000 Hz)                      │
│ Band 3: High-shelf   (default 8000 Hz)                      │
│ Per band: enabled, freq_hz, gain_db (-12 to +12), Q (0.1-10)│
└─────────────────────────┬───────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ Crossover — 1st-order IIR LPF (6 dB/oct)                    │
│ Low-pass → low band (woofer)                                │
│ High-pass = signal - LPF → high band (tweeter)              │
│ Frequency: 200–4000 Hz (default 2000 Hz)                    │
└──────────┬──────────────────────────────────┬───────────────┘
           ▼                                  ▼
┌─────────────────────────┐    ┌─────────────────────────┐
│ LOW BAND (Woofer)       │    │ HIGH BAND (Tweeter)     │
│                         │    │                         │
│ Compressor              │    │ Compressor              │
│ ├─ threshold: -60 to 0  │    │ (same structure)        │
│ ├─ ratio: 1:1 to 20:1   │    │                         │
│ ├─ attack: 0.1–100 ms   │    │                         │
│ ├─ release: 10–1000 ms  │    │                         │
│ └─ makeup: 0–24 dB      │    │                         │
│         ▼               │    │         ▼               │
│ Limiter (brickwall)     │    │ Limiter (brickwall)     │
│ ├─ threshold: -3 dBFS   │    │ ├─ threshold: -3 dBFS   │
│ └─ ratio: 100:1         │    │ └─ ratio: 100:1         │
│         ▼               │    │         ▼               │
│ Delay (0–20 ms)         │    │ Delay (0–20 ms)         │
│ ├─ circular buffer      │    │ ├─ circular buffer      │
│ └─ 0–882 samples        │    │ └─ 0–882 samples        │
│         ▼               │    │         ▼               │
│ Gain × master_volume    │    │ Gain × master_volume    │
│         ▼               │    │         ▼               │
│ Hard clip (±32767)      │    │ Hard clip (±32767)      │
│         ▼               │    │         ▼               │
│ I2S1 → CS4344 #2        │    │ I2S0 → CS4344 #1        │
└─────────────────────────┘    └─────────────────────────┘
```

### Parameter Smoothing

All parameter changes are smoothed with linear interpolation to prevent audible clicks:

```
smoothed += (target - smoothed) × 0.02
```

At 44.1 kHz, this gives ~10–20 ms glide time. Fast enough to feel responsive, slow enough to avoid transients.

**What's smoothed:**
- Master volume, crossover frequency, gains
- All EQ band parameters (freq, gain, Q)
- Compressor parameters (threshold, ratio, attack, release, makeup)
- Limiter threshold
- Delay on/off transitions

### RMS VU Meter

Calculated over each DSP batch (128 frames = 256 stereo samples):

```
sum_sq = Σ(L² + R²) for all samples
rms = sqrt(sum_sq / (frames × 2))
```

Updated every ~1.45 ms. Displayed on OLED and web portal.

---

## Parameter System

### Data Flow

```
Physical Controls                    Web Portal
(Pots, Encoder)                      (Browser)
      │                                    │
      ▼                                    ▼
adc_update_params()                  REST API POST
      │                                    │
      ▼                                    ▼
dsp_params_t local                   JSON {path, value}
      │                                    │
      ▼                                    ▼
xSemaphoreTake(mutex) ◄─────── xSemaphoreTake(mutex)
      │                                    │
      ▼                                    ▼
current_params = local           apply_param_path()
      │                                    │
      ▼                                    ▼
xSemaphoreGive(mutex) ────────► xSemaphoreGive(mutex)
      │
      ▼
DSP Task reads current_params
      │
      ▼
Smooth into smoothed_params
      │
      ▼
Process audio with smoothed_params
```

### Parameter Path System

The web portal uses dot-separated paths to address any parameter:

| Path | Type | Range |
|------|------|-------|
| `master_volume` | float | 0.0–1.0 |
| `crossover_hz` | float | 200–4000 |
| `low_gain` | float | 0.0–2.0 |
| `high_gain` | float | 0.0–2.0 |
| `mute` | bool | true/false |
| `bypass` | bool | true/false |
| `eq_bands.0.enabled` | bool | true/false |
| `eq_bands.0.freq_hz` | float | 20–20000 |
| `eq_bands.0.gain_db` | float | -12 to +12 |
| `eq_bands.0.q` | float | 0.1–10.0 |
| `low_driver.compressor.threshold_db` | float | -60 to 0 |
| `low_driver.compressor.ratio` | float | 1–20 |
| `low_driver.limiter.enabled` | bool | true/false |
| `high_driver.delay.samples` | uint16 | 0–882 |
| ... | ... | ... |

### Preset System

8 presets stored in ESP32 NVS (Non-Volatile Storage) flash:

```
NVS Namespace: "dsp"
Keys: "preset_0" through "preset_7"
Format: Binary blob (preset_t struct)
Size: ~256 bytes per preset
```

Each preset contains a complete `dsp_params_t` snapshot plus an 11-character name.

**Operations:**
- **Save**: Copy current_params → preset_t → NVS blob
- **Load**: NVS blob → preset_t → current_params (under mutex)
- **Auto-load**: Preset 0 loaded on boot

---

## WiFi Web Portal

### Architecture

```
Phone Browser
      │
      ▼ HTTP
┌─────────────────────────────────────────┐
│  ESP32 WiFi AP: "ESP32-DSP"             │
│  Password: "dsp12345"                   │
│  IP: 192.168.4.1                        │
│                                         │
│  ESPAsyncWebServer (port 80)            │
│  ├─ GET  /           → index.html       │
│  ├─ GET  /api/status → JSON status      │
│  ├─ GET  /api/params → JSON all params  │
│  ├─ POST /api/params → Update param     │
│  ├─ GET  /api/presets→ JSON preset list │
│  ├─ POST /api/presets/save              │
│  └─ POST /api/presets/load              │
└─────────────────────────────────────────┘
```

### How to Connect

1. Power on ESP32-DSP
2. On phone/computer, connect to WiFi: **ESP32-DSP** (password: `dsp12345`)
3. Open browser → `http://192.168.4.1`
4. Web portal loads with live VU meter and full controls

### Web UI Tabs

| Tab | Content |
|-----|---------|
| **Dashboard** | Live RMS VU meter, master volume slider, mute/bypass buttons, system status grid |
| **EQ** | Visual EQ curve (Canvas), per-band frequency/gain/Q sliders, enable toggles |
| **Dynamics** | Per-band compressor (threshold/ratio/attack/release), limiter ceiling, enable toggles |
| **Crossover** | Crossover frequency, per-band gain, time-alignment delay with enable toggles |
| **Presets** | 8 preset buttons (load on tap), save current settings |

### REST API

```bash
# Get live status (polled every 200ms by web app)
GET /api/status
→ {"bt_state":2,"rms_level":1234.5,"active_preset":0,"mute":false,"bypass":false,"wifi_rssi":-45}

# Get all DSP parameters
GET /api/params
→ {"master_volume":0.8,"crossover_hz":2000,"eq_bands":[...],"low_driver":{...},...}

# Update a parameter
POST /api/params
Body: {"path":"eq_bands.0.gain_db","value":3.5}

# Save preset
POST /api/presets/save
Body: {"index": 2}

# Load preset
POST /api/presets/load
Body: {"index": 2}
```

### Thread Safety

All parameter writes go through the same `dsp_params_mutex` as the physical controls. This means:
- Web page and physical encoder can adjust params simultaneously
- No audio glitches from concurrent access
- The DSP task always sees consistent parameter sets

---

## Memory Map

### Flash Layout (4 MB total)

```
Address     Size     Partition    Contents
0x000000    144 KB   Bootloader   Second-stage boot + partition table
0x024000    4 KB     otadata      OTA metadata
0x025000    4 KB     phy_init     PHY calibration data
0x026000    16 KB    NVS          Preset storage (8 × ~256 bytes)
0x02A000    2.0 MB   factory      Firmware + all libraries
0x22A000    512 KB   storage      LittleFS (web UI files)
0x2AA000    ~1.2 MB  (unused)     Free space
```

### RAM Usage (327 KB total)

```
Component           Size     Type
─────────────────── ──────   ────
Ring buffer         8 KB     DMA
Audio buffers       1.5 KB   DMA (3 × 512 bytes)
Delay lines         7 KB     DMA (4 × 883 × 2 bytes)
EQ state            64 B     Standard
Comp/Lim state      32 B     Standard
dsp_params_t        ~200 B   Standard
EQ coefficients     80 B     Standard
FreeRTOS heaps      ~200 KB  Standard
Code stack (DSP)    8 KB     Stack
Code stack (UI)     4 KB     Stack
─────────────────── ──────
Total used          ~59 KB
Free                ~268 KB
```

---

## Build & Deploy

### Build Commands

```bash
# Build firmware only
pio run -e esp32dev

# Build and upload firmware
pio run -t upload -e esp32dev

# Upload web UI files to LittleFS (first time or when data/ changes)
pio run -t uploadfs -e esp32dev

# Upload both
pio run -t upload -t uploadfs -e esp32dev

# Run unit tests (host-native)
pio test -e native

# Serial monitor
pio device monitor -b 115200
```

### Build Output

```
RAM:   18.1% (59 KB / 327 KB)
Flash: 80.6% (1.69 MB / 2.0 MB app partition)
Tests: 40/40 PASSED
```

### Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| ESP32-A2DP | 1.8.11 | Bluetooth audio input |
| Adafruit SSD1306 | 2.5.13 | OLED display driver |
| Adafruit ADS1X15 | 2.6.2 | ADC for potentiometers |
| Adafruit GFX | 1.11.9 | Graphics primitives |
| ESPAsyncWebServer | 3.4.1 | WiFi web server |
| AsyncTCP | 2.1.4 | Async TCP layer |
| ArduinoJson | 7.4.3 | JSON serialization |
| LittleFS | Built-in | Filesystem for web UI |

### Serial Debug Output

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
[OK] LittleFS mounted
[OK] WiFi AP: ESP32-DSP @ 192.168.4.1
[OK] Web server started at http://192.168.4.1
[OK] DSP task created on Core 1
[OK] UI task created on Core 0

=== System ready. Connect via Bluetooth! ===
```

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| No audio | BT not connected | Check `BT state` in serial output |
| Audio glitches | Power supply noise | Add 100nF caps, use clean 3.3V |
| OLED blank | I2C address wrong | Run I2C scanner, check 0x3C |
| Web page 404 | LittleFS not uploaded | Run `pio run -t uploadfs` |
| Encoder not working | GPIO 34/35 are input-only | Correct — they can't be outputs |
| ADC all zeros | I2C bus hung | Check ADS1115 wiring, add timeout |
| Preset not saving | NVS full | Erase flash: `pio run -t erase` |

---

## Future Enhancements

- **MIDI Control**: UART2 CC parser for external control surfaces
- **Signal Generator**: Built-in sine/pink noise/sweep for alignment
- **OTA Updates**: Over-the-air firmware updates
- **External MCLK**: 11.2896 MHz crystal for lower DAC jitter
- **SD Card**: Play audio files from SD card
- **Multi-room**: Sync multiple ESP32-DSP units over WiFi
