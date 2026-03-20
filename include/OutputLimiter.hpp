#pragma once
#include <cmath>
#include <algorithm>

// OutputLimiter — soft-knee limiter using tanh saturation.
//
// Not a true brickwall limiter — that would need look-ahead.
// This is a memoryless saturator tuned so that signals up to ~2dBFS
// (the OLA worst-case overshoot) are brought back to 0dBFS with
// minimal harmonic distortion.
//
// Transfer function: out = tanh(in * drive) / drive
// At drive=1.5 the knee is around -6dBFS, ceiling at 0dBFS.
// The division normalises unity gain in the linear region.

class OutputLimiter {
public:
    // drive: 1.0 = gentle, 2.0 = tighter ceiling, higher = more saturation
    explicit OutputLimiter(double drive = 1.5) noexcept { setDrive(drive); }

    void setDrive(double drive) noexcept {
        drive_     = std::max(1.0, drive);
        inv_drive_ = 1.0 / drive_;
    }

    [[nodiscard]] double process(double x) noexcept {
        return std::tanh(x * drive_) * inv_drive_;
    }

    // Stereo convenience — process a sample pair in one call
    void process(double& l, double& r) noexcept {
        l = process(l);
        r = process(r);
    }

private:
    double drive_     = 1.5;
    double inv_drive_ = 1.0 / 1.5;
};
