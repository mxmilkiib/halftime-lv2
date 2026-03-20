#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TransientLookaheadScheduler.hpp"
#include <vector>

TEST_CASE("TransientLookaheadScheduler: delays audio by lookahead", "[lookahead]") {
    TransientLookaheadScheduler la;
    la.setSampleRate(44100.0);

    const std::size_t L = la.latencySamples();
    std::vector<double> out;

    // Push a 1.0 pulse followed by silence
    out.push_back(la.push(1.0, false));
    for (std::size_t i = 1; i < 2 * L; ++i)
        out.push_back(la.push(0.0, false));

    // First L outputs should be zero (delay line priming)
    for (std::size_t i = 0; i < L; ++i)
        CHECK(std::abs(out[i]) < 1e-12);
    // Pulse emerges exactly L samples after it was pushed
    CHECK(out[L] == Catch::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("TransientLookaheadScheduler: onset flag emerges at correct time", "[lookahead]") {
    TransientLookaheadScheduler la;
    la.setSampleRate(44100.0);
    const std::size_t L = la.latencySamples();

    // Fire onset on the very first push
    (void)la.push(0.0, true);
    // For the next L-1 pushes, onset should not yet have emerged
    for (std::size_t i = 1; i < L; ++i) {
        (void)la.push(0.0, false);
        CHECK_FALSE(la.onsetNow());
    }

    // The L-th push after the onset: it should now emerge
    (void)la.push(0.0, false);
    CHECK(la.onsetNow());
}

TEST_CASE("TransientLookaheadScheduler: reset clears state", "[lookahead]") {
    TransientLookaheadScheduler la;
    la.setSampleRate(44100.0);
    for (int i = 0; i < 512; ++i) (void)la.push(1.0, true);
    la.reset();
    CHECK(la.push(0.0, false) == Catch::Approx(0.0).epsilon(1e-12));
    CHECK_FALSE(la.onsetNow());
}
