#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include "CompositeSpectralProcessor.hpp"

struct ScaleProcessor : public ISpectralProcessor {
    double scale;
    explicit ScaleProcessor(double s) : scale(s) {}
    void process(SpectralFrame& f) noexcept override {
        for (std::size_t k = 0; k <= f.half_size; ++k)
            f.mag[k] *= scale;
    }
};

TEST_CASE("CompositeSpectralProcessor: chains processors in order", "[composite]") {
    CompositeSpectralProcessor comp;
    comp.add(std::make_unique<ScaleProcessor>(2.0));
    comp.add(std::make_unique<ScaleProcessor>(3.0));

    std::vector<double> mag(1025, 1.0), ph(1025), tf(1025);
    SpectralFrame frame{mag.data(), ph.data(), tf.data(), 1024, 1.0, 44100.0};
    comp.process(frame);

    // Should be 1.0 * 2.0 * 3.0 = 6.0
    for (std::size_t k = 0; k < 1025; ++k)
        CHECK(mag[k] == Catch::Approx(6.0).epsilon(1e-9));
}

TEST_CASE("CompositeSpectralProcessor: get() returns correct processor", "[composite]") {
    CompositeSpectralProcessor comp;
    auto* s1 = new ScaleProcessor(2.0);
    auto* s2 = new ScaleProcessor(3.0);
    comp.add(std::unique_ptr<ISpectralProcessor>(s1));
    comp.add(std::unique_ptr<ISpectralProcessor>(s2));

    CHECK(comp.get(0) == s1);
    CHECK(comp.get(1) == s2);
    CHECK(comp.get(2) == nullptr);
}

TEST_CASE("CompositeSpectralProcessor: reset propagates to all", "[composite]") {
    struct CountingReset : public ISpectralProcessor {
        int resets = 0;
        void reset() noexcept override { ++resets; }
        void process(SpectralFrame&) noexcept override {}
    };

    CompositeSpectralProcessor comp;
    auto* a = new CountingReset;
    auto* b = new CountingReset;
    comp.add(std::unique_ptr<ISpectralProcessor>(a));
    comp.add(std::unique_ptr<ISpectralProcessor>(b));

    comp.reset();
    CHECK(a->resets == 1);
    CHECK(b->resets == 1);
}
