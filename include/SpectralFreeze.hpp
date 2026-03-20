#pragma once
#include <atomic>
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
    void setPhaseRandom(double r) noexcept { phase_random_ = std::clamp(r, 0.0, 1.0); }

    // Capture the next incoming frame as the freeze snapshot.
    // Thread-safe: sets a flag read by process() on the audio thread.
    void latch() noexcept { pending_latch_.store(true, std::memory_order_release); }

    void setSampleRate(double sr) noexcept override { sr_ = sr; }

    void reset() noexcept override {
        std::memset(frozen_mag_, 0, sizeof(frozen_mag_));
        blend_ = 0.0;
        pending_latch_.store(false, std::memory_order_relaxed);
        has_snapshot_ = false;
    }

    void process(SpectralFrame& frame) noexcept override {
        const std::size_t N = std::min(frame.half_size + 1, MAX_BINS);

        // Latch current frame if requested
        if (pending_latch_.load(std::memory_order_acquire)) {
            for (std::size_t k = 0; k < N; ++k)
                frozen_mag_[k] = frame.mag[k];
            pending_latch_.store(false, std::memory_order_relaxed);
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

        // Optional phase randomization — slowly drifts frozen bin phases
        // for a more organic, granular-pad-like sustained sound.
        if (phase_random_ > 0.001 && frame.phase) {
            for (std::size_t k = 0; k < N; ++k) {
                // Scale randomization by blend amount and phase_random knob
                const double drift = (prngNext() - 0.5) * 2.0 * M_PI * phase_random_ * frozen_w;
                frame.phase[k] += drift;
            }
        }
    }

private:
    double frozen_mag_[MAX_BINS] = {};
    double blend_         = 0.0;
    double phase_random_  = 0.0;
    double sr_            = 44100.0;
    uint32_t prng_state_  = 54321;
    std::atomic<bool> pending_latch_{false};
    bool   has_snapshot_  = false;

    double prngNext() noexcept {
        prng_state_ = prng_state_ * 1664525u + 1013904223u;
        return static_cast<double>(prng_state_) / 4294967296.0;
    }
};
