#pragma once
#include <cmath>
#include <algorithm>

// SubBassEnhancer — generates sub-harmonics below the fundamental.
//
// Two modes:
//   RingMod:  multiply input by a half-frequency sine derived from
//             zero-crossing tracking. Produces a pitched octave-down.
//   HalfWave: half-wave rectify and lowpass — produces a warm,
//             less pitched sub rumble.
//
// gain: 0.0 = bypass, 1.0 = full sub mixed in (additive, not replace).

class SubBassEnhancer {
public:
    enum class Mode : int {
        RingMod  = 0,
        HalfWave = 1,
    };



    // MARK: LIFECYCLE

    void setSampleRate(double sr) noexcept {
        sr_ = sr;
        updateCoeff();
    }

    void setGain(double g) noexcept { gain_ = std::clamp(g, 0.0, 1.0); }
    void setMode(Mode m) noexcept { mode_ = m; }

    void setFreq(double hz) noexcept {
        freq_ = std::clamp(hz, 30.0, 200.0);
        updateCoeff();
    }

    void setDrive(double d) noexcept { drive_ = std::clamp(d, 0.0, 1.0); }

    void reset() noexcept {
        phase_     = 0.0;
        prev_sign_ = false;
        period_    = 0.0;
        counter_   = 0;
        lp_state_  = 0.0;
        zc_lp_     = 0.0;
    }



    // MARK: PER-SAMPLE PROCESSING

    [[nodiscard]] double process(double x) noexcept {
        if (gain_ < 0.001) return x;

        double sub = 0.0;

        switch (mode_) {
            case Mode::RingMod: {
                // Lowpass input for stable zero-crossing detection
                zc_lp_ += zc_lp_coeff_ * (x - zc_lp_);
                const bool sign = zc_lp_ >= 0.0;
                if (sign != prev_sign_) {
                    // Reject periods outside musically useful range
                    const double min_period = sr_ / 500.0;
                    const double max_period = sr_ / 20.0;
                    if (counter_ >= static_cast<int>(min_period)
                     && counter_ <= static_cast<int>(max_period))
                        period_ = period_ * 0.9 + static_cast<double>(counter_) * 0.1;
                    counter_ = 0;
                }
                prev_sign_ = sign;
                ++counter_;

                // Generate half-frequency sine (octave down)
                if (period_ > 2.0) {
                    const double freq = sr_ / (period_ * 2.0); // half fundamental
                    phase_ += freq / sr_;
                    if (phase_ > 1.0) phase_ -= 1.0;
                    sub = std::sin(2.0 * M_PI * phase_) * std::abs(x);
                }
                break;
            }
            case Mode::HalfWave: {
                // Half-wave rectification + lowpass = warm sub content
                const double rect = std::max(x, 0.0);
                lp_state_ += lp_coeff_ * (rect - lp_state_);
                sub = lp_state_ * 2.0; // compensate for half-wave energy loss
                break;
            }
        }

        // Soft-clip drive on the sub signal
        if (drive_ > 0.001) {
            const double k = 1.0 + drive_ * 9.0; // 1x..10x gain into tanh
            sub = std::tanh(sub * k) / std::tanh(k);
        }

        return x + sub * gain_;
    }

private:
    double sr_       = 44100.0;
    double gain_     = 0.0;
    double freq_     = 80.0;
    double drive_    = 0.0;
    Mode   mode_     = Mode::RingMod;
    double phase_    = 0.0;
    bool   prev_sign_= false;
    double period_   = 0.0;
    int    counter_  = 0;
    double lp_state_    = 0.0;
    double lp_coeff_    = 0.011;  // ~80Hz @ 44.1kHz
    double zc_lp_       = 0.0;   // lowpass state for zero-crossing input
    double zc_lp_coeff_ = 0.038; // ~300Hz @ 44.1kHz

    void updateCoeff() noexcept {
        if (sr_ > 0.0) {
            lp_coeff_    = 1.0 - std::exp(-2.0 * M_PI * freq_ / sr_);
            zc_lp_coeff_ = 1.0 - std::exp(-2.0 * M_PI * 300.0 / sr_);
        }
    }
};
