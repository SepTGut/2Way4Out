# DSP Effects Reference

This document describes every DSP effect in the signal chain, its algorithm, parameters, and valid ranges.

## Signal Chain Order

```
Input → DC Block → Parametric EQ (×4) → Crossover → [Per-Band: Compressor → Limiter → Delay → Gain → Clip] → Output
```

---

## 1. DC Block

**Type**: 1st-order IIR high-pass filter  
**Cutoff**: 20 Hz (fixed)  
**Location**: First stage, before EQ  
**Purpose**: Removes DC offset from the Bluetooth audio stream to prevent wasted headroom and potential DAC damage.

**Algorithm**:
```
y[n] = α · x[n] + (1 - α) · y[n-1]
output = x[n] - y[n]
where α = dt / (rc + dt), rc = 1 / (2π · 20)
```

**Parameters**: None (fixed at 20 Hz)

---

## 2. Parametric EQ (4-Band)

**Type**: Biquad IIR (Direct-Form-II Transposed)  
**Algorithm**: Robert Bristow-Johnson's Audio EQ Cookbook  
**Location**: Full-range signal, before crossover  
**Purpose**: Tonal shaping of the full audio signal before it's split into bands.

### Band Types (auto-selected by frequency)
| Frequency Range | Type |
|-----------------|------|
| < 200 Hz | Low-shelf |
| 200–4000 Hz | Peaking EQ |
| > 4000 Hz | High-shelf |

### Parameters per Band
| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| `enabled` | true/false | false | — |
| `freq_hz` | 20 – 20,000 | 100/500/2000/8000 | Hz |
| `gain_db` | -12.0 – +12.0 | 0.0 | dB |
| `q` | 0.1 – 10.0 | 0.7/1.0/1.0/0.7 | — |

### Default Configuration
| Band | Type | Freq | Gain | Q |
|------|------|------|------|---|
| 0 | Low-shelf | 100 Hz | 0 dB | 0.7 |
| 1 | Peaking EQ | 500 Hz | 0 dB | 1.0 |
| 2 | Peaking EQ | 2000 Hz | 0 dB | 1.0 |
| 3 | High-shelf | 8000 Hz | 0 dB | 0.7 |

### Coefficient Calculation
Coefficients are pre-computed whenever a parameter changes (not per sample). The biquad uses the standard RBJ cookbook formulas:

```
y[n] = b0·x[n] + w1
w1   = b1·x[n] - a1·y[n] + w2
w2   = b2·x[n] - a2·y[n]
```

### CPU Cost
~200 cycles/sample for 4 bands (all enabled). ~10% of Core 1 at 44.1 kHz.

---

## 3. Crossover

**Type**: 1st-order IIR low-pass filter (6 dB/octave)  
**High-pass**: Derived as `signal - low_pass_output`  
**Location**: Splits full-range into low and high bands  
**Purpose**: Separates audio into woofer and tweeter frequency ranges.

### Parameters
| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| `crossover_hz` | 200 – 4000 | 2000 | Hz |

### Algorithm
```
α = dt / (rc + dt)
rc = 1 / (2π · cutoff_hz)
dt = 1 / sample_rate

low[n]  = α · input[n] + (1 - α) · low[n-1]
high[n] = input[n] - low[n]
```

### Notes
- First-order (6 dB/oct) is gentle — good for active crossovers with steep acoustic rolloff
- For steeper slopes, consider upgrading to 2nd-order (12 dB/oct) or Linkwitz-Riley (24 dB/oct)
- The high-pass is complementary (all-pass sum), so no phase mismatch at crossover point

### CPU Cost
~30 cycles/sample. ~1% of Core 1.

---

## 4. Compressor (Per-Band)

**Type**: Feed-forward peak detector with smooth gain envelope  
**Location**: After crossover, per driver (low + high)  
**Purpose**: Dynamic range control — reduces loud peaks to protect drivers and even out levels.

### Parameters (per driver)
| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| `enabled` | true/false | false | — |
| `threshold_db` | -60 – 0 | -12 | dBFS |
| `ratio` | 1:1 – 20:1 | 2:1 | — |
| `attack_ms` | 0.1 – 100 | 10 | ms |
| `release_ms` | 10 – 1000 | 100 | ms |
| `makeup_db` | 0 – 24 | 0 | dB |

### Algorithm
1. **Peak detection**: `peak = max(|L|, |R|)`
2. **Level conversion**: `level_dB = 20 · log10(peak / 32768)`
3. **Gain reduction**: If `level_dB > threshold`:
   `GR_dB = (level_dB - threshold) × (1 - 1/ratio)`
4. **Target gain**: `target = 10^((-GR_dB + makeup_dB) / 20)`
5. **Smoothing**: Attack/release envelope with exponential time constants
6. **Apply**: `output = input × smoothed_gain`

### Attack/Release
- **Attack**: Fast response when gain is decreasing (compression engaging)
  - Coefficient: `exp(-1 / (attack_ms × sample_rate / 1000))`
- **Release**: Slow recovery when gain is increasing (compression releasing)
  - Coefficient: `exp(-1 / (release_ms × sample_rate / 1000))`

### CPU Cost
~150 cycles/sample per driver. ~8% of Core 1 for both drivers.

---

## 5. Limiter (Per-Band)

**Type**: Feed-forward brickwall limiter (infinite ratio)  
**Location**: After compressor, per driver  
**Purpose**: Absolute ceiling to prevent driver damage and DAC clipping.

### Parameters (per driver)
| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| `enabled` | true/false | true | — |
| `threshold_db` | -60 – 0 | -3 | dBFS |

### Algorithm
Same as compressor but with:
- **Ratio**: 100:1 (effectively infinite)
- **Attack**: 1 ms (very fast)
- **Makeup gain**: 0 dB

### Hard Clip Safety
After the limiter, a final hard clipper guarantees the output never exceeds ±32767 (16-bit full scale):
```
output = clamp(input, -32768, +32767)
```

### Default Behavior
Limiters are **enabled by default** at -3 dBFS to protect drivers. Compressors are **disabled by default** (user enables as needed).

### CPU Cost
~100 cycles/sample per driver. ~5% of Core 1 for both drivers.

---

## 6. Delay (Per-Band)

**Type**: Circular buffer (ring buffer) with integer sample delay  
**Location**: After limiter, per driver  
**Purpose**: Time-alignment between woofer and tweeter acoustic centers.

### Parameters (per driver)
| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| `enabled` | true/false | false | — |
| `samples` | 0 – 882 | 0 | samples |

### Algorithm
```
write_idx = (write_idx + 1) % buffer_size
buffer[write_idx] = input
read_idx = (write_idx + buffer_size - delay_samples) % buffer_size
output = buffer[read_idx]
```

### Delay Time Conversion
| Time | Samples (44.1 kHz) |
|------|-------------------|
| 0.1 ms | 4 |
| 0.5 ms | 22 |
| 1.0 ms | 44 |
| 5.0 ms | 220 |
| 10.0 ms | 441 |
| 20.0 ms | 882 |

### Buffer Allocation
Each driver gets 2 delay lines (L + R), each `DELAY_MAX_SAMPLES + 1` = 883 samples. Total: 4 × 883 × 2 bytes ≈ 7 KB.

### CPU Cost
~30 cycles/sample per driver. ~2% of Core 1 for both drivers.

---

## 7. Gain & Clip

**Type**: Linear gain with hard clipping  
**Location**: Final stage, per driver  
**Purpose**: Master volume and per-band level adjustment.

### Parameters (per driver)
| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| `master_volume` | 0.0 – 1.0 | 0.8 | linear |
| `low_gain` | 0.0 – 2.0 | 1.0 | linear |
| `high_gain` | 0.0 – 2.0 | 1.0 | linear |

### Algorithm
```
output = input × driver_gain × master_volume
output = clamp(output, -32768, +32767)
```

### CPU Cost
~30 cycles/sample. ~1% of Core 1.

---

## 8. Mute & Bypass

### Mute
- Sets all output samples to zero
- Applied after all processing
- Toggle via footswitch (GPIO 36)

### Bypass
- Routes raw input directly to both DAC outputs
- Skips all DSP processing
- Toggle via footswitch (GPIO 39)

---

## Parameter Smoothing

All parameters are smoothed with linear interpolation to prevent audible clicks and zipper noise:

```
smoothed += (target - smoothed) × 0.02
```

At 44.1 kHz, this gives a glide time of approximately 10–20 ms, which is fast enough to feel responsive but slow enough to avoid transients.

**Smoothed parameters**:
- Master volume
- Crossover frequency
- Low/high gain
- All EQ band parameters (freq, gain, Q)
- Compressor parameters (threshold, ratio, attack, release, makeup)
- Limiter threshold
- Delay samples (on/off transitions only)

---

## Total CPU Budget

| Effect | CPU (approx.) |
|--------|---------------|
| DC Block | 1% |
| 4-band EQ | 10% |
| Crossover | 1% |
| Compressor ×2 | 8% |
| Limiter ×2 | 5% |
| Delay ×2 | 2% |
| Gain + Clip | 1% |
| **Total** | **~33%** |

**Headroom**: ~67% remaining on Core 1 for future features (MIDI, web UI, test tones).

---

## RMS VU Meter

**Type**: Root Mean Square calculation over each DSP batch  
**Batch size**: 128 frames (256 samples including stereo)  
**Output**: `global_rms_level` (volatile float, updated every ~1.45 ms)

**Algorithm**:
```
sum_sq = Σ(L² + R²) for all samples in batch
rms = sqrt(sum_sq / (frames × 2))
```

**Display**: Normalized to 0–1 range, with 2× visual boost for quiet signals. Shown as a bar on the OLED.
