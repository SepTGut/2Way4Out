# DSP Enhancement Plan: ESP32-2Way Crossover

## ✅ COMPLETED — Phase 1-5 Implementation

### Data Structures
- [x] Extended `dsp_params_t` with EQ bands, compressor, limiter, delay, mute/bypass, presets
- [x] Added `eq_band_t`, `dyn_params_t`, `delay_params_t`, `driver_params_t`, `preset_t` structs
- [x] Updated `pins.h` with encoder, footswitch, MIDI, SPI display pin assignments
- [x] Updated `globals.h/cpp` with new state variables (EQ state, comp/lim gain, delay buffers, encoder/footwitch flags)

### DSP Effects Chain
- [x] **4-band parametric EQ** — biquad IIR (Direct-Form-II Transposed), before crossover
  - Low-shelf, 2× peaking PEQ, high-shelf
  - Coefficient pre-computation on parameter change
- [x] **Per-driver compressor** — feed-forward, peak detector, attack/release envelope
- [x] **Per-driver brickwall limiter** — infinite ratio, fast attack, safety clipper
- [x] **Per-driver delay line** — circular buffer, 0-20ms time-alignment
- [x] **Parameter smoothing** — linear interpolation (10-20ms glide) for all parameters
- [x] **DC block filter** — 20 Hz high-pass (already existed)
- [x] **RMS VU meter** — already existed
- [x] **Mute/Bypass** — global mute and DSP bypass toggle

### Hardware Controls (Software Ready — Hardware Not Yet Connected)
- [x] **Rotary encoder** — quadrature decoding via GPIO ISR, push button
- [x] **2nd ADS1115** — 8 total ADC channels (4 pots + 4 EQ/dynamics)
- [x] **4 footswitches** — preset next/prev, mute toggle, bypass toggle (ISR-driven)
- [x] **Expression pedal input** — mapped to ADS1115 #2 CH3
- [x] **MIDI input** — UART2 pin assigned (GPIO 20 RX)

### UI System
- [x] **5-page OLED display** — Main, EQ 1-2, EQ 3-4, Dynamics, Delay/Status
- [x] **Encoder page cycling** — push button cycles through pages
- [x] **Encoder parameter adjustment** — rotation adjusts active page parameter
- [x] **Footswitch status display** — mute/bypass indicators

### Preset System
- [x] **NVS flash storage** — 8 presets with save/load
- [x] **Preset naming** — 11-character names stored per preset
- [x] **Boot auto-load** — preset 0 loaded on startup

## Phase 6: Advanced Features (Future)
- [ ] **MIDI control** — UART2 parser for CC messages to adjust parameters
- [ ] **Signal generator** — sine, pink noise, sweep for alignment/testing
- [ ] **Web UI** — ESPAsyncWi-Fi control panel with EQ curve visualization
- [ ] **SPI display upgrade** — 128×128 OLED or 240×320 TFT

## Technical Constraints
- **Core 1:** Must remain dedicated to DSP.
- **Core 0:** Handles I2C, BT, and UI.
- **Memory:** Keep ring buffer at 8KB to maintain low latency.
- **CPU:** Current estimate ~33% on Core 1 with all effects enabled.
- **Build:** RAM 12.7% (41KB/327KB), Flash 89.7% (1.17MB/1.31MB).
