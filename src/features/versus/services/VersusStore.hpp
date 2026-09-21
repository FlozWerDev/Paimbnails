#pragma once

// Local mirror of what the server knows, plus the handful of preferences the
// hub remembers. The server stays the authority; this exists so the hub can
// draw a rank before the first request answers.

#include "../data/VersusModes.hpp"
#include "../data/VersusRanks.hpp"
#include "../data/VersusTypes.hpp"

#include <string>
#include <vector>

namespace paimon::versus {

struct ModeProfile {
    int elo = kStartElo;
    int best = kStartElo;
    int wins = 0;
    int losses = 0;
    int streak = 0;
    int placementsLeft = kPlacementMatches;
    int64_t xpTotal = 0;
    bool paimon = false;
    int64_t lastPlayed = 0;
};

struct MatchRecord {
    std::string id;
    std::string rival;
    int levelId = 0;
    Mode mode = Mode::Classic;
    Format format = Format::Race;
    Outcome outcome = Outcome::Pending;
    int eloDelta = 0;
    float ownPercent = 0.f;
    float rivalPercent = 0.f;
    int64_t playedAt = 0;
};

class VersusStore {
public:
    static VersusStore& get();

    ModeProfile const& profile(Mode mode) const;
    void setProfile(Mode mode, ModeProfile const& profile);
    RankInfo rank(Mode mode) const;

    // What the two ladders have paid into the main progression bar so far.
    int64_t versusExp() const;

    std::vector<MatchRecord> const& history() const { return m_history; }
    void pushRecord(MatchRecord const& record);
    void setHistory(std::vector<MatchRecord> history);

    Mode preferredMode() const { return m_mode; }
    void setPreferredMode(Mode mode);

    // Never answers a format the queue no longer offers: the hub, the challenge
    // and beginQueue all read this and would otherwise disagree on the rules.
    Format preferredFormat(Mode mode) const;
    void setPreferredFormat(Mode mode, Format format);

    bool hudEnabled() const { return m_hud; }
    void setHudEnabled(bool enabled);

    bool friendsOnly() const { return m_friendsOnly; }
    void setFriendsOnly(bool value);

    std::string const& sessionToken() const { return m_token; }
    void setSessionToken(std::string token);

private:
    VersusStore() = default;

    void load();
    void saveProfiles();

    static constexpr size_t kHistoryKept = 20;

    ModeProfile m_classic;
    ModeProfile m_platformer;
    std::vector<MatchRecord> m_history;
    std::string m_token;
    Mode m_mode = Mode::Classic;
    Format m_classicFormat = Format::Race;
    Format m_platformerFormat = Format::Race;
    bool m_hud = true;
    bool m_friendsOnly = false;
    bool m_loaded = false;
};

} // namespace paimon::versus
