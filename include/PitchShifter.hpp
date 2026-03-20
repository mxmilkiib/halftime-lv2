#pragma once
#include <cmath>
#include <cstring>
#include <cstddef>

// PitchShifter — grain-based pitch shifting via resampled OLA.
// Operates independently of the main OLA engine so pitch can be
// applied before or after the time-stretch stage.
// semitones: -12 to +12. 0 = bypass (cheap check, no processing).

class PitchShifter {
public:
    static constexpr std::size_t MAX_BUF = 131072;

    explicit PitchShifter() { std::memset(buf_, 0, sizeof(buf_)); }

    void setSampleRate(double sr) { sr_ = sr; }
    void setGrainSamples(std::size_t gs) { grain_samps_ = gs; }

    void setSemitones(double semi) noexcept {
        semitones_ = semi;
        ratio_     = std::pow(2.0, semi / 12.0);
    }

    void reset() noexcept {
        std::memset(buf_, 0, sizeof(buf_));
        write_ptr_ = 0;
        phase_[0] = phase_[1] = 0.0;
        start_[0] = start_[1] = 0;
    }

    [[nodiscard]] double process(double input) noexcept {
        // Write
        buf_[write_ptr_] = input;
        write_ptr_ = (write_ptr_ + 1) % MAX_BUF;

        // Bypass when pitch shift is negligible
        if (std::abs(semitones_) < 0.05) return input;

        const std::size_t gs = grain_samps_;
        double out = 0.0;

        for (int g = 0; g < 2; ++g) {
            const std::size_t offset = static_cast<std::size_t>(g) * (gs / 2);

            // Latch grain start
            const std::size_t pi = static_cast<std::size_t>(phase_[g]);
            if (pi == 0)
                start_[g] = (write_ptr_ + MAX_BUF - gs + offset) % MAX_BUF;

            // Read at ratio-scaled rate with linear interpolation
            const double   read_f = static_cast<double>((start_[g] + offset) % MAX_BUF)
                                  + phase_[g] * ratio_;
            const std::size_t r0  = static_cast<std::size_t>(read_f) % MAX_BUF;
            const std::size_t r1  = (r0 + 1) % MAX_BUF;
            const double frac     = read_f - std::floor(read_f);
            const double samp     = buf_[r0] * (1.0 - frac) + buf_[r1] * frac;
            const double win      = hann(static_cast<double>(pi) / static_cast<double>(gs));

            out += samp * win;
            phase_[g] = std::fmod(phase_[g] + 1.0, static_cast<double>(gs));
        }

        return out;
    }

private:
    double      buf_[MAX_BUF];
    std::size_t write_ptr_   = 0;
    double      phase_[2]    = {0.0, 0.0};
    std::size_t start_[2]    = {0, 0};
    std::size_t grain_samps_ = 3528;
    double      ratio_       = 1.0;
    double      semitones_   = 0.0;
    double      sr_          = 44100.0;

    static double hann(double ph) noexcept {
        return 0.5 - 0.5 * std::cos(2.0 * M_PI * ph);
    }
};
