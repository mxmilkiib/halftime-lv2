#pragma once

#include <QMap>

#include "effects/backends/effectprocessor.h"
#include "engine/engine.h"
#include "util/class.h"
#include "util/types.h"

// No HALFTIME_HAS_LV2 — BpmSync is excluded, setBpm() used directly.
#include "HalftimePlugin.hpp"

class HalftimeGroupState : public EffectState {
  public:
    HalftimeGroupState(const mixxx::EngineParameters& engineParameters)
            : EffectState(engineParameters) {
        plugin.setSampleRate(engineParameters.sampleRate());
        plugin.reset();
    }
    ~HalftimeGroupState() override = default;

    HalftimePlugin plugin;
};

class HalftimeEffect : public EffectProcessorImpl<HalftimeGroupState> {
  public:
    HalftimeEffect() = default;
    ~HalftimeEffect() override = default;

    static QString getId();
    static EffectManifestPointer getManifest();

    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;

    void processChannel(
            HalftimeGroupState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

    SINT getGroupDelayFrames() override;

  private:
    QString debugString() const {
        return getId();
    }

    EngineEffectParameterPointer m_pSpeed;
    EngineEffectParameterPointer m_pGrainMs;
    EngineEffectParameterPointer m_pPitch;
    EngineEffectParameterPointer m_pStutter;
    EngineEffectParameterPointer m_pFreeze;
    EngineEffectParameterPointer m_pTransLock;
    EngineEffectParameterPointer m_pSensitivity;
    EngineEffectParameterPointer m_pPhaseLock;
    EngineEffectParameterPointer m_pLookahead;
    EngineEffectParameterPointer m_pFormant;
    EngineEffectParameterPointer m_pSpecFreeze;
    EngineEffectParameterPointer m_pSubGain;

    SINT m_lastLatency = 0;

    DISALLOW_COPY_AND_ASSIGN(HalftimeEffect);
};
