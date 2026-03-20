#include "effects/backends/builtin/halftimeeffect.h"

#include "effects/backends/effectmanifest.h"
#include "engine/effects/engineeffectparameter.h"
#include "util/sample.h"

// static
QString HalftimeEffect::getId() {
    return QStringLiteral("org.mixxx.effects.halftime");
}

// static
EffectManifestPointer HalftimeEffect::getManifest() {
    EffectManifestPointer pManifest(new EffectManifest());

    pManifest->setId(getId());
    pManifest->setName(QObject::tr("Halftime"));
    pManifest->setShortName(QObject::tr("Halftime"));
    pManifest->setAuthor("mxmilkiib");
    pManifest->setVersion("1.0");
    pManifest->setDescription(QObject::tr(
            "WSOLA-based halftime/slowdown effect with phase vocoder "
            "pitch shifting and spectral processing"));
    pManifest->setEffectRampsFromDry(true);
    pManifest->setAddDryToWet(false);

    EffectManifestParameterPointer speed = pManifest->addParameter();
    speed->setId("speed");
    speed->setName(QObject::tr("Speed"));
    speed->setShortName(QObject::tr("Speed"));
    speed->setDescription(QObject::tr("Playback speed (25-100%)"));
    speed->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    speed->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    speed->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    speed->setRange(25.0, 50.0, 100.0);

    EffectManifestParameterPointer grain = pManifest->addParameter();
    grain->setId("grain_ms");
    grain->setName(QObject::tr("Grain"));
    grain->setShortName(QObject::tr("Grain"));
    grain->setDescription(QObject::tr("Grain size in milliseconds"));
    grain->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    grain->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    grain->setRange(20.0, 80.0, 500.0);

    EffectManifestParameterPointer pitch = pManifest->addParameter();
    pitch->setId("pitch_semi");
    pitch->setName(QObject::tr("Pitch"));
    pitch->setShortName(QObject::tr("Pitch"));
    pitch->setDescription(QObject::tr("Pitch shift in semitones"));
    pitch->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    pitch->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    pitch->setRange(-12.0, 0.0, 12.0);

    EffectManifestParameterPointer stutter = pManifest->addParameter();
    stutter->setId("stutter_div");
    stutter->setName(QObject::tr("Stutter"));
    stutter->setShortName(QObject::tr("Stutter"));
    stutter->setDescription(QObject::tr("Stutter loop division (1 = off)"));
    stutter->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    stutter->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    stutter->setRange(1.0, 1.0, 16.0);

    EffectManifestParameterPointer freeze = pManifest->addParameter();
    freeze->setId("freeze");
    freeze->setName(QObject::tr("Freeze"));
    freeze->setShortName(QObject::tr("Freeze"));
    freeze->setDescription(QObject::tr("Freeze the grain buffer"));
    freeze->setValueScaler(EffectManifestParameter::ValueScaler::Toggle);
    freeze->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    freeze->setRange(0.0, 0.0, 1.0);

    EffectManifestParameterPointer transLock = pManifest->addParameter();
    transLock->setId("trans_lock");
    transLock->setName(QObject::tr("Transient Lock"));
    transLock->setShortName(QObject::tr("Trans"));
    transLock->setDescription(QObject::tr(
            "Snap grain boundaries to detected transients"));
    transLock->setValueScaler(EffectManifestParameter::ValueScaler::Toggle);
    transLock->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    transLock->setRange(0.0, 0.0, 1.0);

    EffectManifestParameterPointer sensitivity = pManifest->addParameter();
    sensitivity->setId("sensitivity");
    sensitivity->setName(QObject::tr("Sensitivity"));
    sensitivity->setShortName(QObject::tr("Sens"));
    sensitivity->setDescription(QObject::tr("Transient detection sensitivity"));
    sensitivity->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    sensitivity->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    sensitivity->setRange(0.5, 1.0, 2.0);

    EffectManifestParameterPointer phaseLock = pManifest->addParameter();
    phaseLock->setId("phase_lock");
    phaseLock->setName(QObject::tr("Phase Lock"));
    phaseLock->setShortName(QObject::tr("PhLock"));
    phaseLock->setDescription(QObject::tr(
            "Enable Laroche-Dolson phase locking for cleaner harmonics"));
    phaseLock->setValueScaler(EffectManifestParameter::ValueScaler::Toggle);
    phaseLock->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    phaseLock->setRange(0.0, 1.0, 1.0);

    EffectManifestParameterPointer lookahead = pManifest->addParameter();
    lookahead->setId("lookahead");
    lookahead->setName(QObject::tr("Lookahead"));
    lookahead->setShortName(QObject::tr("Look"));
    lookahead->setDescription(QObject::tr(
            "Enable transient lookahead scheduling (adds latency)"));
    lookahead->setValueScaler(EffectManifestParameter::ValueScaler::Toggle);
    lookahead->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    lookahead->setRange(0.0, 1.0, 1.0);

    EffectManifestParameterPointer formant = pManifest->addParameter();
    formant->setId("formant");
    formant->setName(QObject::tr("Formant"));
    formant->setShortName(QObject::tr("Fmnt"));
    formant->setDescription(QObject::tr("Formant preservation strength"));
    formant->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    formant->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    formant->setRange(0.0, 1.0, 1.0);

    EffectManifestParameterPointer specFreeze = pManifest->addParameter();
    specFreeze->setId("spec_freeze");
    specFreeze->setName(QObject::tr("Spectral Freeze"));
    specFreeze->setShortName(QObject::tr("SpFrz"));
    specFreeze->setDescription(QObject::tr("Spectral freeze blend amount"));
    specFreeze->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    specFreeze->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    specFreeze->setRange(0.0, 0.0, 1.0);

    EffectManifestParameterPointer subGain = pManifest->addParameter();
    subGain->setId("sub_gain");
    subGain->setName(QObject::tr("Sub Bass"));
    subGain->setShortName(QObject::tr("Sub"));
    subGain->setDescription(QObject::tr("Sub-harmonic enhancement gain"));
    subGain->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    subGain->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    subGain->setRange(0.0, 0.0, 1.0);

    return pManifest;
}

void HalftimeEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pSpeed       = parameters.value("speed");
    m_pGrainMs     = parameters.value("grain_ms");
    m_pPitch       = parameters.value("pitch_semi");
    m_pStutter     = parameters.value("stutter_div");
    m_pFreeze      = parameters.value("freeze");
    m_pTransLock   = parameters.value("trans_lock");
    m_pSensitivity = parameters.value("sensitivity");
    m_pPhaseLock   = parameters.value("phase_lock");
    m_pLookahead   = parameters.value("lookahead");
    m_pFormant     = parameters.value("formant");
    m_pSpecFreeze  = parameters.value("spec_freeze");
    m_pSubGain     = parameters.value("sub_gain");
}

void HalftimeEffect::processChannel(
        HalftimeGroupState* pState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& engineParameters,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    HalftimePlugin& plugin = pState->plugin;

    // Push BPM from Mixxx beat info
    if (groupFeatures.beat_length.has_value() &&
            groupFeatures.beat_length->seconds > 0.0) {
        const double bpm = 60.0 / groupFeatures.beat_length->seconds;
        plugin.setBpm(bpm);
    }

    // Map Mixxx parameters to HalftimePlugin controls
    HalftimePlugin::ControlPorts c;
    c.speed            = static_cast<float>(m_pSpeed->value());
    c.grain_ms         = static_cast<float>(m_pGrainMs->value());
    c.pitch_semi       = static_cast<float>(m_pPitch->value());
    c.stutter_div      = static_cast<float>(m_pStutter->value());
    c.freeze           = m_pFreeze->toBool() ? 1.f : 0.f;
    c.transient_lock   = m_pTransLock->toBool() ? 1.f : 0.f;
    c.sensitivity      = static_cast<float>(m_pSensitivity->value());
    c.phase_lock       = m_pPhaseLock->toBool() ? 1.f : 0.f;
    c.lookahead_enable = m_pLookahead->toBool() ? 1.f : 0.f;
    c.formant_strength = static_cast<float>(m_pFormant->value());
    c.spectral_freeze  = static_cast<float>(m_pSpecFreeze->value());
    c.sub_gain         = static_cast<float>(m_pSubGain->value());
    c.wet              = 1.f;
    plugin.pushControls(c);

    const SINT numFrames = engineParameters.framesPerBuffer();
    const SINT numChannels = engineParameters.channelCount();
    const SINT numSamples = engineParameters.samplesPerBuffer();

    // Deinterleave stereo input -> separate L/R buffers
    // Stack-allocate for typical block sizes (up to 4096 frames)
    float buf_l[4096];
    float buf_r[4096];
    float out_l[4096];
    float out_r[4096];

    const SINT frames = std::min(numFrames, static_cast<SINT>(4096));

    if (numChannels >= 2) {
        for (SINT i = 0; i < frames; ++i) {
            buf_l[i] = pInput[i * numChannels];
            buf_r[i] = pInput[i * numChannels + 1];
        }
    } else {
        for (SINT i = 0; i < frames; ++i) {
            buf_l[i] = pInput[i];
            buf_r[i] = pInput[i];
        }
    }

    // Process
    plugin.processBlock(buf_l, buf_r, out_l, out_r, static_cast<uint32_t>(frames));

    // Reinterleave to stereo output
    if (numChannels >= 2) {
        for (SINT i = 0; i < frames; ++i) {
            pOutput[i * numChannels]     = out_l[i];
            pOutput[i * numChannels + 1] = out_r[i];
        }
    } else {
        for (SINT i = 0; i < frames; ++i) {
            pOutput[i] = out_l[i];
        }
    }

    // Ramp to dry on disable
    if (enableState == EffectEnableState::Disabling) {
        SampleUtil::applyRampingGain(
                pOutput, 1.0, 0.0, numSamples);
        plugin.reset();
    }

    m_lastLatency = static_cast<SINT>(plugin.latencySamples());
}

SINT HalftimeEffect::getGroupDelayFrames() {
    return m_lastLatency;
}
