#pragma once
#include "OlaEngine.hpp"
#include "PhaseVocoder.hpp"
#include "CompositeSpectralProcessor.hpp"
#include "FormantShifter.hpp"
#include "SpectralFreeze.hpp"
#include "PhaseLockProcessor.hpp"
#include "TransientLookaheadScheduler.hpp"
#include "SubBassEnhancer.hpp"
#include "StutterGrid.hpp"
#include "DcBlocker.hpp"
#include "OutputLimiter.hpp"
#ifdef HALFTIME_HAS_LV2
#include "BpmSync.hpp"
#endif
#include "ParamSmoother.hpp"
#include "ParamQueue.hpp"
#include "MorphController.hpp"
#include "WindowShapes.hpp"
#include "TransientDetector.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

// Simple ring-buffer delay line for dry signal latency compensation.
// Delays the dry path by the same amount as the wet path (OLA + PhaseVocoder)
// so dry/wet mixing doesn't produce comb filtering.
class DryDelay {
public:
    static constexpr std::size_t MAX_DELAY = 131072; // covers 500ms@192k + FFT

    void setDelay(std::size_t samples) noexcept {
        delay_ = std::min(samples, MAX_DELAY - 1);
    }

    [[nodiscard]] double push(double input) noexcept {
        const std::size_t read_pos = (write_ + MAX_DELAY - delay_) & MASK;
        const double out = buf_[read_pos];
        buf_[write_] = input;
        write_ = (write_ + 1) & MASK;
        return out;
    }

    void reset() noexcept {
        std::memset(buf_, 0, sizeof(buf_));
        write_ = 0;
    }

private:
    static constexpr std::size_t MASK = MAX_DELAY - 1;
    double buf_[MAX_DELAY] = {};
    std::size_t write_ = 0;
    std::size_t delay_ = 0;
};

// HalftimePlugin — assembles the full DSP chain.
// Completely independent of LV2 except for BpmSync (which needs LV2 atoms).
//
// Chain per channel:
//   input -> DC block -> lookahead delay -> OLA (WSOLA, transient-locked)
//         -> PhaseVocoder (phase-lock + formant + freeze)
//         -> SubBassEnhancer -> StutterGrid -> OutputLimiter -> dry/wet

class HalftimePlugin {
public:
    struct ControlPorts {
        float speed            = 50.f;
        float grain_ms         = 80.f;
        float wet              = 1.f;
        float pitch_semi       = 0.f;
        float stutter_div      = 1.f;
        float freeze           = 0.f;
        float transient_lock   = 0.f;
        float sensitivity      = 1.f;
        float window_shape     = 0.f;
        float bpm_division     = 0.f;
        float formant_strength = 1.f;
        float spectral_freeze  = 0.f;
        float spectral_latch   = 0.f;
        float sub_gain         = 0.f;
        float sub_mode         = 0.f;
        float morph_in         = 0.f;
        float morph_out        = 0.f;
        float morph_beats      = 2.f;
        float phase_lock       = 1.f;
        float lookahead_enable = 1.f;
    };


    // MARK: CONSTRUCTION

    HalftimePlugin() { buildSpectralChain(); }


    // MARK: LIFECYCLE

    void setSampleRate(double sr) {
        sr_ = sr;
        for (auto* e : {&ola_l_, &ola_r_})        e->setSampleRate(sr);
        for (auto* p : {&pv_l_,  &pv_r_})         p->setSampleRate(sr);
        for (auto* s : {&stutter_l_, &stutter_r_}) s->setSampleRate(sr);
        for (auto* d : {&dc_l_, &dc_r_})           d->setSampleRate(sr);
        for (auto* b : {&sub_l_, &sub_r_})         b->setSampleRate(sr);
        for (auto* l : {&lookahead_l_, &lookahead_r_}) l->setSampleRate(sr);
        transient_.setSampleRate(sr);
#ifdef HALFTIME_HAS_LV2
        bpm_.setSampleRate(sr);
#endif
        morph_.setSampleRate(sr);
        wet_smooth_.setSampleRate(sr, 20.0);
        pitch_smooth_.setSampleRate(sr, 50.0);
        speed_smooth_.setSampleRate(sr, 30.0);
        formant_smooth_.setSampleRate(sr, 30.0);
        last_latency_ = computeLatency();
        updateDryDelay();
    }

#ifdef HALFTIME_HAS_LV2
    void initUrids(LV2_URID_Map* map) { bpm_.init(map); }
#endif

    // Full DSP state reset — called from LV2 activate()
    void reset() noexcept {
        for (auto* e : {&ola_l_, &ola_r_})        e->reset();
        for (auto* p : {&pv_l_,  &pv_r_})         p->reset();
        for (auto* s : {&stutter_l_, &stutter_r_}) s->reset();
        for (auto* d : {&dc_l_, &dc_r_})           d->reset();
        for (auto* b : {&sub_l_, &sub_r_})         b->reset();
        for (auto* l : {&lookahead_l_, &lookahead_r_}) l->reset();
        dry_delay_l_.reset();
        dry_delay_r_.reset();
        transient_.reset();
        prev_spectral_latch_ = 0.f;
        prev_morph_in_       = 0.f;
        prev_morph_out_      = 0.f;
    }


    // MARK: THREAD-SAFE CONTROL PUSH

    // Called from GUI / control thread
    void pushControls(const ControlPorts& c) noexcept {
        param_queue_.push(c);
    }


    // MARK: BPM ATOM PROCESSING

#ifdef HALFTIME_HAS_LV2
    // Audio thread, before processBlock
    bool processBpmAtoms(const LV2_Atom_Sequence* seq) {
        const bool changed = bpm_.readPosition(seq);
        if (changed && bpm_.active()) {
            const std::size_t gs = bpm_.grainSamples(bpm_.division());
            for (auto* e : {&ola_l_, &ola_r_}) e->setGrainSamplesDirect(gs);
            morph_.setBpm(bpm_.bpm());
            const uint32_t nl = computeLatency();
            if (nl != last_latency_) { last_latency_ = nl; return true; }
        }
        return false;
    }
#endif

    // Direct BPM setter for non-LV2 hosts (e.g. Mixxx).
    // Returns true if latency changed.
    bool setBpm(double bpm) noexcept {
        if (bpm <= 0.0 || std::abs(bpm - bpm_cache_) < 0.01) return false;
        bpm_cache_ = bpm;
        morph_.setBpm(bpm);
        if (bpm_division_ > 0) {
            const double beat_samps = sr_ * 60.0 / bpm;
            const double grain = beat_samps / static_cast<double>(bpm_division_);
            const auto gs = static_cast<std::size_t>(
                std::clamp(grain, 64.0, static_cast<double>(OlaEngine::MAX_BUF / 4)));
            for (auto* e : {&ola_l_, &ola_r_}) e->setGrainSamplesDirect(gs);
        }
        const uint32_t nl = computeLatency();
        if (nl != last_latency_) { last_latency_ = nl; return true; }
        return false;
    }

    void setBpmDivision(int div) noexcept {
        bpm_division_ = std::clamp(div, 0, 8);
    }


    // MARK: AUDIO THREAD CONTROL APPLICATION

    bool setControls(const ControlPorts& c) {
        handleEdgeTriggers(c);
        last_ctrl_ = c;

        // Window shape
        const auto shape = static_cast<WindowShape>(
            std::clamp(static_cast<int>(std::round(c.window_shape)), 0, 2));
        for (auto* e : {&ola_l_, &ola_r_}) e->setWindowShape(shape);

        // BPM division
        bpm_division_ = static_cast<int>(std::round(c.bpm_division));
#ifdef HALFTIME_HAS_LV2
        bpm_.setDivision(bpm_division_);
#endif

        // OLA params
        OlaEngine::Params p;
        p.freeze     = c.freeze > 0.5f;
        p.trans_lock = c.transient_lock > 0.5f;

        const bool bpm_active = bpm_division_ > 0 && bpm_cache_ > 0.0;
        if (bpm_active) {
            const double beat_samps = sr_ * 60.0 / bpm_cache_;
            const double grain = beat_samps / static_cast<double>(bpm_division_);
            const auto gs = static_cast<std::size_t>(
                std::clamp(grain, 64.0, static_cast<double>(OlaEngine::MAX_BUF / 4)));
            for (auto* e : {&ola_l_, &ola_r_}) {
                e->setParams(p);
                e->setGrainSamplesDirect(gs);
            }
        } else {
            p.grain_ms = static_cast<double>(std::clamp(c.grain_ms, 20.f, 500.f));
            for (auto* e : {&ola_l_, &ola_r_}) e->setParams(p);
        }

        // Smoothed targets
        speed_smooth_.setTarget(
            static_cast<double>(std::clamp(c.speed, 25.f, 100.f)) / 100.0);
        pitch_smooth_.setTarget(static_cast<double>(c.pitch_semi));
        wet_smooth_.setTarget(
            static_cast<double>(std::clamp(c.wet, 0.f, 1.f)));
        formant_smooth_.setTarget(
            static_cast<double>(std::clamp(c.formant_strength, 0.f, 1.f)));

        morph_.setTargetSpeed(
            static_cast<double>(std::clamp(c.speed, 25.f, 100.f)) / 100.0);
        morph_.setBpm(bpm_cache_ > 0.0 ? bpm_cache_ : 120.0);

        // Stutter
        for (auto* s : {&stutter_l_, &stutter_r_})
            s->setDiv(static_cast<int>(std::round(c.stutter_div)));

        // Transient sensitivity
        transient_.setSensitivity(static_cast<double>(c.sensitivity));

        // Sub-bass
        const double sub_gain = static_cast<double>(
            std::clamp(c.sub_gain, 0.f, 1.f));
        const auto sub_mode = c.sub_mode > 0.5f
            ? SubBassEnhancer::Mode::HalfWave
            : SubBassEnhancer::Mode::RingMod;
        for (auto* b : {&sub_l_, &sub_r_}) {
            b->setGain(sub_gain);
            b->setMode(sub_mode);
        }

        // Spectral freeze blend
        if (freeze_l_) freeze_l_->setBlend(
            static_cast<double>(std::clamp(c.spectral_freeze, 0.f, 1.f)));
        if (freeze_r_) freeze_r_->setBlend(
            static_cast<double>(std::clamp(c.spectral_freeze, 0.f, 1.f)));

        // Phase lock enable/disable
        if (plock_l_) plock_l_->setEnabled(c.phase_lock > 0.5f);
        if (plock_r_) plock_r_->setEnabled(c.phase_lock > 0.5f);

        // Lookahead
        lookahead_enabled_ = c.lookahead_enable > 0.5f;

        const uint32_t nl = computeLatency();
        if (nl != last_latency_) {
            last_latency_ = nl;
            updateDryDelay();
            return true;
        }
        return false;
    }


    // MARK: MAIN AUDIO PROCESSING

    void processBlock(
        const float* in_l,  const float* in_r,
        float*       out_l, float*       out_r,
        uint32_t     n
    ) noexcept {
        // Drain lock-free queue at block boundary
        if (auto latest = param_queue_.popLatest())
            setControls(*latest);

        const std::size_t gs = ola_l_.grainSamples();

        for (uint32_t i = 0; i < n; ++i) {
            // DC blocking on raw input
            const double xl_raw = dc_l_.process(static_cast<double>(in_l[i]));
            const double xr_raw = dc_r_.process(static_cast<double>(in_r[i]));

            // Per-sample smoothed controls
            const double wet   = wet_smooth_.next();
            const double semi  = pitch_smooth_.next();
            const double fstr  = formant_smooth_.next();
            const double speed = morph_.isActive()
                ? morph_.tick()
                : speed_smooth_.next();

            // Transient detection on undelayed mono sum
            const bool onset_raw = transient_.process((xl_raw + xr_raw) * 0.5);

            // Lookahead scheduling: delay audio, align onset to audio position
            double xl, xr;
            bool onset;
            if (lookahead_enabled_) {
                xl    = lookahead_l_.push(xl_raw, onset_raw);
                xr    = lookahead_r_.push(xr_raw, onset_raw);
                onset = lookahead_l_.onsetNow();
            } else {
                xl = xl_raw; xr = xr_raw; onset = onset_raw;
            }

            // Update per-sample controls
            ola_l_.setSpeedDirect(speed);
            ola_r_.setSpeedDirect(speed);
            if (formant_l_) formant_l_->setStrength(fstr);
            if (formant_r_) formant_r_->setStrength(fstr);
            pv_l_.setSemitones(semi);
            pv_r_.setSemitones(semi);

            // DSP chain: OLA -> vocoder -> sub -> stutter -> limit
            double wl = pv_l_.process(ola_l_.process(xl, onset));
            double wr = pv_r_.process(ola_r_.process(xr, onset));

            wl = sub_l_.process(wl);
            wr = sub_r_.process(wr);

            wl = stutter_l_.process(wl, gs);
            wr = stutter_r_.process(wr, gs);

            limiter_.process(wl, wr);

            // Delay dry signal to match wet path latency (OLA + PV)
            const double dry_l = dry_delay_l_.push(xl);
            const double dry_r = dry_delay_r_.push(xr);

            out_l[i] = static_cast<float>(wl * wet + dry_l * (1.0 - wet));
            out_r[i] = static_cast<float>(wr * wet + dry_r * (1.0 - wet));
        }
    }

    [[nodiscard]] uint32_t latencySamples() const noexcept { return last_latency_; }

    const ControlPorts& lastControls() const noexcept { return last_ctrl_; }

private:

    // MARK: -- dsp components

    OlaEngine                    ola_l_, ola_r_;
    PhaseVocoder                 pv_l_,  pv_r_;
    StutterGrid                  stutter_l_, stutter_r_;
    DcBlocker                    dc_l_,  dc_r_;
    SubBassEnhancer              sub_l_, sub_r_;
    OutputLimiter                limiter_;
    DryDelay                     dry_delay_l_, dry_delay_r_;
    TransientDetector            transient_;
    TransientLookaheadScheduler  lookahead_l_, lookahead_r_;
#ifdef HALFTIME_HAS_LV2
    BpmSync                      bpm_;
#endif
    double                       bpm_cache_     = 0.0;
    int                          bpm_division_  = 0;
    MorphController              morph_;
    ParamSmoother                wet_smooth_, pitch_smooth_,
                                 speed_smooth_, formant_smooth_;
    ParamQueue<ControlPorts, 16> param_queue_;
    ControlPorts                 last_ctrl_;
    double                       sr_            = 44100.0;
    uint32_t                     last_latency_  = 0;
    bool                         lookahead_enabled_ = true;


    // MARK: -- spectral processor chain

    // Non-owning pointers for runtime parameter access.
    // Lifetime: owned by vocoder via the composite.
    PhaseLockProcessor*  plock_l_    = nullptr;
    PhaseLockProcessor*  plock_r_    = nullptr;
    FormantShifter*      formant_l_  = nullptr;
    FormantShifter*      formant_r_  = nullptr;
    SpectralFreeze*      freeze_l_   = nullptr;
    SpectralFreeze*      freeze_r_   = nullptr;

    // Edge-trigger state
    float prev_spectral_latch_ = 0.f;
    float prev_morph_in_       = 0.f;
    float prev_morph_out_      = 0.f;


    // MARK: -- spectral chain build

    // Chain order: PhaseLock -> Formant -> Freeze
    void buildSpectralChain() {
        for (int ch = 0; ch < 2; ++ch) {
            auto comp = std::make_unique<CompositeSpectralProcessor>();

            auto plock = std::make_unique<PhaseLockProcessor>();
            auto* plock_ptr = plock.get();
            comp->add(std::move(plock));

            auto formant = std::make_unique<FormantShifter>();
            auto* formant_ptr = formant.get();
            comp->add(std::move(formant));

            auto freeze = std::make_unique<SpectralFreeze>();
            auto* freeze_ptr = freeze.get();
            comp->add(std::move(freeze));

            if (ch == 0) {
                plock_l_   = plock_ptr;
                formant_l_ = formant_ptr;
                freeze_l_  = freeze_ptr;
                pv_l_.setSpectralProcessor(std::move(comp));
            } else {
                plock_r_   = plock_ptr;
                formant_r_ = formant_ptr;
                freeze_r_  = freeze_ptr;
                pv_r_.setSpectralProcessor(std::move(comp));
            }
        }
    }

    uint32_t computeLatency() const noexcept {
        const uint32_t lookahead = lookahead_enabled_
            ? lookahead_l_.latencySamples() : 0u;
        return ola_l_.latencySamples()
             + PhaseVocoder::latencySamples()
             + lookahead;
    }

    // Wet path internal delay = OLA + PhaseVocoder (not lookahead — both paths share it)
    void updateDryDelay() noexcept {
        const std::size_t wet_delay =
            ola_l_.latencySamples() + PhaseVocoder::latencySamples();
        dry_delay_l_.setDelay(wet_delay);
        dry_delay_r_.setDelay(wet_delay);
    }

    void handleEdgeTriggers(const ControlPorts& c) noexcept {
        if (c.spectral_latch > 0.5f && prev_spectral_latch_ <= 0.5f) {
            if (freeze_l_) freeze_l_->latch();
            if (freeze_r_) freeze_r_->latch();
        }
        prev_spectral_latch_ = c.spectral_latch;

        if (c.morph_in > 0.5f && prev_morph_in_ <= 0.5f)
            morph_.morphIn(static_cast<double>(c.morph_beats));
        prev_morph_in_ = c.morph_in;

        if (c.morph_out > 0.5f && prev_morph_out_ <= 0.5f)
            morph_.morphOut(static_cast<double>(c.morph_beats));
        prev_morph_out_ = c.morph_out;
    }
};
