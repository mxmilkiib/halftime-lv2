# halftime-lv2

LV2 audio plugin implementing a WSOLA-based halftime/slowdown effect with phase vocoder pitch shifting, spectral processing, BPM sync, and morph transitions.

## features

- **WSOLA time-stretching** — 2-pass hierarchical correlation search, cubic Hermite interpolation, precomputed window LUT
- **phase vocoder** — radix-2 FFT pitch shifter with winner-take-all bin collision handling
- **Laroche-Dolson phase locking** — peak-locked phase coherence for clean harmonics
- **formant preservation** — O(N) running-sum spectral envelope correction
- **spectral freeze** — latch magnitude frames with optional phase randomization for organic drones
- **spectral tilt** — frequency-dependent gain slope for darkening or brightening
- **3-band transient detector** — adaptive per-band thresholds, configurable sensitivity
- **transient lookahead** — 10ms delay aligns grain boundaries ahead of detected onsets
- **BPM-sync grain size** — grain length locks to musical beat divisions
- **morph controller** — smoothstep speed ramp over configurable beat count
- **sub-bass enhancer** — ring-mod or half-wave rectification sub-harmonic generation
- **stutter grid** — click-free rhythmic looping with windowed crossfade
- **reverse grains** — read grains backwards for reverse-reverb textures
- **grain randomization** — jitter grain start positions for diffuse pad sounds
- **stereo width** — M/S processing with width control (0=mono, 1=normal, 2=wide)
- **dry/wet latency compensation** — delay line aligns dry signal to wet path latency
- **DC blocker + soft limiter** — prevents offset accumulation and OLA overshoot
- **window shapes** — Hann, Blackman, or rectangular-with-taper, precomputed LUT
- **lock-free parameter queue** — SPSC ring buffer for GUI-to-audio control updates
- **LV2 state save/restore** — all control parameters persisted

## architecture

all DSP is header-only C++17 under `include/`. `src/plugin.cpp` is the only LV2-dependent file. to embed in another host, include `HalftimePlugin.hpp` directly (no LV2 headers needed).

```
HalftimePlugin                          main orchestrator
├── ControlPorts                        25 float params, pushed via ParamQueue
├── processBlock()                      per-block entry point
│   ├── ParamQueue::popLatest()         drain lock-free queue
│   ├── setControls()                   map params to DSP components
│   └── per-sample loop:
│       ├── DcBlocker::process()        first-order highpass at ~10 Hz
│       ├── TransientDetector::process() 3-band adaptive onset detection
│       ├── TransientLookaheadScheduler::push()  delay + onset alignment
│       ├── MorphController::tick()     smoothstep speed ramp
│       ├── OlaEngine::process()        WSOLA grain engine
│       │   ├── wsolaSearch()           2-pass coarse/fine correlation
│       │   ├── cubicHermite()          4-point Catmull-Rom interpolation
│       │   ├── win_lut_[]              precomputed window lookup
│       │   ├── reverse mode            backwards grain read
│       │   └── grain randomization     LCG jitter on start position
│       ├── PhaseVocoder::process()     FFT pitch shifter
│       │   ├── fft()                   radix-2 DIT (forward + inverse)
│       │   ├── analysis                magnitude + true frequency extraction
│       │   ├── ISpectralProcessor      pluggable spectral chain:
│       │   │   ├── PhaseLockProcessor  Laroche-Dolson peak locking
│       │   │   ├── FormantShifter      O(N) envelope preservation
│       │   │   ├── SpectralTilt        frequency-dependent gain slope
│       │   │   └── SpectralFreeze      magnitude latch + phase random
│       │   └── synthesis               2-pass winner-take-all bin mapping
│       ├── SubBassEnhancer::process()  ring-mod or half-wave sub generation
│       ├── StutterGrid::process()      rhythmic loop with crossfade
│       ├── OutputLimiter::process()    soft-knee stereo limiter
│       ├── stereo width                M/S encode → scale side → decode
│       ├── DryDelay::push()            latency-compensated dry signal
│       └── dry/wet mix                 phase-aligned blend
├── setBpm()                            direct BPM for non-LV2 hosts
├── computeLatency()                    OLA + PV + optional lookahead
└── buildSpectralChain()                PhaseLock → Formant → Tilt → Freeze

WindowShapes                            stateless window functions
├── hann(), blackman(), rectTaper()     phase-in → gain-out
├── evaluate()                          dispatch by WindowShape enum
└── normCoeff()                         COLA normalisation per shape

ParamSmoother                           one-pole lowpass for control params
├── setSampleRate()                     compute coeff from time constant
├── setTarget() / next()                set target, advance one sample
└── settled()                           convergence check

ParamQueue<T, N>                        lock-free SPSC ring buffer
├── push()                              producer (GUI thread)
└── popLatest()                         consumer (audio thread), skip-to-latest

BpmSync (LV2 only)                      reads tempo from LV2 atom sequence
├── readPosition()                      parse time:Position atoms
├── grainSamples()                      beat division → sample count
└── guarded by HALFTIME_HAS_LV2

DryDelay                                ring-buffer delay for dry/wet alignment
├── setDelay()                          set delay in samples
└── push()                              write input, return delayed output
```

### key constants

| constant | value | location |
|----------|-------|----------|
| `OlaEngine::MAX_BUF` | 262144 | circular input buffer (2 MB) |
| `OlaEngine::MAX_GRAIN` | 65536 | max grain size in samples |
| `OlaEngine::CORR_LEN` | 256 | samples per WSOLA correlation |
| `OlaEngine::SEARCH_WIN` | 512 | ±samples for WSOLA search |
| `OlaEngine::COARSE_STEP` | 16 | coarse search stride |
| `OlaEngine::FINE_RADIUS` | 16 | ±samples for fine search |
| `PhaseVocoder::FFT_SIZE` | 2048 | FFT window size |
| `PhaseVocoder::HOP` | 512 | FFT hop size (75% overlap) |
| `DryDelay::MAX_DELAY` | 131072 | max dry compensation delay |
| `TransientDetector holdoff` | 50 ms | inter-onset suppression |
| `TransientLookaheadScheduler` | ~10 ms | lookahead delay |

## build

```sh
git submodule update --init --recursive   # fetch DPF + pugl
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build   # installs raw LV2 to ~/.lv2/halftime.lv2
```

requires: C++17 compiler, LV2 development headers, OpenGL development headers, X11/xcb headers.

the DPF build (`-DBUILD_DPF_PLUGIN=ON`, default) produces these artifacts in `build/bin/`:

| format | file | notes |
|--------|------|-------|
| LV2 (with GUI) | `halftime-dpf.lv2/` | DSP + UI split binaries |
| VST2 | `halftime-dpf-vst2.so` | |
| VST3 | `halftime-dpf.vst3/` | |
| CLAP | `halftime-dpf.clap` | |
| JACK standalone | `halftime-dpf` | |

to build only the raw LV2 (no GUI, no DPF dependency): `cmake -B build -DBUILD_DPF_PLUGIN=OFF`

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
| 8 | stutter_div | control | 1-16 |
| 9 | freeze | toggle | 0/1 |
| 10 | trans_lock | toggle | 0/1 |
| 11 | sensitivity | control | 0.5-2.0 |
| 12 | win_shape | control | 0/1/2 |
| 13 | bpm_div | control | 0-8 |
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
| 26 | reverse | toggle | 0/1 |
| 27 | grain_random | control | 0-1 |
| 28 | stereo_width | control | 0-2 |
| 29 | spectral_tilt | control | -1 to +1 |
| 30 | phase_random | control | 0-1 |

## Mixxx integration

see `mixxx/INTEGRATION.md` for instructions on embedding as a native Mixxx `EffectProcessorImpl`.

## license

MIT
