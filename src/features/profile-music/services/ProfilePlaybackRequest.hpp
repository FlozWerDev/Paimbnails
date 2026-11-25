#pragma once

#include <cstdint>

namespace paimon::profile_music {

class ProfilePlaybackRequest {
public:
    struct Token {
        std::uint64_t generation = 0;
        std::uint32_t session = 0;
        int accountID = 0;
        bool operator==(Token const&) const = default;
    };

    Token begin(int accountID, std::uint32_t session) {
        m_token = {++m_generation, session, accountID};
        m_active = true;
        return m_token;
    }

    void cancel() { m_active = false; }

    bool current(Token token, std::uint32_t session, bool profileOpen) const {
        return m_active && profileOpen && token == m_token && token.session == session;
    }

private:
    std::uint64_t m_generation = 0;
    Token m_token;
    bool m_active = false;
};

} // namespace paimon::profile_music
