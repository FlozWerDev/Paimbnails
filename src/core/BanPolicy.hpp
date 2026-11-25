#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace paimon::ban {

inline constexpr std::string_view kVerificationEndpoint = "https://api.flozwer.org/api/banned?";

inline bool isVerificationRequest(std::string_view method, std::string_view url) {
    return method == "GET" && url.starts_with(kVerificationEndpoint);
}

struct AccountIdentity {
    std::int32_t accountID = 0;
    std::string username;
    // GD player user ID. It is separate from accountID and must agree with the
    // account manager during a session.
    std::int32_t userID = 0;

    bool operator==(AccountIdentity const&) const = default;
    // userID is optional metadata: include it when GD exposes it, but do not
    // deny online access on builds where it is zero.
    bool isValid() const { return accountID > 0 && !username.empty(); }
};

// account names are case-insensitive ascii identifiers. an empty name means
// the caller could not read it, so it cannot prove a mismatch on its own.
inline bool sameAccountName(std::string_view first, std::string_view second) {
    if (first.empty() || second.empty()) return true;
    if (first.size() != second.size()) return false;
    for (std::size_t i = 0; i < first.size(); ++i) {
        auto const lower = [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
        };
        if (lower(static_cast<unsigned char>(first[i])) != lower(static_cast<unsigned char>(second[i]))) {
            return false;
        }
    }
    return true;
}

inline bool preferCachedVerdict(bool candidateBanned, std::int64_t candidateTime,
                               bool selectedBanned, std::int64_t selectedTime) {
    return candidateBanned != selectedBanned ? candidateBanned : candidateTime > selectedTime;
}

// only a live server verdict grants access; persisted allow records never do.
// the caller serializes access; all times here use the monotonic clock.
class Session {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kAuthorizationLifetime = std::chrono::seconds(120);

    std::uint64_t authorization(AccountIdentity const& current, Clock::time_point now) const {
        if (m_banned || !current.isValid() || current != m_verifiedAccount || !m_verifiedAt ||
            now < *m_verifiedAt || now - *m_verifiedAt >= kAuthorizationLifetime) {
            return 0;
        }
        return m_generation;
    }

    bool recordVerdict(AccountIdentity const& requested, AccountIdentity const& current,
                       std::optional<bool> banned, Clock::time_point now) {
        if (!requested.isValid() || requested != current) return false;
        if (banned && !*banned && !m_banned && authorization(current, now)) {
            m_verifiedAt = now;
            return true;
        }
        invalidate();
        if (!banned) return true;
        if (*banned) {
            markBanned();
        } else if (!m_banned) {
            m_verifiedAccount = requested;
            m_verifiedAt = now;
        }
        return true;
    }

    void invalidate() {
        ++m_generation;
        m_verifiedAt.reset();
    }

    // A known ban stays latched for the session, including across account changes.
    // A server unban updates the disk records and takes effect on the next launch.
    void markBanned() {
        m_banned = true;
        invalidate();
    }

private:
    bool m_banned = false;
    AccountIdentity m_verifiedAccount;
    std::optional<Clock::time_point> m_verifiedAt;
    std::uint64_t m_generation = 0;
};

} // namespace paimon::ban
