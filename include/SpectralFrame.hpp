#pragma once
#include <cstddef>

// SpectralFrame — data passed between the PhaseVocoder and ISpectralProcessor chain.
// All pointers are owned by the vocoder — processors must not free them.
// half_size = fft_size / 2. Arrays are indexed [0..half_size] inclusive.

struct SpectralFrame {
    double*     mag;          // magnitude spectrum [0..half_size]
    double*     phase;        // phase spectrum [0..half_size]
    double*     true_freq;    // instantaneous frequency per bin [0..half_size]
    std::size_t half_size;    // fft_size / 2
    double      pitch_ratio;  // current pitch shift ratio (1.0 = no shift)
    double      sample_rate;
};
