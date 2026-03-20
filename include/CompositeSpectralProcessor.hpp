#pragma once
#include "ISpectralProcessor.hpp"
#include <vector>
#include <memory>

// CompositeSpectralProcessor — chains multiple ISpectralProcessor
// implementations into a single processor called sequentially per frame.
//
// Ownership: the composite owns its processors via unique_ptr.
// The order of addition is the order of processing:
//   add(FormantShifter) then add(SpectralFreeze) means formant
//   correction runs first, then freeze blends on top.

class CompositeSpectralProcessor final : public ISpectralProcessor {
public:
    void add(std::unique_ptr<ISpectralProcessor> proc) {
        if (proc) chain_.push_back(std::move(proc));
    }

    ISpectralProcessor* get(std::size_t index) noexcept {
        return index < chain_.size() ? chain_[index].get() : nullptr;
    }

    std::size_t size() const noexcept { return chain_.size(); }

    void setSampleRate(double sr) noexcept override {
        for (auto& p : chain_) p->setSampleRate(sr);
    }

    void reset() noexcept override {
        for (auto& p : chain_) p->reset();
    }

    void process(SpectralFrame& frame) noexcept override {
        for (auto& p : chain_) p->process(frame);
    }

private:
    std::vector<std::unique_ptr<ISpectralProcessor>> chain_;
};
