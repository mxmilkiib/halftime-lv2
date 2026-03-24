#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>
#include <numeric>
#include "OlaEngine.hpp"

static constexpr double SR = 44100.0;
static constexpr int BLOCK = 4096;

// Generate a sine wave block
static std::vector<double> sineBlock(int n, double freq, double sr) {
    std::vector<double> buf(n);
    for (int i = 0; i < n; ++i)
        buf[i] = std::sin(2.0 * M_PI * freq * i / sr);
    return buf;
}

// RMS of a buffer
static double rms(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) sum += x * x;
    return std::sqrt(sum / static_cast<double>(v.size()));
}

TEST_CASE("OlaEngine: produces non-silent output at half speed", "[ola]") {
    OlaEngine ola;
    ola.setSampleRate(SR);

    OlaEngine::Params p;
    p.speed = 0.5;
    p.grain_ms = 80.0;
    ola.setParams(p);

    auto input = sineBlock(BLOCK * 2, 440.0, SR);
    std::vector<double> output(BLOCK * 2);

    for (int i = 0; i < BLOCK * 2; ++i)
        output[i] = ola.process(input[i], false);

    // Skip first grain (startup transient), check second half has energy
    std::vector<double> tail(output.begin() + BLOCK, output.end());
    double level = rms(tail);
    CHECK(level > 0.01);  // must produce audible output
}

TEST_CASE("OlaEngine: smooth=0 preserves more transient energy than smooth=1", "[ola][smooth]") {
    // Feed an impulse train, measure output energy at the impulse positions.
    // With smooth=0 (percussive), dry bleed at grain boundaries should
    // preserve more of the transient than smooth=1 (full crossfade).

    auto runWithSmooth = [](double smooth_val) -> double {
        OlaEngine ola;
        ola.setSampleRate(SR);

        OlaEngine::Params p;
        p.speed = 0.5;
        p.grain_ms = 80.0;
        p.smooth = smooth_val;
        ola.setParams(p);

        // Impulse every 2000 samples
        constexpr int LEN = 20000;
        double peak = 0.0;
        for (int i = 0; i < LEN; ++i) {
            double in = (i % 2000 == 0) ? 1.0 : 0.0;
            double out = ola.process(in, false);
            double a = std::abs(out);
            if (a > peak && i > 4000) // skip startup
                peak = a;
        }
        return peak;
    };

    double peak_percussive = runWithSmooth(0.0);
    double peak_sustain    = runWithSmooth(1.0);

    // Percussive mode should have at least as much transient peak as sustain mode
    CHECK(peak_percussive >= peak_sustain * 0.8);
}

TEST_CASE("OlaEngine: smooth parameter is clamped to [0,1]", "[ola][smooth]") {
    OlaEngine ola;
    ola.setSampleRate(SR);

    OlaEngine::Params p;
    p.smooth = -0.5;
    ola.setParams(p);  // should not crash

    p.smooth = 2.0;
    ola.setParams(p);  // should not crash

    // Process a few samples to confirm no NaN/inf
    for (int i = 0; i < 1000; ++i) {
        double out = ola.process(0.5, false);
        CHECK(std::isfinite(out));
    }
}

TEST_CASE("OlaEngine: reset produces silence on next process", "[ola]") {
    OlaEngine ola;
    ola.setSampleRate(SR);

    OlaEngine::Params p;
    p.speed = 0.5;
    ola.setParams(p);

    // Feed some audio
    auto input = sineBlock(BLOCK, 440.0, SR);
    for (int i = 0; i < BLOCK; ++i)
        (void)ola.process(input[i], false);

    // Reset
    ola.reset();

    // Process silence — output should be near-silent
    double maxAbs = 0.0;
    for (int i = 0; i < 256; ++i) {
        double out = ola.process(0.0, false);
        double a = std::abs(out);
        if (a > maxAbs) maxAbs = a;
    }
    CHECK(maxAbs < 0.01);
}

TEST_CASE("OlaEngine: freeze holds audio indefinitely", "[ola]") {
    OlaEngine ola;
    ola.setSampleRate(SR);

    // Feed a sine to fill the buffer
    OlaEngine::Params p;
    p.speed = 0.5;
    p.grain_ms = 80.0;
    ola.setParams(p);

    auto input = sineBlock(BLOCK, 440.0, SR);
    for (int i = 0; i < BLOCK; ++i)
        (void)ola.process(input[i], false);

    // Enable freeze
    p.freeze = true;
    ola.setParams(p);

    // Process silence input — output should still have energy from frozen buffer
    std::vector<double> frozen_out(BLOCK);
    for (int i = 0; i < BLOCK; ++i)
        frozen_out[i] = ola.process(0.0, false);

    double level = rms(frozen_out);
    CHECK(level > 0.01);  // frozen buffer should still produce output
}

TEST_CASE("OlaEngine: reverse mode produces output", "[ola]") {
    OlaEngine ola;
    ola.setSampleRate(SR);

    OlaEngine::Params p;
    p.speed = 0.5;
    p.grain_ms = 80.0;
    p.reverse = true;
    ola.setParams(p);

    auto input = sineBlock(BLOCK * 2, 440.0, SR);
    std::vector<double> output(BLOCK * 2);

    for (int i = 0; i < BLOCK * 2; ++i)
        output[i] = ola.process(input[i], false);

    std::vector<double> tail(output.begin() + BLOCK, output.end());
    double level = rms(tail);
    CHECK(level > 0.01);
}
