#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <string>

namespace paimon::editorfilters {

struct FilterState {
    // Length buckets (GJGameLevel::m_levelLength: 0..4).
    bool tiny = false;
    bool shortLen = false;
    bool medium = false;
    bool longLen = false;
    bool xl = false;

    // Verification status.
    bool verified = false;
    bool unverified = false;
    std::string songID;
};

inline FilterState& state() {
    static FilterState s;
    return s;
}

inline bool anyLengthFilter() {
    auto& f = state();
    return f.tiny || f.shortLen || f.medium || f.longLen || f.xl;
}

inline bool anyVerificationFilter() {
    auto& f = state();
    return f.verified || f.unverified;
}

inline bool anyActive() {
    return anyLengthFilter() || anyVerificationFilter() || !state().songID.empty();
}

inline void reset() {
    state() = FilterState{};
}

inline bool matches(GJGameLevel* level) {
    if (!level) return false;
    auto& f = state();

    // Length
    bool lengthOk = true;
    if (anyLengthFilter()) {
        int len = level->m_levelLength;
        lengthOk =
            (len == 0 && f.tiny)   ||
            (len == 1 && f.shortLen) ||
            (len == 2 && f.medium) ||
            (len == 3 && f.longLen)  ||
            (len == 4 && f.xl);
    }

    // Verification
    bool verifyOk = true;
    if (anyVerificationFilter()) {
        bool isVerified = level->m_isVerified;
        verifyOk = (isVerified && f.verified) || (!isVerified && f.unverified);
    }

    bool songOk = true;
    if (!f.songID.empty()) {
        songOk = std::to_string(level->m_songID) == f.songID;
    }

    return lengthOk && verifyOk && songOk;
}

} // namespace paimon::editorfilters
