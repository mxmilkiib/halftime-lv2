#pragma once
#include <cstring>
#include <cstddef>
#include <cmath>
#include <algorithm>

// StutterGrid — click-free rhythmic looper.
//
// Fix: the previous hard loop reset is replaced with a short Hann-windowed
// crossfade spanning the first and last XFADE_LEN samples of the loop.
// This eliminates the click that occurred every stutter_len samples when
// the read head wrapped discontinuously.

class StutterGrid {
public:
    static constexpr std::size_t BUF       = 65536;
    static constexpr std::size_t XFADE_LEN = 64;  // ~1.5ms @ 44.1kHz — inaudible


    // MARK: LIFECYCLE

    explicit StutterGrid() {
        std::memset(buf_, 0, sizeof(buf_));
        std::memset(prev_tail_, 0, sizeof(prev_tail_));
    }

    void setSampleRate(double) noexcept {}

    void setDiv(int div) noexcept {
        next_div_ = std::max(1, std::min(div, 16));
    }

    void reset() noexcept {
        std::memset(buf_,       0, sizeof(buf_));
        std::memset(prev_tail_, 0, sizeof(prev_tail_));
        write_ = 0;
        phase_ = 0;
    }



    // MARK: PER-SAMPLE PROCESSING

    [[nodiscard]] double process(double input, std::size_t grain_samps) noexcept {
        // Write input
        buf_[write_] = input;
        write_ = (write_ + 1) % BUF;

        // Update div at grain boundary only — avoids mid-loop div changes
        if (phase_ == 0) div_ = next_div_;

        if (div_ <= 1) { prev_loop_len_ = 0; return input; }

        const std::size_t loop_len = std::max(grain_samps / static_cast<std::size_t>(div_),
                                              XFADE_LEN * 2);

        // When loop_len shrinks below phase_, force a crossfade to avoid
        // discontinuous read-head jumps (the "zipper noise" on preset switch).
        if (prev_loop_len_ > 0 && loop_len != prev_loop_len_ && phase_ >= loop_len) {
            // Capture current tail for crossfade and restart the loop
            for (std::size_t k = 0; k < XFADE_LEN; ++k) {
                const std::size_t rd = (write_ + BUF - XFADE_LEN + k) % BUF;
                prev_tail_[k] = buf_[rd];
            }
            phase_ = 0;
        }
        prev_loop_len_ = loop_len;

        // Read head — wraps at loop_len
        const std::size_t read = (write_ + BUF - loop_len + phase_) % BUF;
        const double raw = buf_[read];

        // Crossfade at the loop wrap boundary using Hann window taper
        double out = raw;

        if (phase_ < XFADE_LEN) {
            // Fade-in from previous tail
            const double alpha = hann(static_cast<double>(phase_) / XFADE_LEN * 0.5);
            const double prev  = prev_tail_[phase_];
            out = raw * alpha + prev * (1.0 - alpha);
        } else if (phase_ >= loop_len - XFADE_LEN) {
            // Record outgoing tail for next crossfade reference
            const std::size_t tail_idx = phase_ - (loop_len - XFADE_LEN);
            if (tail_idx < XFADE_LEN) prev_tail_[tail_idx] = raw;
        }

        phase_ = (phase_ + 1) % loop_len;
        return out;
    }

private:
    double buf_[BUF];
    double prev_tail_[XFADE_LEN];
    std::size_t write_         = 0;
    std::size_t phase_         = 0;
    std::size_t prev_loop_len_ = 0;
    int         div_           = 1;
    int         next_div_      = 1;

    static double hann(double ph) noexcept {
        return 0.5 - 0.5 * std::cos(2.0 * M_PI * ph);
    }
};
