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
    static constexpr std::size_t FFT_SIZE = 2048;
    static constexpr std::size_t HALF     = FFT_SIZE / 2;
    static constexpr std::size_t HOP      = FFT_SIZE / 4;  // 75% overlap

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
    }

    void setSampleRate(double sr) {
        sr_ = sr;
        freq_per_bin_ = sr / static_cast<double>(FFT_SIZE);
        expect_phase_ = 2.0 * M_PI * static_cast<double>(HOP) / static_cast<double>(FFT_SIZE);
        if (processor_) processor_->setSampleRate(sr);
    }

    void setSemitones(double semi) noexcept {
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
    std::size_t frame_pos_        = 0;

    std::size_t in_count_  = 0;
    std::size_t out_read_  = 0;
    double sr_             = 44100.0;
    double pitch_ratio_    = 1.0;
    double freq_per_bin_   = 44100.0 / FFT_SIZE;
    double expect_phase_   = 2.0 * M_PI * HOP / FFT_SIZE;

    std::unique_ptr<ISpectralProcessor> processor_;

    void buildWindow() noexcept {
        for (std::size_t i = 0; i < FFT_SIZE; ++i)
            window_[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / FFT_SIZE);
    }

    void processFrame() noexcept {
        // Shift input buffer and apply window
        // Move old data left by HOP, append new HOP samples
        std::memmove(frame_buf_, frame_buf_ + HOP, (FFT_SIZE - HOP) * sizeof(double));
        std::memcpy(frame_buf_ + (FFT_SIZE - HOP), in_buf_, HOP * sizeof(double));

        for (std::size_t i = 0; i < FFT_SIZE; ++i) {
            fft_re_[i] = frame_buf_[i] * window_[i];
            fft_im_[i] = 0.0;
        }

        // Forward FFT
        fft(fft_re_, fft_im_, FFT_SIZE, false);

        // Analysis: extract magnitude and true frequency
        for (std::size_t k = 0; k <= HALF; ++k) {
            const double re = fft_re_[k];
            const double im = fft_im_[k];
            mag_[k] = 2.0 * std::sqrt(re * re + im * im);

            const double ph = std::atan2(im, re);
            double dp = ph - last_phase_[k];
            last_phase_[k] = ph;

            // Remove expected phase advance
            dp -= static_cast<double>(k) * expect_phase_;

            // Wrap to [-pi, pi]
            dp = dp - 2.0 * M_PI * std::round(dp / (2.0 * M_PI));

            // True frequency = bin frequency + deviation
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

        // Synthesis: pitch-shift by resampling the spectrum
        std::memset(syn_re_, 0, sizeof(syn_re_));
        std::memset(syn_im_, 0, sizeof(syn_im_));

        for (std::size_t k = 0; k <= HALF; ++k) {
            const std::size_t new_bin = static_cast<std::size_t>(
                std::round(static_cast<double>(k) * pitch_ratio_));
            if (new_bin > HALF) continue;

            // Phase accumulation for synthesis bin
            const double new_freq = freq_[k] * pitch_ratio_;
            const double dp = (new_freq - static_cast<double>(new_bin) * freq_per_bin_)
                            * expect_phase_ / freq_per_bin_;
            sum_phase_[new_bin] += static_cast<double>(new_bin) * expect_phase_ + dp;

            syn_re_[new_bin] += mag_[k] * std::cos(sum_phase_[new_bin]);
            syn_im_[new_bin] += mag_[k] * std::sin(sum_phase_[new_bin]);

            // Mirror for negative frequencies
            if (new_bin > 0 && new_bin < HALF) {
                syn_re_[FFT_SIZE - new_bin] = syn_re_[new_bin];
                syn_im_[FFT_SIZE - new_bin] = -syn_im_[new_bin];
            }
        }

        // Inverse FFT
        fft(syn_re_, syn_im_, FFT_SIZE, true);

        // Window and overlap-add into output buffer
        const double norm = 1.0 / (FFT_SIZE * 0.5); // normalise for 75% overlap
        for (std::size_t i = 0; i < FFT_SIZE; ++i) {
            const std::size_t pos = (out_read_ + i) % (FFT_SIZE * 2);
            out_buf_[pos] += syn_re_[i] * window_[i] * norm;
        }
    }

    // Radix-2 DIT FFT (in-place). inverse=true for IFFT.
    static void fft(double* re, double* im, std::size_t n, bool inverse) noexcept {
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

        // Cooley-Tukey butterfly
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const double ang = (inverse ? 2.0 : -2.0) * M_PI / static_cast<double>(len);
            const double wre = std::cos(ang);
            const double wim = std::sin(ang);
            for (std::size_t i = 0; i < n; i += len) {
                double cur_re = 1.0, cur_im = 0.0;
                for (std::size_t j = 0; j < len / 2; ++j) {
                    const std::size_t u = i + j;
                    const std::size_t v = i + j + len / 2;
                    const double tre = re[v] * cur_re - im[v] * cur_im;
                    const double tim = re[v] * cur_im + im[v] * cur_re;
                    re[v] = re[u] - tre;
                    im[v] = im[u] - tim;
                    re[u] += tre;
                    im[u] += tim;
                    const double new_re = cur_re * wre - cur_im * wim;
                    cur_im = cur_re * wim + cur_im * wre;
                    cur_re = new_re;
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
