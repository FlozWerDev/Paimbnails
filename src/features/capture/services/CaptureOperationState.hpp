#pragma once

#include <cstdint>

namespace paimon::capture {

// main-thread ownership of a capture, including its delay and queued callbacks.
class CaptureOperationState {
public:
    using Token = std::uint64_t;

    Token begin() {
        if (m_active) return 0;
        m_active = true;
        return ++m_generation;
    }

    bool active() const { return m_active; }
    Token token() const { return m_active ? m_generation : 0; }
    bool owns(Token token) const { return m_active && token != 0 && token == m_generation; }

    bool finish(Token token) {
        if (!owns(token)) return false;
        m_active = false;
        return true;
    }

    bool cancel() {
        bool const owned = m_active;
        m_active = false;
        ++m_generation;
        return owned;
    }

private:
    Token m_generation = 0;
    bool m_active = false;
};

}
