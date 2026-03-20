#pragma once
#include <atomic>
#include <cstddef>
#include <optional>

// ParamQueue — lock-free SPSC (single-producer, single-consumer) ring buffer
// for pushing control parameter structs from the GUI/control thread
// to the audio thread without locks.
//
// The audio thread drains the queue at the start of each block and uses
// the latest value. Intermediate values are skipped — this is correct
// because control parameters are "most-recent-wins" by nature.

template <typename T, std::size_t N>
class ParamQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");

public:
    // Called from the control/GUI thread.
    void push(const T& value) noexcept {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        buf_[w & MASK] = value;
        write_.store(w + 1, std::memory_order_release);
    }

    // Called from the audio thread. Returns the most recent value
    // if any were pushed since the last pop, otherwise std::nullopt.
    [[nodiscard]] std::optional<T> popLatest() noexcept {
        const std::size_t w = write_.load(std::memory_order_acquire);
        const std::size_t r = read_.load(std::memory_order_relaxed);
        if (w == r) return std::nullopt;

        // Skip to latest — we only care about the most recent value
        const std::size_t latest = (w - 1) & MASK;
        T result = buf_[latest];
        read_.store(w, std::memory_order_release);
        return result;
    }

private:
    static constexpr std::size_t MASK = N - 1;
    T buf_[N] = {};
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
};
