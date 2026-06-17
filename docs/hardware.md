# Hardware Guide

## Wiring Overview

This document describes the complete hardware wiring for the ESP32-DSP 2-Way Active Crossover. All pin assignments are defined in `include/pins.h`.

## ESP32 to CS4344 DACs

### CS4344 #1 — Tweeter (I2S0)
| CS4344 Pin | ESP32 GPIO | Notes |
|------------|------------|-------|
| BCK | GPIO 25 | Bit clock |
| LRCK (WS) | GPIO 26 | Left/right clock |
| SDATA | GPIO 23 | Serial data (stereo) |
| MUTE | GPIO 4 | Active LOW mute (via 10kΩ resistor) |
| VCC | 3.3V | |
| GND | GND | |

### CS4344 #2 — Woofer (I2S1)
| CS4344 Pin | ESP32 GPIO | Notes |
|------------|------------|-------|
| BCK | GPIO 14 | Bit clock |
| LRCK (WS) | GPIO 16 | Left/right clock |
| SDATA | GPIO 13 | Serial data (stereo) |
| MUTE | GPIO 27 | Active LOW mute (via 10kΩ resistor) |
| VCC | 3.3V | |
| GND | GND |

**Note**: The CS4344 is a slave-mode DAC — it derives its internal MCLK from the BCK signal. No separate MCLK connection is needed.

## I2C Bus Wiring

All I2C devices share the same SDA (GPIO 21) and SCL (GPIO 22) lines. Use 4.7kΩ pull-up resistors on both lines to 3.3V (many breakout boards have these built-in).

### SSD1306 OLED (0x3C)
| OLED Pin | ESP32 GPIO | Notes |
|----------|------------|-------|
| SDA | GPIO 21 | I2C data |
| SCL | GPIO 22 | I2C clock |
| VCC | 3.3V | |
| GND | GND | |

### ADS1115 #1 — Primary Controls (0x48)
| ADS1115 Pin | ESP32 GPIO | Notes |
|-------------|------------|-------|
| SDA | GPIO 21 | I2C data |
| SCL | GPIO 22 | I2C clock |
| ADDR | GND | Address 0x48 |
| AIN0 | Pot 1 wiper | Master volume |
| AIN1 | Pot 2 wiper | Crossover frequency |
| AIN2 | Pot 3 wiper | Low-band gain |
| AIN3 | Pot 4 wiper | High-band gain |
| VDD | 3.3V | |
| GND | GND | |

### ADS1115 #2 — EQ / Expression (0x49) — Optional
| ADS1115 Pin | ESP32 GPIO | Notes |
|-------------|------------|-------|
| SDA | GPIO 21 | I2C data |
| SCL | GPIO 22 | I2C clock |
| ADDR | VDD (3.3V) | Address 0x49 |
| AIN0 | Pot 5 wiper | EQ Band 0 frequency |
| AIN1 | Pot 6 wiper | EQ Band 0 gain |
| AIN2 | Pot 7 wiper | EQ Band 1 frequency |
| AIN3 | Expression jack | Expression pedal input |
| VDD | 3.3V | |
| GND | GND | |

## Potentiometer Wiring

All potentiometers are wired as voltage dividers between 3.3V and GND:

```
3.3V ──┐
       │
    ┌──┴──┐
    │ Pot │ 10kΩ linear
    │     │
    └──┬──┘
       │ wiper → ADS1115 AINx
       │
GND ───┘
```

**Important**: Use linear (B-taper) pots for all controls. Logarithmic (A-taper) pots will give uneven response.

## Rotary Encoder Wiring

```
         ┌──────────────┐
         │   ENCODER    │
         │              │
GPIO 34 ─┤ A        VCC ├── 3.3V
GPIO 35 ─┤ B        GND ├── GND
GPIO 17 ─┤ SW (push)    │
         └──────────────┘
```

- **A, B**: Quadrature outputs. Internal pull-ups enabled in software.
- **SW**: Push button. Active LOW, internal pull-up enabled.
- **Debounce**: 150 ms software debounce in ISR.

## Footswitch Wiring

All four footswitches use the same circuit:

```
GPIO ──┬── 10kΩ ── 3.3V (pull-up)
       │
       ├── Switch ── GND
       │
       └── 100nF ── GND (optional hardware debounce cap)
```

| Footswitch | GPIO | Function |
|------------|------|----------|
| FS1 | 32 | Preset next |
| FS2 | 33 | Preset previous |
| FS3 | 36 | Mute toggle |
| FS4 | 39 | Bypass toggle |

**Note**: GPIO 32, 33, 36, 39 are input-only pins. They cannot be used as outputs.

## Expression Pedal Input

```
                    10kΩ
3.3V ──┬──────────[════]──┬── ADS1115 #2 AIN3
       │                  │
       │    Expression     │
       │    Pedal (10kΩ)   │
       │                  │
GND ───┴──────────────────┴── GND
```

The expression pedal acts as a variable resistor. The ADS1115 reads the voltage at the wiper. A 10kΩ linear pedal is recommended.

## MIDI Input Circuit (Optional)

```
MIDI DIN Pin 5 ── 220Ω ──┬── 6N138 Pin 2 (Anode)
                          │
                   1N4148 │ (reverse protection)
                          │
MIDI DIN Pin 4 ──────────┘── 6N138 Pin 3 (Cathode)

6N138 Pin 8 ── 3.3V (VCC)
6N138 Pin 5 ── GND
6N138 Pin 6 ── 4.7kΩ ── 3.3V (pull-up)
6N138 Pin 6 ── GPIO 20 (MIDI RX, UART2)
```

**Note**: MIDI uses 5 mA current loop. The 6N138 optocoupler provides galvanic isolation.

## Power Supply

| Component | Voltage | Current |
|-----------|---------|---------|
| ESP32 | 3.3V (via onboard regulator from 5V USB) | ~200 mA peak |
| CS4344 ×2 | 3.3V | ~20 mA each |
| ADS1115 ×2 | 3.3V | ~0.5 mA each |
| SSD1306 | 3.3V | ~20 mA |
| **Total** | **3.3V rail** | **~260 mA** |

**Recommendation**: Use a clean 3.3V supply or the ESP32's onboard regulator. Add 100nF decoupling caps near each IC's VCC pin.

## PCB Layout Recommendations

1. **Keep I2S traces short** — BCK, WS, and DATA should be routed together with matched lengths
2. **Separate analog and digital grounds** — connect at a single point near the power supply
3. **Decoupling** — 100nF ceramic cap on every IC's VCC pin, placed as close as possible
4. **I2C pull-ups** — 4.7kΩ on SDA and SCL, placed near the ESP32
5. **DAC output filtering** — RC low-pass (1kΩ + 100nF) on each DAC output if needed
6. **Mute pins** — Use 10kΩ series resistors on mute pins to limit current

## Schematic (Simplified)

```
                                    ┌─────────────┐
                                    │   ESP32     │
                                    │  DevKit V4  │
                                    │             │
                    ┌───────────────┤ GPIO 21 SDA │
                    │               │ GPIO 22 SCL │
                    │               │             │
                    │  ┌────────────┤ GPIO 25 BCK │──── I2S0 BCK
                    │  │  ┌─────────┤ GPIO 26 WS  │──── I2S0 WS
                    │  │  │  ┌──────┤ GPIO 23 DOUT│──── I2S0 DATA
                    │  │  │  │      │             │
                    │  │  │  │      │ GPIO 14 BCK │──── I2S1 BCK
                    │  │  │  │      │ GPIO 16 WS  │──── I2S1 WS
                    │  │  │  │      │ GPIO 13 DOUT│──── I2S1 DATA
                    │  │  │  │      │             │
                    │  │  │  │      │ GPIO 4  MUTE│──── CS4344 #1 MUTE
                    │  │  │  │      │ GPIO 27 MUTE│──── CS4344 #2 MUTE
                    │  │  │  │      │             │
                    │  │  │  │      │ GPIO 34    ─┤──── Encoder A
                    │  │  │  │      │ GPIO 35    ─┤──── Encoder B
                    │  │  │  │      │ GPIO 17    ─┤──── Encoder SW
                    │  │  │  │      │             │
                    │  │  │  │      │ GPIO 32    ─┤──── FS1 (Preset Next)
                    │  │  │  │      │ GPIO 33    ─┤──── FS2 (Preset Prev)
                    │  │  │  │      │ GPIO 36    ─┤──── FS3 (Mute)
                    │  │  │  │      │ GPIO 39    ─┤──── FS4 (Bypass)
                    │  │  │  │      │             │
                    │  │  │  │      │ GPIO 20 RX │──── MIDI IN (optocoupler)
                    │  │  │  │      └─────────────┘
                    │  │  │  │
    ┌───────────┐   │  │  │  │      ┌─────────────┐
    │ SSD1306   │───┘  │  │  │      │ CS4344 #1   │
    │ OLED      │ SDA  │  │  │      │ (Tweeter)   │
    │ 0x3C      │──────┘  │  │      │             │
    │           │ SCL     │  │      │ BCK ◄── GPIO25
    └───────────┘─────────┘  │      │ WS  ◄── GPIO26
                             │      │ DIN ◄── GPIO23
    ┌───────────┐             │      │ MUTE◄── GPIO4
    │ ADS1115#1 │─────────────┘      │ OUT ──► Tweeter Amp
    │ 0x48      │ SDA               └─────────────┘
    │           │ SCL
    │ AIN0 ◄── Pot1 (Vol)           ┌─────────────┐
    │ AIN1 ◄── Pot2 (Xover)         │ CS4344 #2   │
    │ AIN2 ◄── Pot3 (LoGain)        │ (Woofer)    │
    │ AIN3 ◄── Pot4 (HiGain)        │             │
    └───────────┘                    │ BCK ◄── GPIO14
                                     │ WS  ◄── GPIO16
    ┌───────────┐                    │ DIN ◄── GPIO13
    │ ADS1115#2 │                    │ MUTE◄── GPIO27
    │ 0x49      │                    │ OUT ──► Woofer Amp
    │           │                    └─────────────┘
    │ AIN0 ◄── Pot5 (EQ0 freq)
    │ AIN1 ◄── Pot6 (EQ0 gain)
    │ AIN2 ◄── Pot7 (EQ1 freq)
    │ AIN3 ◄── Expression Pedal
    └───────────┘
```
