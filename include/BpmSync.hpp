#pragma once
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/time/time.h>
#include <lv2/urid/urid.h>
#include <lv2/core/lv2.h>
#include <cmath>
#include <cstdint>
#include <algorithm>

// BpmSync — reads tempo from the LV2 time:Position atom sequence
// and computes grain sizes aligned to musical beat divisions.
//
// LV2 hosts publish tempo changes as atom events on a designated atom port.
// We cache the last-seen BPM and recompute grain size whenever it changes.
//
// Division values:
//   0 = off (manual grain_ms)
//   1 = 1 beat
//   2 = 1/2 beat
//   4 = 1/4 beat
//   8 = 1/8 beat

class BpmSync {
public:
    struct Urids {
        LV2_URID atom_Object;
        LV2_URID atom_Blank;
        LV2_URID atom_Float;
        LV2_URID atom_Double;
        LV2_URID atom_Long;
        LV2_URID time_Position;
        LV2_URID time_beatsPerMinute;
        LV2_URID time_speed;
        LV2_URID time_frame;
    };

    void init(LV2_URID_Map* map) {
        if (!map) return;
        map_ = map;
        urids_.atom_Object          = map->map(map->handle, LV2_ATOM__Object);
        urids_.atom_Blank           = map->map(map->handle, LV2_ATOM__Blank);
        urids_.atom_Float           = map->map(map->handle, LV2_ATOM__Float);
        urids_.atom_Double          = map->map(map->handle, LV2_ATOM__Double);
        urids_.atom_Long            = map->map(map->handle, LV2_ATOM__Long);
        urids_.time_Position        = map->map(map->handle, LV2_TIME__Position);
        urids_.time_beatsPerMinute  = map->map(map->handle, LV2_TIME__beatsPerMinute);
        urids_.time_speed           = map->map(map->handle, LV2_TIME__speed);
        urids_.time_frame           = map->map(map->handle, LV2_TIME__frame);
        initialised_ = true;
    }

    void setSampleRate(double sr) noexcept { sr_ = sr; }

    void setDivision(int div) noexcept {
        division_ = std::clamp(div, 0, 8);
    }

    int division() const noexcept { return division_; }
    double bpm() const noexcept { return bpm_; }
    bool active() const noexcept { return division_ > 0 && bpm_ > 0.0; }

    // Read tempo from LV2 atom sequence. Returns true if BPM changed.
    bool readPosition(const LV2_Atom_Sequence* seq) noexcept {
        if (!initialised_ || !seq) return false;

        bool changed = false;
        LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
            if (ev->body.type != urids_.atom_Object &&
                ev->body.type != urids_.atom_Blank)
                continue;

            const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
            if (obj->body.otype != urids_.time_Position)
                continue;

            const LV2_Atom* bpm_atom = nullptr;
            lv2_atom_object_get(obj,
                urids_.time_beatsPerMinute, &bpm_atom,
                0);

            if (bpm_atom) {
                double new_bpm = 0.0;
                if (bpm_atom->type == urids_.atom_Float)
                    new_bpm = static_cast<double>(
                        reinterpret_cast<const LV2_Atom_Float*>(bpm_atom)->body);
                else if (bpm_atom->type == urids_.atom_Double)
                    new_bpm = reinterpret_cast<const LV2_Atom_Double*>(bpm_atom)->body;

                if (new_bpm > 0.0 && std::abs(new_bpm - bpm_) > 0.01) {
                    bpm_ = new_bpm;
                    changed = true;
                }
            }
        }
        return changed;
    }

    // Compute grain size in samples for a given beat division.
    // div=1: one beat, div=2: half beat, div=4: quarter beat, etc.
    std::size_t grainSamples(int div) const noexcept {
        if (div <= 0 || bpm_ <= 0.0) return 3528; // fallback: 80ms @ 44.1kHz

        const double beat_samps = sr_ * 60.0 / bpm_;
        const double grain = beat_samps / static_cast<double>(div);

        return static_cast<std::size_t>(
            std::clamp(grain, 64.0, static_cast<double>(OlaEngine_MAX_BUF / 4)));
    }

private:
    // Avoid including OlaEngine.hpp — just replicate the constant
    static constexpr std::size_t OlaEngine_MAX_BUF = 262144;

    LV2_URID_Map* map_ = nullptr;
    Urids urids_ = {};
    double sr_   = 44100.0;
    double bpm_  = 0.0;
    int division_ = 0;
    bool initialised_ = false;
};
