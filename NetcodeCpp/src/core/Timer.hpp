#pragma once

#include <chrono>
#include <thread>

namespace netcode {

class Timer {
public:
    Timer() : start_time_(std::chrono::steady_clock::now()) {}

    // Resets the timer anchor to now
    void reset() { start_time_ = std::chrono::steady_clock::now(); }

    // Elapsed time in seconds since construction or reset
    [[nodiscard]] double elapsed_seconds() const {
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> diff = now - start_time_;
        return diff.count();
    }

    // Elapsed time in milliseconds
    [[nodiscard]] double elapsed_ms() const { return elapsed_seconds() * 1000.0; }

    // High-resolution absolute time in seconds (monotonic)
    [[nodiscard]] static double now_seconds() {
        static const auto global_epoch = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> diff = now - global_epoch;
        return diff.count();
    }

    // Sleep for a duration in seconds
    static void sleep_seconds(double seconds) {
        if (seconds <= 0.0) return;
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    }

    // Sleep for a duration in milliseconds
    static void sleep_ms(double ms) {
        if (ms <= 0.0) return;
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
    }

private:
    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace netcode
