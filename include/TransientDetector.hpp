#pragma once
#include <cmath>
#include <cstdint>
#include <array>
#include <algorithm>
#include <cstring>

// TransientDetector — 3-band adaptive spectral flux onset detector.
//
// Splits the signal into three bands via cascaded one-pole filters:
//   Low  : < ~200 Hz  (kick, bass fundamentals)
//   Mid  : 200 Hz–4 kHz (snare, most instruments)
//   High : > 4 kHz    (hi-hats, transient brightness)
//
// Each band maintains its own adaptive threshold (median-tracker
// approximated with a leaky max + decay). A global onset fires when
// the weighted sum of per-band flux exceeds 1.0.

class TransientDetector {
public:


    // MARK: LIFECYCLE

    void setSampleRate(double sr) {
        sr_ = sr;
        hold_max_ = static_cast<uint32_t>(sr * 0.05); // 50ms inter-onset holdoff

        // Compute one-pole coefficients for the crossover filters.
        lp_low_  = lpCoeff(200.0,  sr);
        lp_mid_  = lpCoeff(4000.0, sr);

        // Adaptive threshold time constants
        for (auto& b : bands_) {
            b.attack_coeff  = std::exp(-1.0 / (sr * 0.005)); // 5ms attack
            b.release_coeff = std::exp(-1.0 / (sr * 0.3));   // 300ms release
        }
    }

    void reset() noexcept {
        for (auto& b : bands_) {
            b.prev_energy = 0.0;
            b.smooth      = 0.0;
            b.threshold   = 1e-4;
        }
        std::memset(lp_low_state_, 0, sizeof(lp_low_state_));
        std::memset(lp_mid_state_, 0, sizeof(lp_mid_state_));
        hold_ = 0;
    }



    // MARK: PER-SAMPLE PROCESSING

    [[nodiscard]] bool process(double x) noexcept {
        // 3-band split via cascaded lowpass filters
        lp_low_state_[0] += lp_low_ * (x               - lp_low_state_[0]);
        lp_low_state_[1] += lp_low_ * (lp_low_state_[0] - lp_low_state_[1]);
        const double low  = lp_low_state_[1];

        lp_mid_state_[0] += lp_mid_ * (x               - lp_mid_state_[0]);
        lp_mid_state_[1] += lp_mid_ * (lp_mid_state_[0] - lp_mid_state_[1]);
        const double mid_lp = lp_mid_state_[1];
        const double mid  = mid_lp - low;

        const double high = x - mid_lp;

        const std::array<double, 3> band_sigs = {low, mid, high};
        // Perceptual weights — high band carries more transient info
        const std::array<double, 3> weights   = {0.5, 1.0, 1.5};

        double weighted_flux = 0.0;

        for (int b = 0; b < 3; ++b) {
            Band& bd = bands_[b];
            const double energy = band_sigs[b] * band_sigs[b];

            // Half-wave rectified flux
            const double flux = std::max(energy - bd.prev_energy, 0.0);
            bd.prev_energy    = energy;

            // Smooth flux with a short lowpass
            bd.smooth = bd.smooth * 0.9 + flux * 0.1;

            // Adaptive threshold: rises on flux, decays slowly
            if (bd.smooth > bd.threshold)
                bd.threshold = bd.smooth * (1.0 - bd.attack_coeff)
                             + bd.threshold * bd.attack_coeff;
            else
                bd.threshold = bd.threshold * bd.release_coeff;

            bd.threshold = std::max(bd.threshold, 1e-8); // noise floor

            // Normalised flux (relative to current threshold)
            const double norm_flux = bd.smooth / bd.threshold;
            weighted_flux += norm_flux * weights[b];
        }

        // Global holdoff — prevents re-triggering on the same transient
        if (hold_ > 0) { --hold_; return false; }

        // Trigger when weighted flux exceeds threshold scaled by sensitivity
        if (weighted_flux > 2.5 / sensitivity_) {
            hold_ = hold_max_;
            return true;
        }
        return false;
    }

    // Sensitivity trim: higher = more triggers, lower = fewer
    // Range 0.5–2.0, default 1.0
    void setSensitivity(double s) noexcept {
        sensitivity_ = std::clamp(s, 0.5, 2.0);
    }

private:
    struct Band {
        double prev_energy   = 0.0;
        double smooth        = 0.0;
        double threshold     = 1e-4;
        double attack_coeff  = 0.99;
        double release_coeff = 0.999;
    };

    std::array<Band, 3> bands_;
    double lp_low_state_[2] = {0.0, 0.0};
    double lp_mid_state_[2] = {0.0, 0.0};
    double lp_low_  = 0.028;
    double lp_mid_  = 0.43;
    double sr_      = 44100.0;
    double sensitivity_ = 1.0;
    uint32_t hold_      = 0;
    uint32_t hold_max_  = 2205;

    static double lpCoeff(double freq_hz, double sr) noexcept {
        const double w = 2.0 * M_PI * freq_hz / sr;
        return 1.0 - std::exp(-w);
    }
};
