// plugin.cpp — LV2 C ABI entry point.
// This is the only file that knows about LV2.

#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <string_view>
#include <new>
#include <cstring>
#define HALFTIME_HAS_LV2
#include "HalftimePlugin.hpp"

#define PLUGIN_URI "https://github.com/milkii/halftime-lv2"

struct StateUrids {
    LV2_URID atom_Float;
    LV2_URID p[25];
};

enum PortIndex : uint32_t {
    AUDIO_IN_L          = 0,
    AUDIO_IN_R          = 1,
    AUDIO_OUT_L         = 2,
    AUDIO_OUT_R         = 3,
    CTRL_SPEED          = 4,
    CTRL_GRAIN          = 5,
    CTRL_WET            = 6,
    CTRL_PITCH          = 7,
    CTRL_STUTTER        = 8,
    CTRL_FREEZE         = 9,
    CTRL_TRANS_LOCK     = 10,
    CTRL_SENSITIVITY    = 11,
    CTRL_WIN_SHAPE      = 12,
    CTRL_BPM_DIV        = 13,
    PORT_LATENCY        = 14,
    PORT_TIME_POS       = 15,
    CTRL_FORMANT        = 16,
    CTRL_SPEC_FREEZE    = 17,
    CTRL_SPEC_LATCH     = 18,
    CTRL_SUB_GAIN       = 19,
    CTRL_SUB_MODE       = 20,
    CTRL_MORPH_IN       = 21,
    CTRL_MORPH_OUT      = 22,
    CTRL_MORPH_BEATS    = 23,
    CTRL_PHASE_LOCK     = 24,
    CTRL_LOOKAHEAD      = 25,
    CTRL_REVERSE        = 26,
    CTRL_GRAIN_RANDOM   = 27,
    CTRL_STEREO_WIDTH   = 28,
    CTRL_SPECTRAL_TILT  = 29,
    CTRL_PHASE_RANDOM   = 30,
    PORT_COUNT          = 31,
};

static_assert(PORT_COUNT == 31, "Port count must match manifest.ttl");

struct Instance {
    HalftimePlugin plugin;
    const void*    ports[PORT_COUNT] = {};
    StateUrids     surids;
    LV2_URID_Map*  map = nullptr;
};

static const char* STATE_URIS[] = {
    PLUGIN_URI "#speed",        PLUGIN_URI "#grain_ms",     PLUGIN_URI "#wet",
    PLUGIN_URI "#pitch_semi",   PLUGIN_URI "#stutter_div",  PLUGIN_URI "#freeze",
    PLUGIN_URI "#trans_lock",   PLUGIN_URI "#sensitivity",  PLUGIN_URI "#win_shape",
    PLUGIN_URI "#bpm_div",      PLUGIN_URI "#formant",      PLUGIN_URI "#spec_freeze",
    PLUGIN_URI "#spec_latch",   PLUGIN_URI "#sub_gain",     PLUGIN_URI "#sub_mode",
    PLUGIN_URI "#morph_in",     PLUGIN_URI "#morph_out",    PLUGIN_URI "#morph_beats",
    PLUGIN_URI "#phase_lock",   PLUGIN_URI "#lookahead",
    PLUGIN_URI "#reverse",      PLUGIN_URI "#grain_random", PLUGIN_URI "#stereo_width",
    PLUGIN_URI "#spectral_tilt",PLUGIN_URI "#phase_random",
};

static void mapUrids(StateUrids& u, LV2_URID_Map* map) {
    u.atom_Float = map->map(map->handle, LV2_ATOM__Float);
    for (int i = 0; i < 25; ++i)
        u.p[i] = map->map(map->handle, STATE_URIS[i]);
}

static LV2_Handle instantiate(
    const LV2_Descriptor*, double sr,
    const char*, const LV2_Feature* const* features
) {
    LV2_URID_Map* map = nullptr;
    for (int i = 0; features[i]; ++i)
        if (std::string_view(features[i]->URI) == LV2_URID__map)
            map = static_cast<LV2_URID_Map*>(features[i]->data);

    auto* inst = new (std::nothrow) Instance;
    if (!inst) return nullptr;
    inst->map = map;
    inst->plugin.setSampleRate(sr);
    if (map) { inst->plugin.initUrids(map); mapUrids(inst->surids, map); }
    return inst;
}

static void connect_port(LV2_Handle handle, uint32_t port, void* data) {
    auto* inst = static_cast<Instance*>(handle);
    if (port < PORT_COUNT) inst->ports[port] = data;
}

static void activate(LV2_Handle handle) {
    static_cast<Instance*>(handle)->plugin.reset();
}

static void deactivate(LV2_Handle) {}

static void run(LV2_Handle handle, uint32_t n) {
    auto* inst = static_cast<Instance*>(handle);

    if (inst->ports[PORT_TIME_POS])
        inst->plugin.processBpmAtoms(
            static_cast<const LV2_Atom_Sequence*>(inst->ports[PORT_TIME_POS]));

    auto rf = [&](PortIndex p) -> float {
        return inst->ports[p] ? *static_cast<const float*>(inst->ports[p]) : 0.f;
    };

    HalftimePlugin::ControlPorts c;
    c.speed            = rf(CTRL_SPEED);
    c.grain_ms         = rf(CTRL_GRAIN);
    c.wet              = rf(CTRL_WET);
    c.pitch_semi       = rf(CTRL_PITCH);
    c.stutter_div      = rf(CTRL_STUTTER);
    c.freeze           = rf(CTRL_FREEZE);
    c.transient_lock   = rf(CTRL_TRANS_LOCK);
    c.sensitivity      = rf(CTRL_SENSITIVITY);
    c.window_shape     = rf(CTRL_WIN_SHAPE);
    c.bpm_division     = rf(CTRL_BPM_DIV);
    c.formant_strength = rf(CTRL_FORMANT);
    c.spectral_freeze  = rf(CTRL_SPEC_FREEZE);
    c.spectral_latch   = rf(CTRL_SPEC_LATCH);
    c.sub_gain         = rf(CTRL_SUB_GAIN);
    c.sub_mode         = rf(CTRL_SUB_MODE);
    c.morph_in         = rf(CTRL_MORPH_IN);
    c.morph_out        = rf(CTRL_MORPH_OUT);
    c.morph_beats      = rf(CTRL_MORPH_BEATS);
    c.phase_lock       = rf(CTRL_PHASE_LOCK);
    c.lookahead_enable = rf(CTRL_LOOKAHEAD);
    c.reverse          = rf(CTRL_REVERSE);
    c.grain_random     = rf(CTRL_GRAIN_RANDOM);
    c.stereo_width     = rf(CTRL_STEREO_WIDTH);
    c.spectral_tilt    = rf(CTRL_SPECTRAL_TILT);
    c.phase_random     = rf(CTRL_PHASE_RANDOM);

    inst->plugin.setControls(c);

    if (inst->ports[PORT_LATENCY])
        *static_cast<float*>(const_cast<void*>(inst->ports[PORT_LATENCY]))
            = static_cast<float>(inst->plugin.latencySamples());

    inst->plugin.processBlock(
        static_cast<const float*>(inst->ports[AUDIO_IN_L]),
        static_cast<const float*>(inst->ports[AUDIO_IN_R]),
        static_cast<float*>(const_cast<void*>(inst->ports[AUDIO_OUT_L])),
        static_cast<float*>(const_cast<void*>(inst->ports[AUDIO_OUT_R])),
        n);
}

static void cleanup(LV2_Handle handle) {
    delete static_cast<Instance*>(handle);
}

static LV2_State_Status state_save(
    LV2_Handle handle,
    LV2_State_Store_Function store, LV2_State_Handle sh,
    uint32_t, const LV2_Feature* const*
) {
    auto* inst = static_cast<Instance*>(handle);
    const auto& c = inst->plugin.lastControls();
    const auto& u = inst->surids;

    const float fields[] = {
        c.speed, c.grain_ms, c.wet, c.pitch_semi, c.stutter_div,
        c.freeze, c.transient_lock, c.sensitivity, c.window_shape,
        c.bpm_division, c.formant_strength, c.spectral_freeze,
        c.spectral_latch, c.sub_gain, c.sub_mode,
        c.morph_in, c.morph_out, c.morph_beats,
        c.phase_lock, c.lookahead_enable,
        c.reverse, c.grain_random, c.stereo_width,
        c.spectral_tilt, c.phase_random
    };
    static_assert(sizeof(fields)/sizeof(float) == 25);

    for (int i = 0; i < 25; ++i)
        store(sh, u.p[i], &fields[i], sizeof(float), u.atom_Float,
              LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

    return LV2_STATE_SUCCESS;
}

static LV2_State_Status state_restore(
    LV2_Handle handle,
    LV2_State_Retrieve_Function retrieve, LV2_State_Handle sh,
    uint32_t, const LV2_Feature* const*
) {
    auto* inst = static_cast<Instance*>(handle);
    const auto& u = inst->surids;
    auto c = inst->plugin.lastControls();

    float* fields[] = {
        &c.speed, &c.grain_ms, &c.wet, &c.pitch_semi, &c.stutter_div,
        &c.freeze, &c.transient_lock, &c.sensitivity, &c.window_shape,
        &c.bpm_division, &c.formant_strength, &c.spectral_freeze,
        &c.spectral_latch, &c.sub_gain, &c.sub_mode,
        &c.morph_in, &c.morph_out, &c.morph_beats,
        &c.phase_lock, &c.lookahead_enable,
        &c.reverse, &c.grain_random, &c.stereo_width,
        &c.spectral_tilt, &c.phase_random
    };

    for (int i = 0; i < 25; ++i) {
        std::size_t sz; uint32_t type, flags;
        const void* val = retrieve(sh, u.p[i], &sz, &type, &flags);
        if (val && sz == sizeof(float))
            *fields[i] = *static_cast<const float*>(val);
    }

    inst->plugin.setControls(c);
    return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_iface = { state_save, state_restore };

static const void* extension_data(const char* uri) {
    if (std::string_view(uri) == LV2_STATE__interface) return &state_iface;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI, instantiate, connect_port,
    activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
