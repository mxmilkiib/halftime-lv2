#pragma once
#include <cmath>
#include <cstring>
#include <cstddef>
#include <algorithm>
#include "ISpectralProcessor.hpp"

// SpectralFreeze — latches the current spectral magnitude frame and
// crossfades between the live spectrum and the frozen snapshot.
//
// blend: 0.0 = fully live, 1.0 = fully frozen.
// latch(): captures the current frame as the freeze target.
//
// Unlike time-domain freeze (which just loops a buffer), spectral freeze
// holds the magnitude envelope while allowing phase to evolve naturally.
// This produces a sustained drone that retains harmonic character without
// the periodic artefacts of a looped buffer.

class SpectralFreeze final : public ISpectralProcessor {
public:
    static constexpr std::size_t MAX_BINS = 1025;

    void setBlend(double b) noexcept { blend_ = std::clamp(b, 0.0, 1.0); }

    // Capture the next incoming frame as the freeze snapshot.
    // Thread-safe: sets a flag read by process() on the audio thread.
    void latch() noexcept { pending_latch_ = true; }

    void setSampleRate(double sr) noexcept override { sr_ = sr; }

    void reset() noexcept override {
        std::memset(frozen_mag_, 0, sizeof(frozen_mag_));
        blend_ = 0.0;
        pending_latch_ = false;
        has_snapshot_ = false;
    }

    void process(SpectralFrame& frame) noexcept override {
        const std::size_t N = std::min(frame.half_size + 1, MAX_BINS);

        // Latch current frame if requested
        if (pending_latch_) {
            for (std::size_t k = 0; k < N; ++k)
                frozen_mag_[k] = frame.mag[k];
            pending_latch_ = false;
            has_snapshot_ = true;
        }

        // No snapshot or blend is zero — pass through
        if (!has_snapshot_ || blend_ < 0.001) return;

        // Crossfade between live and frozen magnitude
        const double live_w   = 1.0 - blend_;
        const double frozen_w = blend_;

        for (std::size_t k = 0; k < N; ++k) {
            frame.mag[k] = frame.mag[k] * live_w + frozen_mag_[k] * frozen_w;
        }

        // Phase is left untouched — it evolves naturally, which gives
        // the frozen sound a slowly shifting character rather than a
        // static tone.
    }

private:
    double frozen_mag_[MAX_BINS] = {};
    double blend_     = 0.0;
    double sr_        = 44100.0;
    bool   pending_latch_ = false;
    bool   has_snapshot_   = false;
};
