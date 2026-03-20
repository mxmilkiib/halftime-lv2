#pragma once
#include <cmath>
#include <cstdint>
#include <atomic>
#include <algorithm>

// MorphController — smooth speed ramp between 1.0 (normal) and a target
// halftime speed over a specified number of beats.
//
// Used for "morph in" (gradually slow down to halftime) and "morph out"
// (gradually return to normal speed) transitions.
//
// Thread-safe: morphIn()/morphOut() can be called from the control thread.
// tick() is called per-sample from the audio thread.

class MorphController {
public:
    enum class MorphDir { In, Out };

    void setSampleRate(double sr) noexcept { sr_ = sr; }

    void setBpm(double bpm) noexcept {
        bpm_ = std::max(bpm, 20.0);
    }

    void setTargetSpeed(double speed) noexcept {
        target_speed_ = std::clamp(speed, 0.25, 1.0);
    }

    // Trigger a morph-in (ramp from current speed to target halftime speed)
    void morphIn(double beats = 2.0) noexcept {
        pending_msg_.store(pack(beats, MorphDir::In), std::memory_order_release);
        pending_.store(true, std::memory_order_release);
    }

    // Trigger a morph-out (ramp from current speed back to 1.0)
    void morphOut(double beats = 2.0) noexcept {
        pending_msg_.store(pack(beats, MorphDir::Out), std::memory_order_release);
        pending_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    // Called per-sample from the audio thread. Returns the current speed value.
    [[nodiscard]] double tick() noexcept {
        // Check for new morph trigger (lock-free)
        if (pending_.load(std::memory_order_acquire)) {
            pending_.store(false, std::memory_order_release);
            double beats; MorphDir dir;
            unpack(pending_msg_.load(std::memory_order_acquire), beats, dir);
            const double beat_samps = sr_ * 60.0 / bpm_;
            ramp_len_    = static_cast<uint32_t>(beat_samps * beats);
            ramp_len_    = std::max(ramp_len_, uint32_t{1});
            ramp_pos_    = 0;
            ramp_dir_    = dir;
            start_speed_ = current_speed_;
            end_speed_   = (dir == MorphDir::In) ? target_speed_ : 1.0;
            active_      = true;
        }

        if (!active_) return current_speed_;

        // Linear ramp between start and end speed
        const double t = static_cast<double>(ramp_pos_) / static_cast<double>(ramp_len_);
        // Smoothstep for perceptually linear tempo change
        const double s = t * t * (3.0 - 2.0 * t);
        current_speed_ = start_speed_ + (end_speed_ - start_speed_) * s;

        ++ramp_pos_;
        if (ramp_pos_ >= ramp_len_) {
            current_speed_ = end_speed_;
            active_ = false;
        }

        return current_speed_;
    }

private:
    double   sr_           = 44100.0;
    double   bpm_          = 120.0;
    double   target_speed_ = 0.5;
    double   current_speed_= 1.0;
    double   start_speed_  = 1.0;
    double   end_speed_    = 0.5;
    uint32_t ramp_len_     = 0;
    uint32_t ramp_pos_     = 0;
    MorphDir ramp_dir_     = MorphDir::In;
    bool     active_       = false;

    // Cross-thread trigger — packed into one atomic to avoid torn reads.
    struct PendingMsg {
        uint32_t beats_x1000;
        uint32_t direction;
    };

    std::atomic<uint64_t> pending_msg_{0};
    std::atomic<bool>     pending_{false};

    static uint64_t pack(double beats, MorphDir dir) noexcept {
        const uint32_t b = static_cast<uint32_t>(beats * 1000.0);
        const uint32_t d = (dir == MorphDir::Out) ? 1u : 0u;
        return (static_cast<uint64_t>(b) << 32) | d;
    }

    static void unpack(uint64_t v, double& beats, MorphDir& dir) noexcept {
        beats = static_cast<double>(v >> 32) / 1000.0;
        dir   = (v & 0xFFFFFFFF) ? MorphDir::Out : MorphDir::In;
    }
};
