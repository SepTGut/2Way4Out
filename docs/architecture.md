# System Architecture

## Overview

The ESP32-DSP is a real-time audio processing system built on the ESP32 dual-core microcontroller. It uses FreeRTOS to split work across two cores: Core 0 handles all user interface and communication tasks, while Core 1 is dedicated entirely to audio DSP processing.

## Dual-Core Design

```
┌─────────────────────────────────────────────────────────────────────┐
│                          ESP32-WROOM-32                             │
│                         Dual Core @ 240 MHz                         │
│                                                                     │
│  ┌──────────────────────────┐    ┌──────────────────────────────┐   │
│  │        CORE 0            │    │          CORE 1              │   │
│  │    (UI + Comms)          │    │     (Audio DSP Only)         │   │
│  │    Priority 2            │    │     Priority 5               │   │
│  │                          │    │                              │   │
│  │  ┌────────────────────┐  │    │  ┌────────────────────────┐  │   │
│  │  │     UI Task        │  │    │  │     DSP Task           │  │   │
│  │  │                    │  │    │  │                        │  │   │
│  │  │ • ADS1115 ×2      │  │    │  │ • Ring buffer read     │  │   │
│  │  │ • Encoder ISR     │  │    │  │ • Parameter smoothing  │  │   │
│  │  │ • Footswitch ISR  │  │    │  │ • DC block             │  │   │
│  │  │ • OLED rendering  │  │    │  │ • 4-band EQ            │  │   │
│  │  │ • Bluetooth stack │  │    │  │ • Crossover            │  │   │
│  │  │ • I2C bus mgmt    │  │    │  │ • Compressor ×2        │  │   │
│  │  │ • NVS preset I/O  │  │    │  │ • Limiter ×2           │  │   │
│  │  │                    │  │    │  │ • Delay ×2             │  │   │
│  │  │ Period: 200 ms    │  │    │  │ • Gain + Clip          │  │   │
│  │  │ Stack: 4 KB       │  │    │  │ • I2S write ×2         │  │   │
│  │  └────────────────────┘  │    │  │                        │  │   │
│  │                          │    │  │ Period: ~1.45 ms       │  │   │
│  │                          │    │  │ (128 frames @ 44.1kHz)  │  │   │
│  │                          │    │  │ Stack: 8 KB             │  │   │
│  │                          │    │  └────────────────────────┘  │   │
│  └──────────────────────────┘    └──────────────────────────────┘   │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────────┐│
│  │                    Shared Resources                              ││
│  │  • Ring Buffer (8 KB, DMA) — BT callback → DSP task            ││
│  │  • dsp_params_mutex — protects current_params                   ││
│  │  • current_params — UI writes, DSP reads                        ││
│  │  • global_rms_level — DSP writes, UI reads (volatile)          ││
│  │  • enc_count / enc_pressed / fs_flags — ISR → UI task          ││
│  └─────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────┘
```

## Data Flow

### Audio Path
```
Phone ──Bluetooth──► ESP32 BT Stack ──a2dp_data_cb()──► Ring Buffer
                                                              │
                                                    xRingbufferReceiveUpTo()
                                                              │
                                                              ▼
                                                    DSP Task (Core 1)
                                                    ┌───────────────┐
                                                    │ Process batch │
                                                    │ of 128 frames │
                                                    │ (512 bytes)   │
                                                    └───────┬───────┘
                                                            │
                                                    ┌───────┴───────┐
                                                    ▼               ▼
                                              I2S1 (Woofer)    I2S0 (Tweeter)
                                              ───CS4344 #2───  ───CS4344 #1───
```

### Parameter Path
```
Potentiometers ──ADS1115──┐
Encoder ISR ─────────────┼──► adc_update_params() ──► current_params
Footswitch ISR ──────────┘         (under mutex)
                                        │
                                        ▼
                                  DSP Task reads
                                  current_params
```

## Task Timing

### DSP Task (~1.45 ms per batch)
The DSP task processes audio in batches of 128 frames. At 44.1 kHz stereo, 128 frames = ~1.45 ms of audio. The ring buffer provides ~46 ms of buffering (8 KB), giving the DSP task plenty of slack.

| Operation | Time (approx.) |
|-----------|----------------|
| Ring buffer read | 0.05 ms |
| Parameter smooth (all) | 0.02 ms |
| DC Block (128 frames) | 0.02 ms |
| EQ 4-band (128 frames) | 0.15 ms |
| Crossover (128 frames) | 0.03 ms |
| Compressor ×2 (128 frames) | 0.15 ms |
| Limiter ×2 (128 frames) | 0.10 ms |
| Delay ×2 (128 frames) | 0.05 ms |
| Gain + Clip (128 frames) | 0.03 ms |
| I2S write ×2 | 0.10 ms |
| **Total per batch** | **~0.7 ms** |
| **CPU usage** | **~33% @ 44.1 kHz** |

### UI Task (200 ms period)
| Operation | Time (approx.) |
|-----------|----------------|
| ADC read (8 channels via I2C) | ~2 ms |
| Parameter mapping | 0.1 ms |
| Encoder/footswitch processing | 0.1 ms |
| OLED rendering + I2C transfer | ~5 ms |
| **Total per cycle** | **~8 ms** |
| **CPU usage** | **~4% @ 5 Hz** |

## Ring Buffer Design

The ring buffer decouples the Bluetooth A2DP callback (which runs in a high-priority BT task context) from the DSP task. It's configured as a raw byte stream (`RINGBUF_TYPE_BYTEBUF`) with 8 KB capacity.

```
                    Written by                  Read by
                 a2dp_data_cb()              dsp_task()
                      │                            │
                      ▼                            ▼
              ┌───────────────────────────────────────┐
              │           Ring Buffer (8 KB)          │
              │                                       │
              │  ┌───┬───┬───┬───┬───┬───┬───┬───┐   │
              │  │   │   │   │   │   │   │   │   │   │
              │  └───┴───┴───┴───┴───┴───┴───┴───┘   │
              │  ◄── write_pos    read_pos ──►        │
              │                                       │
              │  Capacity: 8192 bytes ≈ 46 ms audio   │
              └───────────────────────────────────────┘
```

- **Write**: `xRingbufferSend(audio_rb, data, len, 0)` — non-blocking, drops data if full
- **Read**: `xRingbufferReceiveUpTo(audio_rb, &size, timeout, bytes_per_batch)` — blocks up to 100 ms
- **Underrun handling**: If no data is available, the DSP task writes silence to both DACs

## Mutex Design

The `dsp_params_mutex` protects the shared `current_params` struct. The critical section is kept as short as possible:

```
UI Task:                          DSP Task:
──────────                        ──────────
adc_update_params()               xRingbufferReceive()
  ↓                                 ↓
xSemaphoreTake(mutex, 10ms)       xSemaphoreTake(mutex, 10ms)
  ↓                                 ↓
current_params = local_params     target_params = current_params
  ↓                                 ↓
xSemaphoreGive(mutex)             xSemaphoreGive(mutex)
  ↓                                 ↓
ui_update(params)                 Process audio with target_params
```

If the mutex cannot be acquired within 10 ms:
- **UI task**: Skips the update cycle (DSP still has previous values)
- **DSP task**: Falls back to `dsp_params_default`

## ISR Design

### Encoder ISR
- **Trigger**: GPIO 34 (channel A) and GPIO 35 (channel B), both edges
- **Algorithm**: 4-entry state machine lookup table
- **Output**: Accumulates in `enc_count` (cleared by UI task)
- **Latency**: < 1 µs per interrupt

### Footswitch ISRs
- **Trigger**: GPIO 32, 33, 36, 39 — falling edge (active low)
- **Debounce**: 150 ms timestamp-based (ignores repeated edges)
- **Output**: Sets `fs_flags[n]` (cleared by UI task)
- **Latency**: < 1 µs per interrupt

### A2DP Data Callback
- **Trigger**: Bluetooth stack delivers PCM audio chunks
- **Action**: `xRingbufferSend(audio_rb, data, len, 0)` — non-blocking
- **Constraint**: Must return quickly (runs in BT task context)

## Memory Layout

### DMA Memory (allocated with `MALLOC_CAP_DMA`)
| Buffer | Size | Purpose |
|--------|------|---------|
| `raw_buf` | 512 bytes | Raw PCM from ring buffer |
| `low_buf` | 512 bytes | Processed low-band output |
| `high_buf` | 512 bytes | Processed high-band output |
| `delay_buf_low_L` | 1,766 bytes | Woofer left delay line |
| `delay_buf_low_R` | 1,766 bytes | Woofer right delay line |
| `delay_buf_high_L` | 1,766 bytes | Tweeter left delay line |
| `delay_buf_high_R` | 1,766 bytes | Tweeter right delay line |
| **DMA Total** | **~8 KB** | |

### Standard RAM
| Variable | Size | Purpose |
|----------|------|---------|
| `eq_coeffs` | 80 bytes | 4 bands × 5 coefficients × 4 bytes |
| `eq_state` | 64 bytes | 4 bands × 2 channels × 2 delays × 4 bytes |
| `comp_gain` | 16 bytes | 2 drivers × 2 channels × 4 bytes |
| `lim_gain` | 16 bytes | 2 drivers × 2 channels × 4 bytes |
| `filt_state` | 16 bytes | Crossover filter state |
| `current_params` | ~200 bytes | Full DSP parameter set |
| **State Total** | **~400 bytes** | |

### Flash
| Data | Size | Purpose |
|------|------|---------|
| Firmware + libraries | ~1.17 MB | Code, constants, DSP algorithms |
| Preset storage (NVS) | ~2 KB | 8 presets × ~256 bytes each |
| NVS overhead | ~4 KB | Key-value metadata |
| **Total used** | **~1.17 MB** | |
| **App partition** | **1.5 MB** | 4 MB chip, single-app layout |
| **Free for expansion** | **~330 KB** | Future features, OTA support |

## I2S Configuration

Both I2S peripherals are configured identically:

| Parameter | Value |
|-----------|-------|
| Mode | Master transmitter |
| Sample rate | 44,100 Hz |
| Bits per sample | 16 |
| Channel format | Stereo (right-left) |
| Communication format | I2S Philips standard |
| DMA buffers | 8 × 64 samples |
| Clock source | APLL (low jitter) |
| MCLK | Not used (CS4344 is slave) |

### I2S0 (Tweeter)
| Signal | GPIO |
|--------|------|
| BCK | 25 |
| WS (LRCK) | 26 |
| DATA_OUT | 23 |

### I2S1 (Woofer)
| Signal | GPIO |
|--------|------|
| BCK | 14 |
| WS (LRCK) | 16 |
| DATA_OUT | 13 |

## I2C Bus Configuration

| Parameter | Value |
|-----------|-------|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| Speed | 400 kHz (Fast Mode) |
| Pull-ups | External 4.7kΩ (or module-integrated) |

### Device Addresses
| Address | Device | Purpose |
|---------|--------|---------|
| 0x3C | SSD1306 OLED | Status display |
| 0x48 | ADS1115 #1 | Primary pots (vol, xover, gains) |
| 0x49 | ADS1115 #2 | EQ pots + expression pedal |
| 0x50 | 24LC256 (optional) | Extended preset storage |

## FreeRTOS Configuration

| Parameter | Value |
|-----------|-------|
| Tick rate | 1000 Hz (1 ms tick) |
| Min stack size | 4 KB (UI), 8 KB (DSP) |
| Time slice | 1 ms |
| Preemption | Enabled |
| CPU frequency | 240 MHz |

### Priority Scheme
| Priority | Task |
|----------|------|
| 5 | DSP task (highest user priority) |
| 2 | UI task |
| 1 | Idle loop (diagnostics) |
| 0 | FreeRTOS idle task |
