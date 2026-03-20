#pragma once
#include <cmath>
#include <algorithm>

// One-pole lowpass smoother for control parameters.
// Eliminates zipper noise when knobs move between audio blocks.
// Time constant: ~20ms by default — fast enough to track gestures,
// slow enough to be inaudible as a click.

class ParamSmoother {
public:
    void setSampleRate(double sr, double time_ms = 20.0) {
        // Compute one-pole coefficient from time constant
        coeff_ = std::exp(-1.0 / (sr * time_ms / 1000.0));
        // Clamp to valid range — avoids NaN if called before SR is set
        coeff_ = std::max(0.0, std::min(coeff_, 0.9999));
    }

    void reset(double value) noexcept { current_ = target_ = value; }
    void setTarget(double value) noexcept { target_ = value; }
    double target() const noexcept { return target_; }

    [[nodiscard]] double next() noexcept {
        current_ = current_ * coeff_ + target_ * (1.0 - coeff_);
        return current_;
    }

    // Returns true once the smoother is settled (within epsilon of target).
    // Useful for skipping processing on idle plugins.
    [[nodiscard]] bool settled(double eps = 1e-6) const noexcept {
        return std::abs(current_ - target_) < eps;
    }

private:
    double coeff_   = 0.99;
    double current_ = 0.0;
    double target_  = 0.0;
};
