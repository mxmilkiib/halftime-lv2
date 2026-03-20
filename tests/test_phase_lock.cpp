#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include "PhaseLockProcessor.hpp"
#include "PhaseVocoder.hpp"
#include "CompositeSpectralProcessor.hpp"

TEST_CASE("PhaseLockProcessor: installs via composite without crash", "[phaselock]") {
    PhaseVocoder pv;
    pv.setSampleRate(44100.0);
    pv.setSemitones(-5.0);

    auto comp = std::make_unique<CompositeSpectralProcessor>();
    comp->add(std::make_unique<PhaseLockProcessor>());
    REQUIRE_NOTHROW(
        pv.setSpectralProcessor(std::move(comp)));

    for (int i = 0; i < 8192; ++i)
        (void)pv.process(std::sin(2.0 * M_PI * 440.0 / 44100.0 * i));
}

TEST_CASE("PhaseLockProcessor: disabled is exact no-op on frame", "[phaselock]") {
    PhaseLockProcessor pl;
    pl.setSampleRate(44100.0);
    pl.setEnabled(false);

    std::vector<double> mag(1025, 1.0), ph(1025, 0.5), tf(1025, 0.01);
    const auto mag_orig = mag;
    const auto tf_orig  = tf;
    SpectralFrame frame{mag.data(), ph.data(), tf.data(), 1024, 0.7, 44100.0};
    pl.process(frame);

    for (std::size_t k = 0; k < 1025; ++k) {
        CHECK(mag[k] == Catch::Approx(mag_orig[k]).epsilon(1e-12));
        CHECK(tf[k]  == Catch::Approx(tf_orig[k]).epsilon(1e-12));
    }
}

TEST_CASE("PhaseLockProcessor: modifies true_freq near peaks", "[phaselock]") {
    PhaseLockProcessor pl;
    pl.setSampleRate(44100.0);
    pl.setEnabled(true);
    pl.setNeighbourhood(4);

    // Single peak at bin 100
    std::vector<double> mag(1025, 0.01);
    std::vector<double> ph(1025, 0.0);
    std::vector<double> tf(1025, 0.01);
    mag[100] = 1.0;
    tf[100]  = 0.5;

    const auto tf_orig = tf;
    SpectralFrame frame{mag.data(), ph.data(), tf.data(), 1024, 0.7, 44100.0};
    pl.process(frame);

    // Peak itself should be unchanged
    CHECK(tf[100] == Catch::Approx(tf_orig[100]).epsilon(1e-9));

    // At least one neighbour should have been modified
    bool any_modified = false;
    for (int k = 96; k <= 104; ++k)
        if (k != 100 && std::abs(tf[k] - tf_orig[k]) > 1e-6)
            any_modified = true;
    CHECK(any_modified);
}
