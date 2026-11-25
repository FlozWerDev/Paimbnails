#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::thumbviewer {

class FocusTransition {
public:
    bool setFocused(bool focused) {
        float const target = focused ? 1.f : 0.f;
        if (target == m_target) return false;
        m_from = m_progress;
        m_target = target;
        m_elapsed = 0.f;
        m_running = m_from != m_target;
        return true;
    }

    bool advance(float dt, float duration = 0.32f) {
        if (!m_running) return false;
        if (!std::isfinite(dt) || dt < 0.f) dt = 0.f;
        m_elapsed += dt;
        float const time = duration > 0.f ? std::clamp(m_elapsed / duration, 0.f, 1.f) : 1.f;
        float const eased = time * time * (3.f - 2.f * time);
        m_progress = m_from + (m_target - m_from) * eased;
        m_running = time < 1.f;
        if (!m_running) m_progress = m_target;
        return m_running;
    }

    float progress() const { return m_progress; }
    bool running() const { return m_running; }

private:
    float m_progress = 0.f;
    float m_from = 0.f;
    float m_target = 0.f;
    float m_elapsed = 0.f;
    bool m_running = false;
};

}
