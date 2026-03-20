#pragma once
#include <cmath>
#include <algorithm>

// DcBlocker — first-order highpass at ~10 Hz.
// Removes DC offset accumulated by OLA summation, especially under freeze.
// R=0.9997 gives -3dB at ~10 Hz @ 44.1kHz.
// Effectively zero latency (1 sample group delay at audio frequencies).

class DcBlocker {
public:
    void setSampleRate(double sr) noexcept {
        // Coefficient computed from bilinear transform of RC highpass
        // fc = 10 Hz
        const double fc = 10.0;
        r_ = 1.0 - (2.0 * M_PI * fc / sr);
        r_ = std::max(0.99, std::min(r_, 0.9999));
    }

    [[nodiscard]] double process(double x) noexcept {
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        const double y = x - x_prev_ + r_ * y_prev_;
        x_prev_ = x;
        y_prev_ = y;
        return y;
    }

    void reset() noexcept { x_prev_ = y_prev_ = 0.0; }

private:
    double r_      = 0.9997;
    double x_prev_ = 0.0;
    double y_prev_ = 0.0;
};
