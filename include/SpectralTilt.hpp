#pragma once
#include <cmath>
#include <cstddef>
#include <algorithm>
#include "ISpectralProcessor.hpp"

// SpectralTilt — frequency-dependent gain in the spectral domain.
//
// tilt: -1.0 = darken (attenuate highs), 0.0 = flat, +1.0 = brighten (boost highs).
// Applied as a linear gain slope across bins: gain(k) = 1.0 + tilt * (k/N - 0.5).
// At tilt=+1.0 the highest bin is at +6dB, the lowest at -6dB.

class SpectralTilt final : public ISpectralProcessor {
public:
    void setTilt(double t) noexcept { tilt_ = std::clamp(t, -1.0, 1.0); }

    void setSampleRate(double sr) noexcept override { sr_ = sr; }
    void reset() noexcept override {}

    void process(SpectralFrame& frame) noexcept override {
        if (std::abs(tilt_) < 0.01) return;

        const std::size_t N = frame.half_size;
        const double inv_n = 1.0 / static_cast<double>(N);

        for (std::size_t k = 0; k <= N; ++k) {
            // Linear tilt: maps k=0 to (1 - tilt*0.5), k=N to (1 + tilt*0.5)
            const double gain = 1.0 + tilt_ * (static_cast<double>(k) * inv_n - 0.5);
            frame.mag[k] *= std::max(gain, 0.0);
        }
    }

private:
    double tilt_ = 0.0;
    double sr_   = 44100.0;
};
