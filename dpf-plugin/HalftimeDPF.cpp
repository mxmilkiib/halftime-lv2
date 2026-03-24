// HalftimeDPF.cpp — DPF Plugin class wrapping HalftimePlugin.
// DSP lives entirely in HalftimePlugin.hpp (no LV2 dependency).

#include "DistrhoPlugin.hpp"
#include "HalftimePlugin.hpp"
#include "HalftimePresets.hpp"

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
    kInputGain,
    kOutputGain,
    kEqLow,
    kEqMid,
    kEqHigh,
    kEqOutLow,
    kEqOutMid,
    kEqOutHigh,
    kManualBpm,
    kSubFreq,
    kSubDrive,
    kNumWritable,
    kBpmDisplay = kNumWritable,
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
        : Plugin(kParamCount, NUM_PRESETS, 0) // params, programs, states
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
        fParams[kPhaseLock]    = 0.f;
        fParams[kLookahead]    = 1.f;
        fParams[kReverse]      = 0.f;
        fParams[kGrainRandom]  = 0.f;
        fParams[kStereoWidth]  = 1.f;
        fParams[kSpectralTilt] = 0.f;
        fParams[kPhaseRandom]  = 0.f;
        fParams[kInputGain]    = 0.f;
        fParams[kOutputGain]   = 0.f;
        fParams[kEqLow]        = 0.f;
        fParams[kEqMid]        = 0.f;
        fParams[kEqHigh]       = 0.f;
        fParams[kEqOutLow]     = 0.f;
        fParams[kEqOutMid]     = 0.f;
        fParams[kEqOutHigh]    = 0.f;
        fParams[kManualBpm]    = 0.f;
        fParams[kSubFreq]      = 80.f;
        fParams[kSubDrive]     = 0.f;
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
            p.description = "Playback speed of the time-stretched signal. 50% = half speed, 100% = normal.";
            p.ranges.min = 25.f; p.ranges.max = 100.f; p.ranges.def = 50.f;
            p.groupId = kGroupCore;
            break;
        case kGrainMs:
            p.name = "Grain Size"; p.symbol = "grain_ms"; p.unit = "ms";
            p.description = "Length of each overlap-add grain. Shorter = tighter transients, longer = smoother sustain.";
            p.ranges.min = 20.f; p.ranges.max = 500.f; p.ranges.def = 80.f;
            p.groupId = kGroupCore;
            break;
        case kWet:
            p.name = "Wet"; p.symbol = "wet";
            p.description = "Blend between dry input and processed signal. 0 = fully dry, 1 = fully wet.";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 1.f;
            p.groupId = kGroupCore;
            break;
        case kPitchSemi:
            p.name = "Pitch"; p.symbol = "pitch_semi"; p.unit = "st";
            p.description = "Pitch shift in semitones applied via the phase vocoder. Independent of speed.";
            p.ranges.min = -12.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupPitch;
            break;
        case kStutterDiv:
            p.name = "Stutter"; p.symbol = "stutter_div";
            p.description = "Rhythmic loop subdivisions. Divides the grain into N equal loops for glitch/beat-repeat effects.";
            p.hints |= kParameterIsInteger;
            p.ranges.min = 1.f; p.ranges.max = 16.f; p.ranges.def = 1.f;
            p.groupId = kGroupModulation;
            break;
        case kFreeze:
            p.name = "Freeze"; p.symbol = "freeze";
            p.description = "Freezes the grain buffer, looping the current captured audio indefinitely.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupCore;
            break;
        case kTransLock:
            p.name = "Transient Lock"; p.symbol = "trans_lock";
            p.description = "Snaps grain boundaries to detected transients, preserving attack clarity.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kSensitivity:
            p.name = "Sensitivity"; p.symbol = "sensitivity";
            p.description = "Transient detection threshold. Higher values detect quieter onsets for Transient Lock.";
            p.ranges.min = 0.5f; p.ranges.max = 2.f; p.ranges.def = 1.f;
            p.groupId = kGroupGrain;
            break;
        case kWinShape:
            p.name = "Window"; p.symbol = "win_shape";
            p.description = "Grain window shape. Hann = smooth, Blackman = less leakage, Rect = sharp transients.";
            p.hints |= kParameterIsInteger;
            p.ranges.min = 0.f; p.ranges.max = 2.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kBpmDiv:
            p.name = "BPM Div"; p.symbol = "bpm_div";
            p.description = "Syncs grain size to host tempo. The fraction sets grain length as a note division of the beat.";
            p.hints |= kParameterIsInteger;
            p.ranges.min = 0.f; p.ranges.max = 8.f; p.ranges.def = 0.f;
            p.groupId = kGroupModulation;
            break;
        case kFormant:
            p.name = "Formant"; p.symbol = "formant";
            p.description = "Formant preservation strength during pitch shifting. Keeps vowel character at 1.0.";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 1.f;
            p.groupId = kGroupPitch;
            break;
        case kSpecFreeze:
            p.name = "Spectral Freeze"; p.symbol = "spec_freeze";
            p.description = "Freezes the frequency spectrum, creating sustained tonal drones from the current sound.";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        case kSpecLatch:
            p.name = "Spectral Latch"; p.symbol = "spec_latch";
            p.description = "Latches the frozen spectrum so it persists after Spectral Freeze is turned off.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        case kSubGain:
            p.name = "Sub Bass"; p.symbol = "sub_gain";
            p.description = "Adds synthesised sub-bass harmonics below the fundamental, thickening low end.";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kSubMode:
            p.name = "Sub Mode"; p.symbol = "sub_mode";
            p.description = "Switches sub-bass generation algorithm. Off = octave-down, On = filtered sub-harmonic.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kMorphIn:
            p.name = "Morph In"; p.symbol = "morph_in";
            p.description = "Gradually ramps speed from 100% down to the set value over the morph duration.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupModulation;
            break;
        case kMorphOut:
            p.name = "Morph Out"; p.symbol = "morph_out";
            p.description = "Gradually ramps speed from the set value back up to 100% over the morph duration.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupModulation;
            break;
        case kMorphBeats:
            p.name = "Morph Beats"; p.symbol = "morph_beats";
            p.description = "Duration of the morph transition in beats. Longer values give a more gradual speed ramp.";
            p.ranges.min = 0.5f; p.ranges.max = 8.f; p.ranges.def = 2.f;
            p.groupId = kGroupModulation;
            break;
        case kPhaseLock:
            p.name = "Phase Lock"; p.symbol = "phase_lock";
            p.description = "Locks phase vocoder bin phases together, reducing phasiness on tonal material.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupPitch;
            break;
        case kLookahead:
            p.name = "Lookahead"; p.symbol = "lookahead";
            p.description = "Enables transient lookahead buffer. Adds latency but improves attack preservation.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 1.f;
            p.groupId = kGroupGrain;
            break;
        case kReverse:
            p.name = "Reverse"; p.symbol = "reverse";
            p.description = "Reads grains backwards, creating reversed playback textures within each grain.";
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kGrainRandom:
            p.name = "Grain Random"; p.symbol = "grain_random";
            p.description = "Randomises grain start positions. Adds textural variation and diffuses rhythmic patterns.";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupGrain;
            break;
        case kStereoWidth:
            p.name = "Stereo Width"; p.symbol = "stereo_width";
            p.description = "Controls stereo image width. 0 = mono, 1 = original, 2 = extra wide.";
            p.ranges.min = 0.f; p.ranges.max = 2.f; p.ranges.def = 1.f;
            p.groupId = kGroupOutput;
            break;
        case kSpectralTilt:
            p.name = "Spectral Tilt"; p.symbol = "spectral_tilt";
            p.description = "Tilts the spectral balance. Negative = darker/warmer, positive = brighter/airier.";
            p.ranges.min = -1.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        case kPhaseRandom:
            p.name = "Phase Random"; p.symbol = "phase_random";
            p.description = "Randomises spectral phase, smearing transients into diffuse ambient textures.";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupSpectral;
            break;
        case kInputGain:
            p.name = "Input Gain"; p.symbol = "input_gain"; p.unit = "dB";
            p.description = "Adjusts input level before processing. Use to prevent clipping or boost quiet signals.";
            p.ranges.min = -24.f; p.ranges.max = 24.f; p.ranges.def = 0.f;
            p.groupId = kGroupCore;
            break;
        case kOutputGain:
            p.name = "Output Gain"; p.symbol = "output_gain"; p.unit = "dB";
            p.description = "Final output level after all processing. Compensates for volume changes from effects.";
            p.ranges.min = -24.f; p.ranges.max = 24.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kEqLow:
            p.name = "EQ Low"; p.symbol = "eq_low"; p.unit = "dB";
            p.description = "Input low-frequency shelf. Cut to remove rumble, boost to add warmth before processing.";
            p.ranges.min = -48.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupCore;
            break;
        case kEqMid:
            p.name = "EQ Mid"; p.symbol = "eq_mid"; p.unit = "dB";
            p.description = "Input mid-frequency band. Shape the body and presence of the signal before processing.";
            p.ranges.min = -48.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupCore;
            break;
        case kEqHigh:
            p.name = "EQ High"; p.symbol = "eq_high"; p.unit = "dB";
            p.description = "Input high-frequency shelf. Cut to tame harshness, boost to add air before processing.";
            p.ranges.min = -48.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupCore;
            break;
        case kEqOutLow:
            p.name = "Out EQ Low"; p.symbol = "eq_out_low"; p.unit = "dB";
            p.description = "Output low-frequency shelf. Shape the low end of the final processed signal.";
            p.ranges.min = -48.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kEqOutMid:
            p.name = "Out EQ Mid"; p.symbol = "eq_out_mid"; p.unit = "dB";
            p.description = "Output mid-frequency band. Adjust body and clarity of the final processed signal.";
            p.ranges.min = -48.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kEqOutHigh:
            p.name = "Out EQ High"; p.symbol = "eq_out_high"; p.unit = "dB";
            p.description = "Output high-frequency shelf. Tame or enhance the top end of the processed signal.";
            p.ranges.min = -48.f; p.ranges.max = 12.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kManualBpm:
            p.name = "Manual BPM"; p.symbol = "manual_bpm";
            p.description = "Override host tempo with a manual BPM value. Set to 0 for automatic host sync.";
            p.ranges.min = 0.f; p.ranges.max = 300.f; p.ranges.def = 0.f;
            p.groupId = kGroupModulation;
            break;
        case kSubFreq:
            p.name = "Sub Frequency"; p.symbol = "sub_freq"; p.unit = "Hz";
            p.description = "Cutoff frequency for the sub-bass lowpass filter. Lower values produce deeper sub content.";
            p.ranges.min = 30.f; p.ranges.max = 200.f; p.ranges.def = 80.f;
            p.groupId = kGroupOutput;
            break;
        case kSubDrive:
            p.name = "Sub Drive"; p.symbol = "sub_drive";
            p.description = "Soft-clip saturation on the sub-bass signal. Adds harmonic warmth and weight.";
            p.ranges.min = 0.f; p.ranges.max = 1.f; p.ranges.def = 0.f;
            p.groupId = kGroupOutput;
            break;
        case kBpmDisplay:
            p.name = "BPM"; p.symbol = "bpm_display";
            p.hints = kParameterIsOutput;
            p.ranges.min = 0.f; p.ranges.max = 999.f; p.ranges.def = 0.f;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override {
        return (index < kParamCount) ? fParams[index] : 0.f;
    }

    void setParameterValue(uint32_t index, float value) override {
        if (index < kParamCount) fParams[index] = value;
    }

    void initProgramName(uint32_t index, String& name) override {
        if (index < NUM_PRESETS)
            name = halftimePresets[index].name;
    }

    void loadProgram(uint32_t index) override {
        if (index >= NUM_PRESETS) return;
        const auto& p = halftimePresets[index];
        for (int i = 0; i < kNumWritable; ++i)
            fParams[i] = p.v[i];
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
        // BPM: prefer host, fall back to manual
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 0.0) {
            fPlugin.setBpm(tp.bbt.beatsPerMinute);
            fParams[kBpmDisplay] = static_cast<float>(tp.bbt.beatsPerMinute);
        } else if (fParams[kManualBpm] > 0.f) {
            fPlugin.setBpm(static_cast<double>(fParams[kManualBpm]));
            fParams[kBpmDisplay] = fParams[kManualBpm];
        }

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
        c.input_gain       = fParams[kInputGain];
        c.output_gain      = fParams[kOutputGain];
        c.eq_low           = fParams[kEqLow];
        c.eq_mid           = fParams[kEqMid];
        c.eq_high          = fParams[kEqHigh];
        c.eq_out_low       = fParams[kEqOutLow];
        c.eq_out_mid       = fParams[kEqOutMid];
        c.eq_out_high      = fParams[kEqOutHigh];
        c.manual_bpm       = fParams[kManualBpm];
        c.sub_freq         = fParams[kSubFreq];
        c.sub_drive        = fParams[kSubDrive];

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
