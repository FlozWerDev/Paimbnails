#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace paimon::frameperf {

// main-thread-only counters. No allocation, logging or filesystem work in a
// measured scope. Nested stages are inclusive and must not be summed blindly.
enum class Stage : unsigned {
    Scheduler, Capture, RTX, Cursor, Present, CaptureCallbacks, PopupBlur, Count
};
inline std::array<int64_t, static_cast<unsigned>(Stage::Count)> times{};
inline bool active = false;

inline void beginFrame() {
    times.fill(0);
    active = true;
}

inline int64_t time(Stage stage) { return times[static_cast<unsigned>(stage)]; }

class Scope {
    Stage m_stage;
    bool m_active;
    std::chrono::steady_clock::time_point m_start;
public:
    explicit Scope(Stage stage) : m_stage(stage), m_active(active),
        m_start(m_active ? std::chrono::steady_clock::now()
                        : std::chrono::steady_clock::time_point{}) {}
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
    ~Scope() {
        if (m_active) times[static_cast<unsigned>(m_stage)] +=
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - m_start).count();
    }
};

} // namespace paimon::frameperf
