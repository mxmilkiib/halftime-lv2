#pragma once
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <algorithm>

// TransientLookaheadScheduler — wraps an onset detector result with a
// short delay buffer so grain boundaries can be placed AHEAD of the
// detected onset rather than one block behind it.
//
// The problem:
//   Our 3-band transient detector fires at time T when it observes
//   energy flux at time T. The WSOLA grain snap therefore latches at T,
//   but the actual transient happened slightly before T due to the
//   smoother's group delay (~5ms). The result: grain boundaries land
//   just after the beat, which sounds rhythmically late.
//
// The fix:
//   Buffer LOOKAHEAD_MS of audio before it reaches the OLA engine.
//   Run the transient detector on the buffered audio.
//   When an onset is detected, the grain snap fires at the onset's
//   actual position in the delayed stream.
//
// Latency cost: LOOKAHEAD_MS samples added to total plugin latency.

class TransientLookaheadScheduler {
public:
    static constexpr double LOOKAHEAD_MS = 10.0;
    static constexpr std::size_t MAX_LOOKAHEAD = 2048;

    void setSampleRate(double sr) noexcept {
        sr_          = sr;
        lookahead_   = static_cast<std::size_t>(LOOKAHEAD_MS * sr / 1000.0);
        lookahead_   = std::clamp(lookahead_, std::size_t{1}, MAX_LOOKAHEAD);
    }

    void reset() noexcept {
        std::memset(delay_buf_, 0, sizeof(delay_buf_));
        std::memset(onset_ring_, 0, sizeof(onset_ring_));
        write_ = 0;
        last_onset_ = false;
    }

    // Push one input sample and onset detection result.
    // Returns the delayed output sample — use this as input to OlaEngine.
    [[nodiscard]] double push(double input, bool onset_detected) noexcept {
        // Read delayed output BEFORE writing — gives exactly lookahead_ samples delay
        const std::size_t read_pos = (write_ + MAX_LOOKAHEAD - lookahead_) % MAX_LOOKAHEAD;
        const double delayed = delay_buf_[read_pos];
        last_onset_ = onset_ring_[read_pos];

        // Write new sample
        onset_ring_[write_] = onset_detected;
        delay_buf_[write_] = input;
        write_ = (write_ + 1) % MAX_LOOKAHEAD;

        return delayed;
    }

    // Returns true if an onset that was detected LOOKAHEAD samples ago
    // is now emerging from the delay line.
    [[nodiscard]] bool onsetNow() const noexcept {
        return last_onset_;
    }

    [[nodiscard]] uint32_t latencySamples() const noexcept {
        return static_cast<uint32_t>(lookahead_);
    }

private:
    double delay_buf_[MAX_LOOKAHEAD]  = {};
    bool   onset_ring_[MAX_LOOKAHEAD] = {};
    std::size_t write_     = 0;
    std::size_t lookahead_ = 441;
    double      sr_        = 44100.0;
    bool        last_onset_= false;
};
