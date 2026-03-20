#pragma once
#include <cmath>
#include <cstddef>
#include <algorithm>

// WindowShapes — stateless window function collection.
// All functions take phase in [0.0, 1.0] and return gain in [0.0, 1.0].
// Used by OlaEngine at grain read time — zero storage cost.

enum class WindowShape : int {
    Hann       = 0,  // General purpose. Good spectral leakage, smooth output.
    Blackman   = 1,  // Better leakage than Hann. Best for sustained tonal material.
    RectTaper  = 2,  // Rectangular centre with short tapered edges.
                     // Sharpest transient reproduction. More leakage on tones.
};

namespace window {

[[nodiscard]] inline double hann(double ph) noexcept {
    return 0.5 - 0.5 * std::cos(2.0 * M_PI * ph);
}

[[nodiscard]] inline double blackman(double ph) noexcept {
    // Blackman window: better stopband attenuation than Hann at modest extra cost
    return 0.42
         - 0.50 * std::cos(2.0 * M_PI * ph)
         + 0.08 * std::cos(4.0 * M_PI * ph);
}

[[nodiscard]] inline double rectTaper(double ph, double taper = 0.08) noexcept {
    // Flat top with raised-cosine fade-in/out over `taper` fraction of the grain.
    // taper=0.08 means 8% of grain length at each end — about 6ms at 80ms grain.
    if (ph < taper)
        return 0.5 - 0.5 * std::cos(M_PI * ph / taper);
    if (ph > 1.0 - taper)
        return 0.5 - 0.5 * std::cos(M_PI * (1.0 - ph) / taper);
    return 1.0;
}

[[nodiscard]] inline double evaluate(WindowShape shape, double ph) noexcept {
    switch (shape) {
        case WindowShape::Hann:      return hann(ph);
        case WindowShape::Blackman:  return blackman(ph);
        case WindowShape::RectTaper: return rectTaper(ph);
        default:                     return hann(ph);
    }
}

// Normalisation coefficient — compensates for different window energy levels
// so output amplitude stays consistent when switching shapes.
// Computed analytically; valid for 50% overlap (2-grain OLA).
[[nodiscard]] inline double normCoeff(WindowShape shape) noexcept {
    switch (shape) {
        case WindowShape::Hann:      return 1.0;      // Hann at 50% overlap sums to 1
        case WindowShape::Blackman:  return 1.0/0.84;  // slightly lower energy
        case WindowShape::RectTaper: return 1.0/2.0;   // flat-top sums to 2.0 at 50% overlap
        default:                     return 1.0;
    }
}

} // namespace window
