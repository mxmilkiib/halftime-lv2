#pragma once
#include "SpectralFrame.hpp"

// ISpectralProcessor — interface for spectral-domain effects.
// Implementations are chained inside CompositeSpectralProcessor and
// called per-frame by the PhaseVocoder between analysis and synthesis.

class ISpectralProcessor {
public:
    virtual ~ISpectralProcessor() = default;
    virtual void setSampleRate(double sr) noexcept { (void)sr; }
    virtual void reset() noexcept {}
    virtual void process(SpectralFrame& frame) noexcept = 0;
};
