#pragma once
#include <cmath>
#include <cstring>
#include <cstddef>
#include <algorithm>
#include "ISpectralProcessor.hpp"

// PhaseLockProcessor — ISpectralProcessor that implements peak phase locking.
//
// Problem it solves:
//   The standard phase vocoder updates synthesis phase independently per bin.
//   For bins near a spectral peak, this breaks the inter-bin phase coherence
//   that gives harmonic content its tonal character. You hear it as:
//   - Phasiness / metallic quality on pitched chords
//   - Loss of attack transient sharpness
//   - Unnatural sibilance on vocals
//
// Algorithm (Laroche-Dolson):
//   1. Find spectral peaks: local magnitude maxima above a noise floor.
//   2. For each non-peak bin k in a +/-N neighbourhood of the nearest peak p:
//      lock its true frequency to harmonic coherence with the peak:
//      true_freq[k] = true_freq[p] * (k / p).

class PhaseLockProcessor final : public ISpectralProcessor {
public:
    void setNeighbourhood(int n) noexcept {
        neighbourhood_ = std::clamp(n, 1, 32);
    }

    void setNoiseFloor(double floor) noexcept {
        noise_floor_ = std::clamp(floor, 0.0, 0.5);
    }

    void setEnabled(bool en) noexcept { enabled_ = en; }

    void setSampleRate(double sr) noexcept override { sr_ = sr; }

    void reset() noexcept override {
        std::memset(is_peak_, 0, sizeof(is_peak_));
    }

    void process(SpectralFrame& frame) noexcept override {
        if (!enabled_) return;

        const std::size_t H = frame.half_size;
        if (H == 0 || H >= MAX_BINS) return;

        // Step 1: find spectral peaks
        double frame_peak = 0.0;
        for (std::size_t k = 0; k <= H; ++k)
            frame_peak = std::max(frame_peak, frame.mag[k]);

        const double threshold = frame_peak * noise_floor_;

        std::memset(is_peak_, 0, (H + 1) * sizeof(bool));

        for (std::size_t k = 1; k < H; ++k) {
            if (frame.mag[k] > threshold
             && frame.mag[k] >= frame.mag[k - 1]
             && frame.mag[k] >= frame.mag[k + 1]) {
                is_peak_[k] = true;
            }
        }

        // Step 2: propagate peak frequency to neighbours
        for (std::size_t k = 0; k <= H; ++k) {
            if (is_peak_[k]) continue;

            int nearest_peak = -1;
            int min_dist     = neighbourhood_ + 1;

            for (int delta = -neighbourhood_; delta <= neighbourhood_; ++delta) {
                const int pk = static_cast<int>(k) + delta;
                if (pk < 0 || pk > static_cast<int>(H)) continue;
                if (is_peak_[pk] && std::abs(delta) < min_dist) {
                    min_dist     = std::abs(delta);
                    nearest_peak = pk;
                }
            }

            if (nearest_peak < 0 || nearest_peak == 0) continue;

            // Laroche-Dolson phase locking: force non-peak bins to maintain
            // harmonic coherence with the nearest peak by scaling the peak's
            // true frequency by the bin index ratio.
            const double peak_freq = frame.true_freq[nearest_peak];
            if (std::abs(peak_freq) < 1e-12) continue;

            frame.true_freq[k] = peak_freq
                * (static_cast<double>(k) / static_cast<double>(nearest_peak));
        }
    }

private:
    static constexpr std::size_t MAX_BINS = 1025;
    bool   is_peak_[MAX_BINS] = {};
    double sr_                = 44100.0;
    int    neighbourhood_ = 6;
    double noise_floor_   = 0.05;
    bool   enabled_       = true;
};
