#pragma once
#include <cmath>
#include <cstring>
#include <cstddef>
#include <algorithm>
#include "ISpectralProcessor.hpp"

// FormantShifter — spectral envelope preservation for pitch shifting.
//
// When pitch-shifting, the spectral envelope (formants) shifts with the
// harmonics, making vocals sound chipmunky (up) or muffled (down).
// This processor estimates the spectral envelope via cepstral smoothing,
// then reapplies it after the pitch shift to preserve the original timbre.
//
// Algorithm:
//   1. Estimate spectral envelope of the current frame (smoothed magnitude).
//   2. Estimate spectral envelope of what the shifted frame would be.
//   3. Multiply shifted magnitudes by (original_envelope / shifted_envelope).
//
// strength: 0.0 = no correction, 1.0 = full formant preservation.

class FormantShifter final : public ISpectralProcessor {
public:
    void setStrength(double s) noexcept {
        strength_ = std::clamp(s, 0.0, 1.0);
    }

    void setSampleRate(double sr) noexcept override { sr_ = sr; }

    void reset() noexcept override {
        std::memset(env_orig_,    0, sizeof(env_orig_));
        std::memset(env_shifted_, 0, sizeof(env_shifted_));
    }

    void process(SpectralFrame& frame) noexcept override {
        if (strength_ < 0.01) return;
        if (std::abs(frame.pitch_ratio - 1.0) < 0.01) return;

        const std::size_t H = frame.half_size;
        const std::size_t N = std::min(H + 1, MAX_BINS);

        // Estimate original spectral envelope via moving average smoothing
        estimateEnvelope(frame.mag, env_orig_, N);

        // Estimate what the shifted envelope looks like
        // (resample the original envelope at pitch_ratio rate)
        for (std::size_t k = 0; k < N; ++k) {
            const double src = static_cast<double>(k) / frame.pitch_ratio;
            const std::size_t s0 = static_cast<std::size_t>(src);
            const double frac = src - std::floor(src);
            if (s0 + 1 < N)
                env_shifted_[k] = env_orig_[s0] * (1.0 - frac) + env_orig_[s0 + 1] * frac;
            else if (s0 < N)
                env_shifted_[k] = env_orig_[s0];
            else
                env_shifted_[k] = 1e-10;
        }

        // Apply correction: mag *= lerp(1.0, orig/shifted, strength)
        for (std::size_t k = 0; k < N; ++k) {
            const double correction = (env_shifted_[k] > 1e-10)
                ? env_orig_[k] / env_shifted_[k]
                : 1.0;
            const double blended = 1.0 + (correction - 1.0) * strength_;
            frame.mag[k] *= blended;
        }
    }

private:
    static constexpr std::size_t MAX_BINS = 1025;
    static constexpr std::size_t SMOOTH_W = 20; // bins for moving average

    double env_orig_[MAX_BINS]    = {};
    double env_shifted_[MAX_BINS] = {};
    double strength_ = 1.0;
    double sr_       = 44100.0;

    // Simple moving average envelope estimator
    static void estimateEnvelope(const double* mag, double* env, std::size_t n) noexcept {
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t lo = (k >= SMOOTH_W) ? k - SMOOTH_W : 0;
            const std::size_t hi = std::min(k + SMOOTH_W, n - 1);
            double sum = 0.0;
            for (std::size_t j = lo; j <= hi; ++j)
                sum += mag[j];
            env[k] = sum / static_cast<double>(hi - lo + 1);
            env[k] = std::max(env[k], 1e-10); // avoid division by zero
        }
    }
};
