#pragma once
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <algorithm>
#include "ISpectralProcessor.hpp"
#include "SpectralFrame.hpp"

// PhaseVocoder — FFT-based pitch shifter with pluggable spectral processing.
//
// Chain: input -> analysis window -> FFT -> phase accumulation ->
//        ISpectralProcessor chain -> IFFT -> synthesis window -> OLA output
//
// The FFT is implemented as a simple radix-2 DIT for self-containment.
// For production use one would swap in FFTW or pffft.

class PhaseVocoder {
public:
    static constexpr std::size_t FFT_SIZE = 4096;
    static constexpr std::size_t HALF     = FFT_SIZE / 2;
    static constexpr std::size_t HOP      = FFT_SIZE / 4;  // 75% overlap


    // MARK: LIFECYCLE

    PhaseVocoder() {
        std::memset(in_buf_,       0, sizeof(in_buf_));
        std::memset(out_buf_,      0, sizeof(out_buf_));
        std::memset(last_phase_,   0, sizeof(last_phase_));
        std::memset(sum_phase_,    0, sizeof(sum_phase_));
        std::memset(window_,       0, sizeof(window_));
        std::memset(fft_re_,       0, sizeof(fft_re_));
        std::memset(fft_im_,       0, sizeof(fft_im_));
        std::memset(mag_,          0, sizeof(mag_));
        std::memset(freq_,         0, sizeof(freq_));
        std::memset(syn_re_,       0, sizeof(syn_re_));
        std::memset(syn_im_,       0, sizeof(syn_im_));
        buildWindow();
        buildTwiddles();
    }

    void setSampleRate(double sr) {
        sr_ = sr;
        freq_per_bin_ = sr / static_cast<double>(FFT_SIZE);
        expect_phase_ = 2.0 * M_PI * static_cast<double>(HOP) / static_cast<double>(FFT_SIZE);
        if (processor_) processor_->setSampleRate(sr);
    }

    void setSemitones(double semi) noexcept {
        semitones_   = semi;
        pitch_ratio_ = std::pow(2.0, semi / 12.0);
    }

    void setSpectralProcessor(std::unique_ptr<ISpectralProcessor> proc) {
        processor_ = std::move(proc);
        if (processor_) processor_->setSampleRate(sr_);
    }

    void reset() noexcept {
        std::memset(in_buf_,     0, sizeof(in_buf_));
        std::memset(out_buf_,    0, sizeof(out_buf_));
        std::memset(last_phase_, 0, sizeof(last_phase_));
        std::memset(sum_phase_,  0, sizeof(sum_phase_));
        in_count_ = 0;
        out_read_ = 0;
        if (processor_) processor_->reset();
    }

    static uint32_t latencySamples() noexcept {
        return static_cast<uint32_t>(FFT_SIZE);
    }



    // MARK: PER-SAMPLE PROCESSING

    [[nodiscard]] double process(double input) noexcept {
        // Accumulate input
        in_buf_[in_count_] = input;
        const double output = out_buf_[out_read_];
        out_buf_[out_read_] = 0.0;
        out_read_ = (out_read_ + 1) % (FFT_SIZE * 2);
        ++in_count_;

        if (in_count_ >= HOP) {
            in_count_ = 0;
            processFrame();
        }

        return output;
    }

private:


    // MARK: -- internal state

    double in_buf_[FFT_SIZE]      = {};
    double out_buf_[FFT_SIZE * 2] = {};
    double last_phase_[HALF + 1]  = {};
    double sum_phase_[HALF + 1]   = {};
    double window_[FFT_SIZE]      = {};
    double fft_re_[FFT_SIZE]      = {};
    double fft_im_[FFT_SIZE]      = {};
    double mag_[HALF + 1]         = {};
    double freq_[HALF + 1]        = {};
    double syn_re_[FFT_SIZE]      = {};
    double syn_im_[FFT_SIZE]      = {};

    // Circular input accumulator for overlap
    double frame_buf_[FFT_SIZE]   = {};

    std::size_t in_count_  = 0;
    std::size_t out_read_  = 0;
    double sr_             = 44100.0;
    double semitones_      = 0.0;
    double pitch_ratio_    = 1.0;
    double freq_per_bin_   = 44100.0 / FFT_SIZE;
    double expect_phase_   = 2.0 * M_PI * HOP / FFT_SIZE;

    double ola_norm_           = 1.0 / 3.0;  // updated by buildWindow()
    double tw_re_[FFT_SIZE]    = {};         // pre-computed twiddle factors (real)
    double tw_im_[FFT_SIZE]    = {};         // pre-computed twiddle factors (imag)

    std::unique_ptr<ISpectralProcessor> processor_;



    // MARK: -- window and twiddle setup

    // Blackman-Harris 4-term window: -92dB sidelobe suppression vs Hann's -31dB.
    // Dramatically reduces spectral leakage ("butterfly" artifacts).
    void buildWindow() noexcept {
        constexpr double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
        const double N = static_cast<double>(FFT_SIZE);
        for (std::size_t i = 0; i < FFT_SIZE; ++i) {
            const double ph = static_cast<double>(i) / N;
            window_[i] = a0
                       - a1 * std::cos(2.0 * M_PI * ph)
                       + a2 * std::cos(4.0 * M_PI * ph)
                       - a3 * std::cos(6.0 * M_PI * ph);
        }
        // Compute WOLA normalisation numerically for this window at 75% overlap.
        // At each sample position, sum w²(n) from all 4 overlapping frames,
        // then average across the hop to get a stable gain factor.
        double wola_sum = 0.0;
        for (std::size_t n = 0; n < HOP; ++n) {
            double accum = 0.0;
            for (int k = 0; k < 4; ++k) {
                const int idx = static_cast<int>(n) + k * static_cast<int>(HOP);
                if (idx >= 0 && idx < static_cast<int>(FFT_SIZE))
                    accum += window_[idx] * window_[idx];
            }
            wola_sum += accum;
        }
        // norm compensates for: 2x magnitude mirror, WOLA sum, per-sample average
        ola_norm_ = static_cast<double>(HOP) / (2.0 * wola_sum);
    }

    // Pre-compute twiddle factor table for better FFT numerical accuracy.
    // Eliminates accumulated rounding error from recursive twiddle computation.
    void buildTwiddles() noexcept {
        for (std::size_t len = 2; len <= FFT_SIZE; len <<= 1) {
            const std::size_t half = len / 2;
            const double ang = -2.0 * M_PI / static_cast<double>(len);
            for (std::size_t j = 0; j < half; ++j) {
                const std::size_t idx = half + j;  // unique slot per (len, j)
                tw_re_[idx] = std::cos(ang * static_cast<double>(j));
                tw_im_[idx] = std::sin(ang * static_cast<double>(j));
            }
        }
    }



    // MARK: -- frame processing

    void processFrame() noexcept {
        // Shift input buffer and apply window
        std::memmove(frame_buf_, frame_buf_ + HOP, (FFT_SIZE - HOP) * sizeof(double));
        std::memcpy(frame_buf_ + (FFT_SIZE - HOP), in_buf_, HOP * sizeof(double));

        for (std::size_t i = 0; i < FFT_SIZE; ++i) {
            fft_re_[i] = frame_buf_[i] * window_[i];
            fft_im_[i] = 0.0;
        }

        // Forward FFT
        fft(fft_re_, fft_im_, false);

        // Analysis: extract magnitude and true frequency
        for (std::size_t k = 0; k <= HALF; ++k) {
            const double re = fft_re_[k];
            const double im = fft_im_[k];
            mag_[k] = 2.0 * std::sqrt(re * re + im * im);

            const double ph = std::atan2(im, re);
            double dp = ph - last_phase_[k];
            last_phase_[k] = ph;

            dp -= static_cast<double>(k) * expect_phase_;
            dp = dp - 2.0 * M_PI * std::round(dp / (2.0 * M_PI));

            freq_[k] = static_cast<double>(k) * freq_per_bin_
                      + dp * freq_per_bin_ / expect_phase_;
        }

        // Run spectral processor chain if installed
        if (processor_) {
            SpectralFrame frame;
            frame.mag         = mag_;
            frame.phase       = last_phase_;
            frame.true_freq   = freq_;
            frame.half_size   = HALF;
            frame.pitch_ratio = pitch_ratio_;
            frame.sample_rate = sr_;
            processor_->process(frame);
        }

        // Synthesis: pitch-shift by interpolated spectral resampling.
        // Each output bin reads from the corresponding input position via
        // linear interpolation — no bin collisions, no spectral gaps.
        std::memset(syn_re_, 0, sizeof(syn_re_));
        std::memset(syn_im_, 0, sizeof(syn_im_));

        for (std::size_t nb = 0; nb <= HALF; ++nb) {
            const double src = static_cast<double>(nb) / pitch_ratio_;
            const std::size_t s0 = static_cast<std::size_t>(src);
            if (s0 > HALF) break;
            const double frac = src - static_cast<double>(s0);

            double m, f;
            if (s0 + 1 <= HALF) {
                m = mag_[s0] * (1.0 - frac) + mag_[s0 + 1] * frac;
                f = (freq_[s0] * (1.0 - frac) + freq_[s0 + 1] * frac)
                  * pitch_ratio_;
            } else {
                m = mag_[s0];
                f = freq_[s0] * pitch_ratio_;
            }

            const double dp = (f - static_cast<double>(nb) * freq_per_bin_)
                            * expect_phase_ / freq_per_bin_;
            // std::remainder wraps to [-pi, pi], preventing unbounded growth
            // of sum_phase_ that degrades precision on extended sessions.
            sum_phase_[nb] = std::remainder(
                sum_phase_[nb] + static_cast<double>(nb) * expect_phase_ + dp,
                2.0 * M_PI);

            syn_re_[nb] = m * std::cos(sum_phase_[nb]);
            syn_im_[nb] = m * std::sin(sum_phase_[nb]);

            if (nb > 0 && nb < HALF) {
                syn_re_[FFT_SIZE - nb] = syn_re_[nb];
                syn_im_[FFT_SIZE - nb] = -syn_im_[nb];
            }
        }

        // Inverse FFT
        fft(syn_re_, syn_im_, true);

        // Synthesis window + overlap-add with numerically computed normalisation
        for (std::size_t i = 0; i < FFT_SIZE; ++i) {
            const std::size_t pos = (out_read_ + i) % (FFT_SIZE * 2);
            out_buf_[pos] += syn_re_[i] * window_[i] * ola_norm_;
        }
    }



    // MARK: -- fft

    // Radix-2 DIT FFT using pre-computed twiddle factor table.
    void fft(double* re, double* im, bool inverse) const noexcept {
        const std::size_t n = FFT_SIZE;

        // Bit-reversal permutation
        for (std::size_t i = 1, j = 0; i < n; ++i) {
            std::size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }

        // Cooley-Tukey with pre-computed twiddles
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const std::size_t half = len / 2;
            const double sign = inverse ? -1.0 : 1.0;
            for (std::size_t i = 0; i < n; i += len) {
                for (std::size_t j = 0; j < half; ++j) {
                    const std::size_t tw_idx = half + j;
                    const double wr = tw_re_[tw_idx];
                    const double wi = sign * tw_im_[tw_idx];
                    const std::size_t u = i + j;
                    const std::size_t v = u + half;
                    const double tre = re[v] * wr - im[v] * wi;
                    const double tim = re[v] * wi + im[v] * wr;
                    re[v] = re[u] - tre;
                    im[v] = im[u] - tim;
                    re[u] += tre;
                    im[u] += tim;
                }
            }
        }

        if (inverse) {
            const double inv_n = 1.0 / static_cast<double>(n);
            for (std::size_t i = 0; i < n; ++i) {
                re[i] *= inv_n;
                im[i] *= inv_n;
            }
        }
    }
};
