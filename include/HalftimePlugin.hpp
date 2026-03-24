#pragma once
#include "OlaEngine.hpp"
#include "PhaseVocoder.hpp"
#include "CompositeSpectralProcessor.hpp"
#include "FormantShifter.hpp"
#include "SpectralFreeze.hpp"
#include "SpectralTilt.hpp"
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
#include "ThreeBandEQ.hpp"
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
// Per-sample signal chain (each channel):
//   input -> input_gain -> EQ -> DC block -> lookahead delay
//         -> OLA (WSOLA, transient-locked)
//         -> PhaseVocoder (phase-lock + formant + freeze)
//         -> SubBassEnhancer -> StutterGrid -> OutputLimiter
//         -> stereo width -> dry/wet blend -> output EQ -> output_gain
//
// Control flow:
//   setControls() applies all parameter changes (called once per block).
//   processBlock() runs the per-sample DSP loop.
//   setBpm() / processBpmAtoms() handle tempo from host or manual knob.

class HalftimePlugin {
public:
    // All host-facing parameters. Populated by the DPF or LV2 wrapper
    // each audio block, then passed to setControls().
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
        float phase_lock       = 0.f;
        float lookahead_enable = 1.f;
        float reverse          = 0.f;
        float grain_random     = 0.f;
        float stereo_width     = 1.f;
        float spectral_tilt    = 0.f;
        float phase_random     = 0.f;
        float input_gain       = 0.f;
        float output_gain      = 0.f;
        float eq_low           = 0.f;
        float eq_mid           = 0.f;
        float eq_high          = 0.f;
        float eq_out_low       = 0.f;
        float eq_out_mid       = 0.f;
        float eq_out_high      = 0.f;
        float manual_bpm       = 0.f;
        float sub_freq         = 80.f;
        float sub_drive        = 0.f;
        float smooth           = 0.5f;
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
        for (auto* q : {&eq_l_, &eq_r_})           q->setSampleRate(sr);
        for (auto* q : {&eq_out_l_, &eq_out_r_})   q->setSampleRate(sr);
        transient_.setSampleRate(sr);
#ifdef HALFTIME_HAS_LV2
        bpm_.setSampleRate(sr);
#endif
        morph_.setSampleRate(sr);
        wet_smooth_.setSampleRate(sr, 20.0);
        pitch_smooth_.setSampleRate(sr, 50.0);
        speed_smooth_.setSampleRate(sr, 30.0);
        formant_smooth_.setSampleRate(sr, 30.0);
        in_gain_smooth_.setSampleRate(sr, 20.0);
        out_gain_smooth_.setSampleRate(sr, 20.0);
        last_latency_ = computeLatency();
        updateDryDelay();
    }

#ifdef HALFTIME_HAS_LV2
    void initUrids(LV2_URID_Map* map) { bpm_.init(map); }
#endif

    // Full DSP state reset — called from LV2 activate() / DPF activate()
    void reset() noexcept {
        for (auto* e : {&ola_l_, &ola_r_})        e->reset();
        for (auto* p : {&pv_l_,  &pv_r_})         p->reset();
        for (auto* s : {&stutter_l_, &stutter_r_}) s->reset();
        for (auto* d : {&dc_l_, &dc_r_})           d->reset();
        for (auto* b : {&sub_l_, &sub_r_})         b->reset();
        for (auto* l : {&lookahead_l_, &lookahead_r_}) l->reset();
        for (auto* q : {&eq_l_, &eq_r_})           q->reset();
        for (auto* q : {&eq_out_l_, &eq_out_r_})   q->reset();
        dry_delay_l_.reset();
        dry_delay_r_.reset();
        transient_.reset();
        prev_spectral_latch_ = 0.f;
        prev_morph_in_       = 0.f;
        prev_morph_out_      = 0.f;
    }



    // MARK: THREAD-SAFE CONTROL PUSH

    // Called from the GUI / control thread — lock-free delivery to audio thread
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

    // Direct BPM setter for DPF hosts and LV2 manual BPM fallback.
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

    // Apply all control port values to DSP components. Called once per block.
    // Returns true if latency changed (host must re-query).
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
        p.reverse    = c.reverse > 0.5f;
        p.random     = static_cast<double>(std::clamp(c.grain_random, 0.f, 1.f));
        p.smooth     = static_cast<double>(std::clamp(c.smooth, 0.f, 1.f));

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
            b->setFreq(static_cast<double>(std::clamp(c.sub_freq, 30.f, 200.f)));
            b->setDrive(static_cast<double>(std::clamp(c.sub_drive, 0.f, 1.f)));
        }

        // Spectral freeze blend + phase randomization
        const double sf_blend = static_cast<double>(std::clamp(c.spectral_freeze, 0.f, 1.f));
        const double sf_phrand = static_cast<double>(std::clamp(c.phase_random, 0.f, 1.f));
        if (freeze_l_) { freeze_l_->setBlend(sf_blend); freeze_l_->setPhaseRandom(sf_phrand); }
        if (freeze_r_) { freeze_r_->setBlend(sf_blend); freeze_r_->setPhaseRandom(sf_phrand); }

        // Spectral tilt
        const double tilt_val = static_cast<double>(std::clamp(c.spectral_tilt, -1.f, 1.f));
        if (tilt_l_) tilt_l_->setTilt(tilt_val);
        if (tilt_r_) tilt_r_->setTilt(tilt_val);

        // Phase lock enable/disable
        if (plock_l_) plock_l_->setEnabled(c.phase_lock > 0.5f);
        if (plock_r_) plock_r_->setEnabled(c.phase_lock > 0.5f);

        // Lookahead
        lookahead_enabled_ = c.lookahead_enable > 0.5f;

        // Stereo width
        stereo_width_ = static_cast<double>(std::clamp(c.stereo_width, 0.f, 2.f));

        // Input / output gain (dB -> linear)
        in_gain_smooth_.setTarget(
            std::pow(10.0, static_cast<double>(std::clamp(c.input_gain, -24.f, 24.f)) / 20.0));
        out_gain_smooth_.setTarget(
            std::pow(10.0, static_cast<double>(std::clamp(c.output_gain, -24.f, 24.f)) / 20.0));

        // Three-band EQ
        eq_l_.setLowGain (static_cast<double>(std::clamp(c.eq_low,  -48.f, 12.f)));
        eq_l_.setMidGain (static_cast<double>(std::clamp(c.eq_mid,  -48.f, 12.f)));
        eq_l_.setHighGain(static_cast<double>(std::clamp(c.eq_high, -48.f, 12.f)));
        eq_r_.setLowGain (static_cast<double>(std::clamp(c.eq_low,  -48.f, 12.f)));
        eq_r_.setMidGain (static_cast<double>(std::clamp(c.eq_mid,  -48.f, 12.f)));
        eq_r_.setHighGain(static_cast<double>(std::clamp(c.eq_high, -48.f, 12.f)));

        // Output three-band EQ
        eq_out_l_.setLowGain (static_cast<double>(std::clamp(c.eq_out_low,  -48.f, 12.f)));
        eq_out_l_.setMidGain (static_cast<double>(std::clamp(c.eq_out_mid,  -48.f, 12.f)));
        eq_out_l_.setHighGain(static_cast<double>(std::clamp(c.eq_out_high, -48.f, 12.f)));
        eq_out_r_.setLowGain (static_cast<double>(std::clamp(c.eq_out_low,  -48.f, 12.f)));
        eq_out_r_.setMidGain (static_cast<double>(std::clamp(c.eq_out_mid,  -48.f, 12.f)));
        eq_out_r_.setHighGain(static_cast<double>(std::clamp(c.eq_out_high, -48.f, 12.f)));

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
            // Input gain + EQ + DC block
            const double in_g = in_gain_smooth_.next();
            const double xl_raw = dc_l_.process(eq_l_.process(static_cast<double>(in_l[i]) * in_g));
            const double xr_raw = dc_r_.process(eq_r_.process(static_cast<double>(in_r[i]) * in_g));

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

            // Stereo width via M/S processing
            // width=1.0: unchanged, width=0.0: mono, width=2.0: exaggerated stereo
            if (std::abs(stereo_width_ - 1.0) > 0.001) {
                const double mid  = (wl + wr) * 0.5;
                const double side = (wl - wr) * 0.5;
                wl = mid + side * stereo_width_;
                wr = mid - side * stereo_width_;
            }

            // Delay dry signal to match wet path latency (OLA + PV)
            const double dry_l = dry_delay_l_.push(xl);
            const double dry_r = dry_delay_r_.push(xr);

            const double out_g = out_gain_smooth_.next();
            double ol = (wl * wet + dry_l * (1.0 - wet)) * out_g;
            double or_ = (wr * wet + dry_r * (1.0 - wet)) * out_g;
            out_l[i] = static_cast<float>(eq_out_l_.process(ol));
            out_r[i] = static_cast<float>(eq_out_r_.process(or_));
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
                                 speed_smooth_, formant_smooth_,
                                 in_gain_smooth_, out_gain_smooth_;
    ThreeBandEQ                  eq_l_, eq_r_;
    ThreeBandEQ                  eq_out_l_, eq_out_r_;
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
    SpectralTilt*        tilt_l_     = nullptr;
    SpectralTilt*        tilt_r_     = nullptr;
    double               stereo_width_ = 1.0;

    // Edge-trigger state
    float prev_spectral_latch_ = 0.f;
    float prev_morph_in_       = 0.f;
    float prev_morph_out_      = 0.f;



    // MARK: -- spectral chain build

    // Chain order: PhaseLock -> Formant -> Tilt -> Freeze
    void buildSpectralChain() {
        for (int ch = 0; ch < 2; ++ch) {
            auto comp = std::make_unique<CompositeSpectralProcessor>();

            auto plock = std::make_unique<PhaseLockProcessor>();
            auto* plock_ptr = plock.get();
            comp->add(std::move(plock));

            auto formant = std::make_unique<FormantShifter>();
            auto* formant_ptr = formant.get();
            comp->add(std::move(formant));

            auto tilt = std::make_unique<SpectralTilt>();
            auto* tilt_ptr = tilt.get();
            comp->add(std::move(tilt));

            auto freeze = std::make_unique<SpectralFreeze>();
            auto* freeze_ptr = freeze.get();
            comp->add(std::move(freeze));

            if (ch == 0) {
                plock_l_   = plock_ptr;
                formant_l_ = formant_ptr;
                tilt_l_    = tilt_ptr;
                freeze_l_  = freeze_ptr;
                pv_l_.setSpectralProcessor(std::move(comp));
            } else {
                plock_r_   = plock_ptr;
                formant_r_ = formant_ptr;
                tilt_r_    = tilt_ptr;
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
