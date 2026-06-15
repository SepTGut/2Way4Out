 # DSP Enhancement Plan: ESP32-2Way Crossover

## Phase 1: Stability & Audio Quality (Bug Fixes)
- [ ] **Implement Parameter Smoothing:** Add a linear interpolator to `dsp_params` so that changes to frequency and gain "slide" over 10-20ms instead of jumping.
- [ ] **I2C Timeout/Recovery:** Wrap I2C calls in a timeout check to ensure a failing OLED doesn't freeze the ADC readings.
- [ ] **Filter Coefficient Stability:** Validate that the coefficient calculation remains stable at the `CROSSOVER_MIN_HZ` limit to avoid oscillation.

## Phase 2: DSP Engine Upgrades (Optimizations)
- [ ] **DC Block Filter:** Implement a 1st-order High-Pass filter at 20Hz to remove DC offset.
- [ ] **Optimized Loop Unrolling:** Refactor the DSP inner loop to process samples using pointer arithmetic instead of array indexing for a minor speed boost.
- [ ] **Precision Tuning:** Move filter state variables (`filt_state`) to `double` to reduce quantization noise.

## Phase 3: UI & User Experience (Improvements)
- [ ] **RMS VU Meter:** Replace raw sample display on OLED with an RMS (Root Mean Square) calculation for a professional-looking volume bar.
- [ ] **Boot-up Animation:** Add a "System Initializing" progress bar to the OLED.
- [ ] **BT State Visuals:** Add a small icon or color-coded text to clearly indicate "Disconnected" vs "Connected" vs "Streaming".

## Technical Constraints
- **Core 1:** Must remain dedicated to DSP.
- **Core 0:** Handles I2C, BT, and UI.
- **Memory:** Keep ring buffer at 8KB to maintain low latency.
