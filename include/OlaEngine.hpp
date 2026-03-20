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
    static constexpr std::size_t CORR_LEN   = 256;   // samples used for correlation
    static constexpr std::size_t SEARCH_WIN = 512;   // ±samples around nominal position

    struct Params {
        double speed      = 0.5;
        double grain_ms   = 80.0;
        bool   freeze     = false;
        bool   trans_lock = false;
    };

    explicit OlaEngine() {
        std::memset(buf_,      0, sizeof(buf_));
        std::memset(out_tail_, 0, sizeof(out_tail_));
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
    }

    // Direct per-sample speed setter — bypasses the ms->samples path.
    // Used by HalftimePlugin to feed MorphController output sample-by-sample.
    void setSpeedDirect(double speed) noexcept {
        speed_smoother_.setTarget(std::clamp(speed, 0.25, 1.0));
    }

    // Set grain size directly in samples — used by BPM sync.
    void setGrainSamplesDirect(std::size_t gs) noexcept {
        grain_samps_ = std::clamp(gs, std::size_t{1}, MAX_BUF / 4);
        grain_ms_    = static_cast<double>(grain_samps_) / sr_ * 1000.0;
    }

    void setWindowShape(WindowShape shape) noexcept {
        win_shape_ = shape;
    }

    void reset() noexcept {
        std::memset(buf_,      0, sizeof(buf_));
        std::memset(out_tail_, 0, sizeof(out_tail_));
        write_ptr_  = 0;
        phase_[0]   = 0.0;
        phase_[1]   = 0.0;
        start_[0]   = 0;
        start_[1]   = 0;
        speed_smoother_.reset(speed_smoother_.target());
    }

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
            if (pi == 0) {
                if (!freeze_) {
                    const std::size_t nominal =
                        (write_ptr_ + MAX_BUF - gs + offset) % MAX_BUF;

                    if (trans_lock_ && onset) {
                        // Snap to detected onset — skip correlation search
                        start_[g] = (write_ptr_ + MAX_BUF - gs) % MAX_BUF;
                    } else {
                        start_[g] = wsolaSearch(nominal, g);
                    }

                    // Save a copy of the new grain's head as the next reference tail
                    for (std::size_t k = 0; k < CORR_LEN; ++k)
                        out_tail_[g][k] = readBuf((start_[g] + k) % MAX_BUF);
                }
            }

            // Interpolated read
            const double   rp   = static_cast<double>((start_[g] + offset) % MAX_BUF)
                                 + phase_[g];
            const std::size_t r0 = static_cast<std::size_t>(rp) % MAX_BUF;
            const std::size_t r1 = (r0 + 1) % MAX_BUF;
            const double frac    = rp - std::floor(rp);
            const double samp    = buf_[r0] * (1.0 - frac) + buf_[r1] * frac;
            const double win     = window::evaluate(win_shape_,
                static_cast<double>(pi) / static_cast<double>(gs));
            const double norm    = window::normCoeff(win_shape_);

            out += samp * win * norm;

            // Advance read phase at smoothed speed
            phase_[g] = std::fmod(phase_[g] + speed, static_cast<double>(gs));
        }

        return out;
    }

    std::size_t grainSamples() const noexcept { return grain_samps_; }

    uint32_t latencySamples() const noexcept {
        return static_cast<uint32_t>(grain_samps_);
    }

private:
    double buf_[MAX_BUF];

    // Tail of previous output grain for each interleaved head — WSOLA reference
    double out_tail_[2][CORR_LEN];

    std::size_t write_ptr_   = 0;
    double      phase_[2]    = {0.0, 0.0};
    std::size_t start_[2]    = {0, 0};
    std::size_t grain_samps_ = 3528;
    double      grain_ms_    = 80.0;
    double      sr_          = 44100.0;
    bool        freeze_      = false;
    bool        trans_lock_  = false;
    WindowShape win_shape_   = WindowShape::Hann;
    ParamSmoother speed_smoother_;

    double readBuf(std::size_t idx) const noexcept {
        return buf_[idx % MAX_BUF];
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

    // Search ±SEARCH_WIN around nominal for highest correlation.
    // Stepped search — checks every 4th sample to keep CPU cost bounded.
    std::size_t wsolaSearch(std::size_t nominal, int g) const noexcept {
        std::size_t best_pos  = nominal;
        double      best_corr = -2.0;
        const int   step      = 4;

        for (int delta = -static_cast<int>(SEARCH_WIN);
                 delta <= static_cast<int>(SEARCH_WIN);
                 delta += step)
        {
            const std::size_t candidate =
                (nominal + MAX_BUF + static_cast<std::size_t>(delta)) % MAX_BUF;
            const double corr = correlate(candidate, g);
            if (corr > best_corr) {
                best_corr = corr;
                best_pos  = candidate;
            }
        }
        return best_pos;
    }

    void updateGrain(double ms) noexcept {
        grain_ms_    = ms;
        grain_samps_ = static_cast<std::size_t>(
            std::clamp(ms / 1000.0 * sr_, 1.0,
                       static_cast<double>(MAX_BUF / 4)));
    }
};
