#pragma once

// Shared preset definitions for both DSP and UI.
// Parameter order matches ParamId enum in HalftimeDPF.cpp / HalftimeUI.cpp:
//   kSpeed, kGrainMs, kWet, kPitchSemi, kStutterDiv,
//   kFreeze, kTransLock, kSensitivity, kWinShape, kBpmDiv,
//   kFormant, kSpecFreeze, kSpecLatch, kSubGain, kSubMode,
//   kMorphIn, kMorphOut, kMorphBeats, kPhaseLock, kLookahead,
//   kReverse, kGrainRandom, kStereoWidth, kSpectralTilt, kPhaseRandom,
//   kInputGain, kOutputGain, kEqLow, kEqMid, kEqHigh,
//   kEqOutLow, kEqOutMid, kEqOutHigh, kManualBpm, kSubFreq, kSubDrive, kSmooth

static constexpr int NUM_PRESETS     = 8;
static constexpr int PRESET_NUM_VALS = 37;

struct HalftimePreset {
    const char* name;
    float v[PRESET_NUM_VALS];
};

static const HalftimePreset halftimePresets[NUM_PRESETS] = {
    //                        Spd  Grn   Wet  Ptch Stut Frz  TrLk Sens Win  Bpm  Fmt  SFrz SLat SubG SubM MIn  MOut MBt  PLck Look Rev  GRnd Wid  Tilt PRnd InG  OutG EqL  EqM  EqH  OqL  OqM  OqH  MBpm SFrq SDrv Smth
    {"Default",            {  50,  80,  1.0,   0,   1,  0,   0,  1.0,  0,   0,  1.0, 0.0, 0,   0.0, 0,   0,   0,   2,   0,   1,   0,  0.0, 1.0,  0.0, 0.0, 0,   0,   0,   0,   0,   0,   0,   0,   0,  80,  0,  0.5}},
    {"Gentle Slow",        {  70, 100,  0.5,   0,   1,  0,   0,  1.0,  0,   0,  1.0, 0.0, 0,   0.0, 0,   0,   0,   2,   0,   1,   0,  0.0, 1.0,  0.0, 0.0, 0,   0,   0,   0,   0,   0,   0,   0,   0,  80,  0,  0.7}},
    {"Deep Half",          {  25, 120,  1.0,   0,   1,  0,   0,  1.2,  0,   0,  0.8, 0.0, 0,   0.3, 0,   0,   0,   2,   0,   1,   0,  0.0, 1.2,  0.0, 0.0, 0,   0,   3,   0,  -2,   0,   0,   0,   0,  60,  0.3, 0.6}},
    {"Octave Down",        {  50,  80,  1.0, -12,   1,  0,   0,  1.0,  0,   0,  1.0, 0.0, 0,   0.2, 0,   0,   0,   2,   0,   1,   0,  0.0, 1.0,  0.0, 0.0, 0,   0,   4,   0,   0,   0,   0,   0,   0,  80,  0,  0.5}},
    {"Frozen Texture",     {  50,  80,  1.0,   0,   1,  1,   0,  1.0,  0,   0,  1.0, 0.8, 1,   0.0, 0,   0,   0,   2,   0,   1,   0,  0.0, 1.5,  0.0, 0.0, 0,   0,   0,   0,   0,   0,   0,   0,   0,  80,  0,  0.9}},
    {"Stutter Beat",       {  50,  40,  1.0,   0,   4,  0,   0,  1.0,  0,   4,  1.0, 0.0, 0,   0.0, 0,   0,   0,   2,   0,   1,   0,  0.0, 1.0,  0.0, 0.0, 0,   0,   0,   0,   0,   0,   0,   0,   0,  80,  0,  0.2}},
    {"Ambient Wash",       {  40, 200,  0.8,   0,   1,  0,   0,  1.0,  1,   0,  0.7, 0.0, 0,   0.0, 0,   0,   0,   4,   0,   1,   1,  0.5, 2.0,  0.0, 0.3, 0,   0,   0,   2,   0,   0,   0,   0,   0,  80,  0,  0.9}},
    {"Lo-Fi Tape",         {  50,  60,  0.7,  -1,   1,  0,   0,  1.0,  2,   0,  0.8, 0.0, 0,   0.0, 0,   0,   0,   2,   0,   1,   0,  0.3, 1.0, -0.5, 0.0, 0,   0,   2,  -1,  -3,   0,   0,  -2,   0,  80,  0,  0.4}},
};
