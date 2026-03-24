#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "ParamSmoother.hpp"
#include "WindowShapes.hpp"

// OlaEngine — WSOLA (Waveform Similarity OLA) grain engine.
//
// Improvement over basic OLA:
//   At each grain boundary, instead of latching the read head at a
//   mathematically-computed position, we search a window of ±search_len
//   samples and pick the offset whose waveform best correlates with the
//   tail of the previous output grain.
//
//   This eliminates the metallic flutter caused by phase discontinuities
//   between successive grains — the dominant artefact of naive OLA on
//   pitched material.
//
// No heap allocation after construction. All state is in-place.
// process() is safe to call from the audio thread.

class OlaEngine {
public:
    static constexpr std::size_t MAX_BUF    = 262144;
    static constexpr std::size_t MAX_GRAIN  = MAX_BUF / 4;
    static constexpr std::size_t CORR_LEN   = 256;   // samples used for correlation
    static constexpr std::size_t SEARCH_WIN = 512;   // ±samples around nominal position

    // Per-block parameters set by HalftimePlugin::setControls()
    struct Params {
        double speed      = 0.5;
        double grain_ms   = 80.0;
        bool   freeze     = false;
        bool   trans_lock = false;
        bool   reverse    = false;
        double random     = 0.0;  // 0.0 = no randomization, 1.0 = full grain-length jitter
        double smooth     = 0.5;  // 0.0 = percussive (dry bleed at loop points), 1.0 = sustain (full crossfade)
    };



    // MARK: LIFECYCLE

    explicit OlaEngine() {
        std::memset(buf_,      0, sizeof(buf_));
        std::memset(out_tail_, 0, sizeof(out_tail_));
        rebuildWindowLut();
    }

    void setSampleRate(double sr) {
        sr_ = sr;
        speed_smoother_.setSampleRate(sr, 30.0);  // 30ms ramp on speed changes
        updateGrain(grain_ms_);
    }

    void setParams(const Params& p) {
        if (p.grain_ms != grain_ms_) updateGrain(p.grain_ms);
        speed_smoother_.setTarget(p.speed);
        freeze_     = p.freeze;
        trans_lock_ = p.trans_lock;
        reverse_    = p.reverse;
        random_     = p.random;
        smooth_     = std::clamp(p.smooth, 0.0, 1.0);
    }

    // Direct per-sample speed setter — bypasses the ms->samples path.
    // Used by HalftimePlugin to feed MorphController output sample-by-sample.
    void setSpeedDirect(double speed) noexcept {
        speed_smoother_.setTarget(std::clamp(speed, 0.25, 1.0));
    }

    // Set grain size directly in samples — used by BPM sync.
    void setGrainSamplesDirect(std::size_t gs) noexcept {
        const auto clamped = std::clamp(gs, std::size_t{1}, MAX_GRAIN);
        if (clamped == grain_samps_) return;
        grain_samps_ = clamped;
        grain_ms_    = static_cast<double>(grain_samps_) / sr_ * 1000.0;
        rebuildWindowLut();
    }

    void setWindowShape(WindowShape shape) noexcept {
        if (shape != win_shape_) {
            win_shape_ = shape;
            rebuildWindowLut();
        }
    }

    void reset() noexcept {
        std::memset(buf_,      0, sizeof(buf_));
        std::memset(out_tail_, 0, sizeof(out_tail_));
        write_ptr_  = 0;
        phase_[0]   = 0.0;
        phase_[1]   = static_cast<double>(grain_samps_ / 2);
        start_[0]   = 0;
        start_[1]   = 0;
        boundary_done_[0] = false;
        boundary_done_[1] = false;
        speed_smoother_.reset(speed_smoother_.target());
    }



    // MARK: PER-SAMPLE PROCESSING

    [[nodiscard]] double process(double input, bool onset) noexcept {
        // Write input into circular buffer
        buf_[write_ptr_] = input;
        write_ptr_ = (write_ptr_ + 1) % MAX_BUF;

        const double speed = speed_smoother_.next();
        const std::size_t gs = grain_samps_;

        double out = 0.0;

        for (int g = 0; g < 2; ++g) {
            const std::size_t half  = gs / 2;
            const std::size_t offset = static_cast<std::size_t>(g) * half;
            const std::size_t pi    = static_cast<std::size_t>(phase_[g]);

            // Grain boundary — find best-correlation start position
            if (pi == 0 && !boundary_done_[g]) {
                boundary_done_[g] = true;
                if (!freeze_) {
                    const std::size_t nominal =
                        (write_ptr_ + MAX_BUF - gs + offset) % MAX_BUF;

                    if (trans_lock_ && onset) {
                        // Snap to detected onset — skip correlation search
                        start_[g] = (write_ptr_ + MAX_BUF - gs) % MAX_BUF;
                    } else {
                        // Apply random jitter to nominal position before WSOLA search
                        std::size_t search_pos = nominal;
                        if (random_ > 0.001) {
                            const double jitter_range = random_ * static_cast<double>(gs);
                            const double jitter = (prngNext() - 0.5) * 2.0 * jitter_range;
                            search_pos = (nominal + MAX_BUF +
                                static_cast<std::size_t>(static_cast<int>(jitter) + static_cast<int>(MAX_BUF))
                            ) % MAX_BUF;
                        }
                        start_[g] = wsolaSearch(search_pos, g);
                    }

                    // Save a copy of the new grain's head as the next reference tail
                    for (std::size_t k = 0; k < CORR_LEN; ++k)
                        out_tail_[g][k] = readBuf((start_[g] + k) % MAX_BUF);
                }
            }

            // Cubic Hermite interpolated read — much less HF attenuation than linear
            // In reverse mode, read backwards from end of grain
            const double phase_pos = reverse_
                ? static_cast<double>(gs) - 1.0 - phase_[g]
                : phase_[g];
            const double   rp   = static_cast<double>((start_[g] + offset) % MAX_BUF)
                                 + phase_pos;
            const std::size_t ri = static_cast<std::size_t>(rp) % MAX_BUF;
            const double frac   = rp - std::floor(rp);
            const double samp   = cubicHermite(
                readBuf(ri + MAX_BUF - 1), readBuf(ri),
                readBuf(ri + 1),           readBuf(ri + 2), frac);

            // Smooth: crossfade dry input at grain boundaries.
            // When smooth is low (percussive), a short dry-signal bleed
            // precedes each loop restart so transient attacks aren't lost.
            // When smooth is high (sustain), the OLA window dominates.
            double env = win_lut_[pi] * win_norm_;
            if (smooth_ < 0.99 && pi < fade_len_) {
                // Fade region at grain start: blend from dry toward OLA
                const double t = static_cast<double>(pi) / static_cast<double>(fade_len_);
                // Mix: at t=0 fully dry-weighted, at t=1 fully OLA
                const double dry_weight = (1.0 - smooth_) * (1.0 - t);
                env = env * (1.0 - dry_weight) + dry_weight;
            }

            out += samp * env;

            // Advance read phase at smoothed speed
            phase_[g] = std::fmod(phase_[g] + speed, static_cast<double>(gs));
            // Reset boundary guard once we've moved past sample 0
            if (static_cast<std::size_t>(phase_[g]) > 0)
                boundary_done_[g] = false;
        }

        return out;
    }

    std::size_t grainSamples() const noexcept { return grain_samps_; }

    uint32_t latencySamples() const noexcept {
        return static_cast<uint32_t>(grain_samps_);
    }

private:


    // MARK: -- internal state

    double buf_[MAX_BUF];

    // Tail of previous output grain for each interleaved head — WSOLA reference
    double out_tail_[2][CORR_LEN];

    std::size_t write_ptr_   = 0;
    double      phase_[2]    = {0.0, 1764.0}; // grain 1 staggered by gs/2
    std::size_t start_[2]    = {0, 0};
    bool        boundary_done_[2] = {false, false};
    std::size_t grain_samps_ = 3528;
    double      grain_ms_    = 80.0;
    double      sr_          = 44100.0;
    bool        freeze_      = false;
    bool        trans_lock_  = false;
    bool        reverse_     = false;
    double      random_      = 0.0;
    double      smooth_      = 0.5;
    std::size_t fade_len_     = 256;  // samples of dry-bleed region at grain start
    uint32_t    prng_state_  = 12345;
    WindowShape win_shape_   = WindowShape::Hann;
    double      win_lut_[MAX_GRAIN] = {};
    double      win_norm_    = 1.0;
    ParamSmoother speed_smoother_;



    // MARK: -- helpers

    // Fast LCG PRNG — returns [0.0, 1.0)
    double prngNext() noexcept {
        prng_state_ = prng_state_ * 1664525u + 1013904223u;
        return static_cast<double>(prng_state_) / 4294967296.0;
    }

    double readBuf(std::size_t idx) const noexcept {
        return buf_[idx % MAX_BUF];
    }

    // 4-point cubic Hermite (Catmull-Rom) interpolation.
    // Preserves HF content much better than linear; 3 extra multiply-adds.
    static double cubicHermite(double y0, double y1, double y2, double y3,
                               double t) noexcept {
        const double c0 = y1;
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // Normalised cross-correlation between out_tail_[g] and the buffer
    // starting at candidate_start. Returns value in [-1, 1].
    double correlate(std::size_t candidate_start, int g) const noexcept {
        double num = 0.0, denom_ref = 0.0, denom_cand = 0.0;
        for (std::size_t k = 0; k < CORR_LEN; ++k) {
            const double ref  = out_tail_[g][k];
            const double cand = readBuf((candidate_start + k) % MAX_BUF);
            num       += ref * cand;
            denom_ref  += ref  * ref;
            denom_cand += cand * cand;
        }
        const double denom = std::sqrt(denom_ref * denom_cand);
        return (denom > 1e-10) ? (num / denom) : 0.0;
    }



    // MARK: -- wsola search

    // 2-pass hierarchical WSOLA search around nominal position.
    // Pass 1: coarse sweep at step=16 over ±SEARCH_WIN (~64 correlations).
    // Pass 2: fine sweep at step=1 over ±FINE_RADIUS around best coarse hit (~32).
    // Total: ~96 correlations vs ~256 for the old flat step=4 approach (~2.7x faster).
    static constexpr int COARSE_STEP  = 16;
    static constexpr int FINE_RADIUS  = 16;

    std::size_t wsolaSearch(std::size_t nominal, int g) const noexcept {
        std::size_t best_pos  = nominal;
        double      best_corr = -2.0;

        // Pass 1: coarse
        for (int delta = -static_cast<int>(SEARCH_WIN);
                 delta <= static_cast<int>(SEARCH_WIN);
                 delta += COARSE_STEP)
        {
            const std::size_t candidate =
                (nominal + MAX_BUF + static_cast<std::size_t>(delta)) % MAX_BUF;
            const double corr = correlate(candidate, g);
            if (corr > best_corr) {
                best_corr = corr;
                best_pos  = candidate;
            }
        }

        // Pass 2: fine search around coarse winner
        const std::size_t coarse_best = best_pos;
        for (int delta = -FINE_RADIUS; delta <= FINE_RADIUS; ++delta) {
            const std::size_t candidate =
                (coarse_best + MAX_BUF + static_cast<std::size_t>(delta)) % MAX_BUF;
            const double corr = correlate(candidate, g);
            if (corr > best_corr) {
                best_corr = corr;
                best_pos  = candidate;
            }
        }
        return best_pos;
    }



    // MARK: -- grain sizing

    void updateGrain(double ms) noexcept {
        grain_ms_    = ms;
        grain_samps_ = static_cast<std::size_t>(
            std::clamp(ms / 1000.0 * sr_, 1.0,
                       static_cast<double>(MAX_GRAIN)));
        // Fade region = 1/8 of grain, clamped to a useful range
        fade_len_ = std::clamp(grain_samps_ / 8, std::size_t{16}, std::size_t{2048});
        rebuildWindowLut();
    }

    void rebuildWindowLut() noexcept {
        const double gs_d = static_cast<double>(grain_samps_);
        for (std::size_t i = 0; i < grain_samps_; ++i)
            win_lut_[i] = window::evaluate(win_shape_, static_cast<double>(i) / gs_d);
        win_norm_ = window::normCoeff(win_shape_);
    }
};
