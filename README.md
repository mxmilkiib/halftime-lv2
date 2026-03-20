# halftime-lv2

LV2 audio plugin implementing a WSOLA-based halftime/slowdown effect with phase vocoder pitch shifting, spectral freeze, formant preservation, BPM sync, and morph transitions.

## features

- **WSOLA time-stretching** — correlation-based grain placement eliminates metallic flutter on pitched material
- **phase vocoder** — FFT-based pitch shifting with peak phase locking (Laroche-Dolson) for clean harmonic content
- **formant preservation** — spectral envelope correction prevents chipmunk artefacts on vocals
- **spectral freeze** — latch and crossfade magnitude frames for sustained drone effects
- **3-band transient detector** — adaptive per-band thresholds with configurable sensitivity
- **transient lookahead** — 10ms delay buffer aligns grain boundaries ahead of detected onsets
- **BPM-sync grain size** — grain length locks to musical beat divisions when host provides tempo
- **morph controller** — smoothstep ramp between normal and halftime speed over configurable beat count
- **sub-bass enhancer** — ring-mod or half-wave rectification sub-harmonic generation
- **stutter grid** — click-free rhythmic looping with Hann-windowed crossfade at boundaries
- **DC blocker + soft limiter** — prevents offset accumulation and OLA overshoot
- **window shape selection** — Hann, Blackman, or rectangular-with-taper per grain
- **lock-free parameter queue** — thread-safe SPSC control updates from GUI to audio thread
- **LV2 state save/restore** — all 20 control parameters persisted

## architecture

all DSP logic lives in plain header-only C++17 classes under `include/` with no LV2 dependency. `src/plugin.cpp` is the only file that touches the LV2 C API.

to embed in another host (e.g. Mixxx native effects), include `HalftimePlugin.hpp` directly — no LV2 baggage.

## build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build   # installs to ~/.lv2/halftime.lv2
```

requires: C++17 compiler, LV2 development headers (`pkg-config --exists lv2`).

## test

```sh
./build/tests/halftime_tests
./build/tests/halftime_tests_san   # address + undefined behaviour sanitisers
```

## LV2 ports

| index | symbol | type | range |
|-------|--------|------|-------|
| 0-1 | in_l, in_r | audio in | |
| 2-3 | out_l, out_r | audio out | |
| 4 | speed | control | 25-100% |
| 5 | grain_ms | control | 20-500 ms |
| 6 | wet | control | 0-1 |
| 7 | pitch_semi | control | -12 to +12 st |
| 8 | stutter_div | control | 1-16 (integer) |
| 9 | freeze | toggle | 0/1 |
| 10 | trans_lock | toggle | 0/1 |
| 11 | sensitivity | control | 0.5-2.0 |
| 12 | win_shape | control | 0/1/2 (integer) |
| 13 | bpm_div | control | 0-8 (integer) |
| 14 | latency | output | samples |
| 15 | time_pos | atom in | time:Position |
| 16 | formant | control | 0-1 |
| 17 | spec_freeze | control | 0-1 |
| 18 | spec_latch | toggle | 0/1 |
| 19 | sub_gain | control | 0-1 |
| 20 | sub_mode | toggle | 0/1 |
| 21 | morph_in | toggle | 0/1 |
| 22 | morph_out | toggle | 0/1 |
| 23 | morph_beats | control | 0.5-8.0 |
| 24 | phase_lock | toggle | 0/1 |
| 25 | lookahead | toggle | 0/1 |

## license

MIT
