// HalftimeDPF.cpp — DPF Plugin class wrapping HalftimePlugin.
// DSP lives entirely in HalftimePlugin.hpp (no LV2 dependency).

#include "DistrhoPlugin.hpp"
#include "HalftimePlugin.hpp"

START_NAMESPACE_DISTRHO

// Parameter indices — must match the UI
enum ParamId : uint32_t {
    kSpeed = 0,
    kGrainMs,
    kWet,
    kPitchSemi,
    kStutterDiv,
    kFreeze,
    kTransLock,
    kSensitivity,
    kWinShape,
    kBpmDiv,
    kFormant,
    kSpecFreeze,
    kSpecLatch,
    kSubGain,
    kSubMode,
    kMorphIn,
    kMorphOut,
    kMorphBeats,
    kPhaseLock,
    kLookahead,
    kReverse,
    kGrainRandom,
    kStereoWidth,
    kSpectralTilt,
    kPhaseRandom,
    kParamCount
};

// Port groups for host-side organisation
enum PortGroupId : uint32_t {
    kGroupCore = 0,
    kGroupPitch,
    kGroupSpectral,
    kGroupGrain,
    kGroupModulation,
    kGroupOutput,
    kGroupCount
};

class HalftimeDPF : public Plugin {
public:
    HalftimeDPF()
        : Plugin(kParamCount, 0, 0) // params, programs, states
    {
        std::memset(fParams, 0, sizeof(fParams));
        // set defaults
        fParams[kSpeed]        = 50.f;
        fParams[kGrainMs]      = 80.f;
        fParams[kWet]          = 1.f;
        fParams[kPitchSemi]    = 0.f;
        fParams[kStutterDiv]   = 1.f;
        fParams[kFreeze]       = 0.f;
        fParams[kTransLock]    = 0.f;
        fParams[kSensitivity]  = 1.f;
        fParams[kWinShape]     = 0.f;
        fParams[kBpmDiv]       = 0.f;
        fParams[kFormant]      = 1.f;
        fParams[kSpecFreeze]   = 0.f;
        fParams[kSpecLatch]    = 0.f;
        fParams[kSubGain]      = 0.f;
        fParams[kSubMode]      = 0.f;
        fParams[kMorphIn]      = 0.f;
        fParams[kMorphOut]     = 0.f;
        fParams[kMorphBeats]   = 2.f;
        fParams[kPhaseLock]    = 1.f;
        fParams[kLookahead]    = 1.f;
        fParams[kReverse]      = 0.f;
        fParams[kGrainRandom]  = 0.f;
        fParams[kStereoWidth]  = 1.f;
        fParams[kSpectralTilt] = 0.f;
        fParams[kPhaseRandom]  = 0.f;
    }

protected:
    const char* getLabel()       const override { return "halftime"; }
    const char* getDescription() const override { return "WSOLA halftime/slowdown with phase vocoder, spectral freeze, and morph transitions"; }
    const char* getMaker()       const override { return "mxmilkiib"; }
    const char* getHomePage()    const override { return "https://github.com/mxmilkiib/halftime-lv2"; }
    const char* getLicense()     const override { return "MIT"; }
    uint32_t    getVersion()     const override { return d_version(1, 1, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initPortGroup(uint32_t groupId, PortGroup& pg) override {
        switch (groupId) {
        case kGroupCore:       pg.name = "Core";       pg.symbol = "core";       break;
        case kGroupPitch:      pg.name = "Pitch";      pg.symbol = "pitch";      break;
        case kGroupSpectral:   pg.name = "Spectral";   pg.symbol = "spectral";   break;
        case kGroupGrain:      pg.name = "Grain";      pg.symbol = "grain";      break;
        case kGroupModulation: pg.name = "Modulation";  pg.symbol = "modulation"; break;
        case kGroupOutput:     pg.name = "Output";      pg.symbol = "output";     break;
        }
    }

    void initParameter(uint32_t index, Parameter& p) override {
        p.hints = kParameterIsAutomatable;

        switch (index) {
        case kSpeed:
            p.name = "Speed"; p.symbol = "speed"; p.unit = "%";
            p.ranges.min = 25.f; p.ranges.max = 100.f; p.ranges.def = 50.f;
            p.groupId = kGroupCore;
            break;
        case kGrainMs:
            p.name = "Grain Size"; p.symbol = "grain_ms"; p.unit = "ms";
            p.ranges.min = 20.f; p.ranges.max = 500.f; p.ranges.def = 80.f;
            p.groupId = kGroupCore;
            break;
        case kWet:
            p.name = "Wet"; p.symbol = "wet";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 1.f;
            p.groupId = kGroupCore;
            break;
        case kPitchSemi:
            p.name = "Pitch"; p.symbol = "pitch_semi"; p.unit = "st";
            p.ranges.min = -12.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupPitch;
            break;
        case kStutterDiv:
            p.name = "Stutter"; p.symbol = "stutter_div";
            p.hints |= kParameterIsInteger;
            p.ranges.min = 1.f; p.ranges.max = 16.f; p.ranges.def = 1.f;
            p.groupId = kGroupModulation;
            break;
        case kFreeze:
            p.name = "Freeze"; p.symbol = "freeze";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupCore;
            break;
        case kTransLock:
            p.name = "Transient Lock"; p.symbol = "trans_lock";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kSensitivity:
            p.name = "Sensitivity"; p.symbol = "sensitivity";
            p.ranges.min = 0.5f; p.ranges.max = 2.f; p.ranges.def = 1.f;
            p.groupId = kGroupGrain;
            break;
        case kWinShape:
            p.name = "Window"; p.symbol = "win_shape";
            p.hints |= kParameterIsInteger;
            p.ranges.min = 0.f; p.ranges.max = 2.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kBpmDiv:
            p.name = "BPM Div"; p.symbol = "bpm_div";
            p.hints |= kParameterIsInteger;
            p.ranges.min = 0.f; p.ranges.max = 8.f; p.ranges.def = 0.f;
            p.groupId = kGroupModulation;
            break;
        case kFormant:
            p.name = "Formant"; p.symbol = "formant";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 1.f;
            p.groupId = kGroupPitch;
            break;
        case kSpecFreeze:
            p.name = "Spectral Freeze"; p.symbol = "spec_freeze";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        case kSpecLatch:
            p.name = "Spectral Latch"; p.symbol = "spec_latch";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        case kSubGain:
            p.name = "Sub Bass"; p.symbol = "sub_gain";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kSubMode:
            p.name = "Sub Mode"; p.symbol = "sub_mode";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kMorphIn:
            p.name = "Morph In"; p.symbol = "morph_in";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupModulation;
            break;
        case kMorphOut:
            p.name = "Morph Out"; p.symbol = "morph_out";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupModulation;
            break;
        case kMorphBeats:
            p.name = "Morph Beats"; p.symbol = "morph_beats";
            p.ranges.min = 0.5f; p.ranges.max = 8.f; p.ranges.def = 2.f;
            p.groupId = kGroupModulation;
            break;
        case kPhaseLock:
            p.name = "Phase Lock"; p.symbol = "phase_lock";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 1.f;
            p.groupId = kGroupPitch;
            break;
        case kLookahead:
            p.name = "Lookahead"; p.symbol = "lookahead";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 1.f;
            p.groupId = kGroupGrain;
            break;
        case kReverse:
            p.name = "Reverse"; p.symbol = "reverse";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kGrainRandom:
            p.name = "Grain Random"; p.symbol = "grain_random";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kStereoWidth:
            p.name = "Stereo Width"; p.symbol = "stereo_width";
            p.ranges.min = 0.f; p.ranges.max = 2.f; p.ranges.def = 1.f;
            p.groupId = kGroupOutput;
            break;
        case kSpectralTilt:
            p.name = "Spectral Tilt"; p.symbol = "spectral_tilt";
            p.ranges.min = -1.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        case kPhaseRandom:
            p.name = "Phase Random"; p.symbol = "phase_random";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override {
        return (index < kParamCount) ? fParams[index] : 0.f;
    }

    void setParameterValue(uint32_t index, float value) override {
        if (index < kParamCount) fParams[index] = value;
    }

    void sampleRateChanged(double newSampleRate) override {
        fPlugin.setSampleRate(newSampleRate);
    }

    void activate() override {
        fPlugin.setSampleRate(getSampleRate());
        fPlugin.reset();
    }

    void deactivate() override {}

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        // Feed BPM from host time position
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 0.0)
            fPlugin.setBpm(tp.bbt.beatsPerMinute);

        // Build control ports struct
        HalftimePlugin::ControlPorts c;
        c.speed            = fParams[kSpeed];
        c.grain_ms         = fParams[kGrainMs];
        c.wet              = fParams[kWet];
        c.pitch_semi       = fParams[kPitchSemi];
        c.stutter_div      = fParams[kStutterDiv];
        c.freeze           = fParams[kFreeze];
        c.transient_lock   = fParams[kTransLock];
        c.sensitivity      = fParams[kSensitivity];
        c.window_shape     = fParams[kWinShape];
        c.bpm_division     = fParams[kBpmDiv];
        c.formant_strength = fParams[kFormant];
        c.spectral_freeze  = fParams[kSpecFreeze];
        c.spectral_latch   = fParams[kSpecLatch];
        c.sub_gain         = fParams[kSubGain];
        c.sub_mode         = fParams[kSubMode];
        c.morph_in         = fParams[kMorphIn];
        c.morph_out        = fParams[kMorphOut];
        c.morph_beats      = fParams[kMorphBeats];
        c.phase_lock       = fParams[kPhaseLock];
        c.lookahead_enable = fParams[kLookahead];
        c.reverse          = fParams[kReverse];
        c.grain_random     = fParams[kGrainRandom];
        c.stereo_width     = fParams[kStereoWidth];
        c.spectral_tilt    = fParams[kSpectralTilt];
        c.phase_random     = fParams[kPhaseRandom];

        fPlugin.setControls(c);

        // Process audio
        fPlugin.processBlock(inputs[0], inputs[1],
                             outputs[0], outputs[1], frames);

        // Report latency
        setLatency(fPlugin.latencySamples());
    }

private:
    float fParams[kParamCount];
    HalftimePlugin fPlugin;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HalftimeDPF)
};

Plugin* createPlugin() {
    return new HalftimeDPF();
}

END_NAMESPACE_DISTRHO
