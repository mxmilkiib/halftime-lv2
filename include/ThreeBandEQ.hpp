#pragma once
#include <cmath>

// Three-band EQ using biquad filters (transposed direct-form II).
// Isolator-style design: Low shelf at 250 Hz, high-Q mid peak at 1.2 kHz, high shelf at 4 kHz.
// Steeper slopes (Q=0.5) for better frequency separation.
// Gains are in dB, range [-48, +12] for deep cuts.

class ThreeBandEQ {
public:
    void setSampleRate(double sr) noexcept {
        sr_ = sr;
        recalc();
    }

    void setLowGain(double dB)  noexcept { low_dB_  = dB; recalc_low();  }
    void setMidGain(double dB)  noexcept { mid_dB_  = dB; recalc_mid();  }
    void setHighGain(double dB) noexcept { high_dB_ = dB; recalc_high(); }

    double process(double x) noexcept {
        x = biquad(low_,  x);
        x = biquad(mid_,  x);
        x = biquad(high_, x);
        return x;
    }

    void reset() noexcept {
        low_.z1  = low_.z2  = 0.0;
        mid_.z1  = mid_.z2  = 0.0;
        high_.z1 = high_.z2 = 0.0;
    }

private:
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };

    static double biquad(Biquad& f, double x) noexcept {
        const double y = f.b0 * x + f.z1;
        f.z1 = f.b1 * x - f.a1 * y + f.z2;
        f.z2 = f.b2 * x - f.a2 * y;
        return y;
    }

    void recalc_low()  noexcept { designShelf(low_,  250.0,  low_dB_,  true);  }
    void recalc_mid()  noexcept { designPeak (mid_,  1200.0, mid_dB_,  0.5);   }
    void recalc_high() noexcept { designShelf(high_, 4000.0, high_dB_, false); }
    void recalc()      noexcept { recalc_low(); recalc_mid(); recalc_high(); }

    void designShelf(Biquad& f, double freq, double dB, bool low) noexcept {
        if (std::abs(dB) < 0.01) {
            f.b0 = 1.0; f.b1 = 0.0; f.b2 = 0.0;
            f.a1 = 0.0; f.a2 = 0.0;
            return;
        }
        const double A     = std::pow(10.0, dB / 40.0);
        const double w0    = 2.0 * M_PI * freq / sr_;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * 0.5);  // Steeper slope for isolator
        const double sqrtA = std::sqrt(A);
        double b0, b1, b2, a0, a1, a2;
        if (low) {
            b0 = A * ((A+1) - (A-1)*cosw0 + 2*sqrtA*alpha);
            b1 = 2*A * ((A-1) - (A+1)*cosw0);
            b2 = A * ((A+1) - (A-1)*cosw0 - 2*sqrtA*alpha);
            a0 = (A+1) + (A-1)*cosw0 + 2*sqrtA*alpha;
            a1 = -2 * ((A-1) + (A+1)*cosw0);
            a2 = (A+1) + (A-1)*cosw0 - 2*sqrtA*alpha;
        } else {
            b0 = A * ((A+1) + (A-1)*cosw0 + 2*sqrtA*alpha);
            b1 = -2*A * ((A-1) + (A+1)*cosw0);
            b2 = A * ((A+1) + (A-1)*cosw0 - 2*sqrtA*alpha);
            a0 = (A+1) - (A-1)*cosw0 + 2*sqrtA*alpha;
            a1 = 2 * ((A-1) - (A+1)*cosw0);
            a2 = (A+1) - (A-1)*cosw0 - 2*sqrtA*alpha;
        }
        f.b0 = b0/a0; f.b1 = b1/a0; f.b2 = b2/a0;
        f.a1 = a1/a0; f.a2 = a2/a0;
    }

    void designPeak(Biquad& f, double freq, double dB, double Q) noexcept {
        if (std::abs(dB) < 0.01) {
            f.b0 = 1.0; f.b1 = 0.0; f.b2 = 0.0;
            f.a1 = 0.0; f.a2 = 0.0;
            return;
        }
        const double A     = std::pow(10.0, dB / 40.0);
        const double w0    = 2.0 * M_PI * freq / sr_;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * Q);
        const double b0 = 1 + alpha * A;
        const double b1 = -2 * cosw0;
        const double b2 = 1 - alpha * A;
        const double a0 = 1 + alpha / A;
        const double a1 = -2 * cosw0;
        const double a2 = 1 - alpha / A;
        f.b0 = b0/a0; f.b1 = b1/a0; f.b2 = b2/a0;
        f.a1 = a1/a0; f.a2 = a2/a0;
    }

    double sr_ = 44100.0;
    double low_dB_ = 0.0, mid_dB_ = 0.0, high_dB_ = 0.0;
    Biquad low_, mid_, high_;
};
