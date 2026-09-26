#pragma once

#include "../data/VersusTypes.hpp"
#include "VersusStore.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::versus {

struct LevelOffer {
    int levelId = 0;
    std::string name;
    std::string author;
    int difficulty = 0;
    int length = 0;
    bool banned = false;
};

struct SeasonInfo {
    int number = 0;
    int daysLeft = 0;
    std::vector<std::string> mutators;
};

struct MatchInfo {
    std::string id;
    // straight from the server, so the client can react to a match that ended
    // without it: dodge, void, walked rival.
    std::string serverPhase;
    PlayerRef rival;
    Mode mode = Mode::Classic;
    Format format = Format::Race;
    int levelId = 0;
    uint64_t seed = 0;
    int countdownMs = 0;
    bool catchUp = true;
    // the server decides this, not the format: friendlies are always unranked.
    bool ranked = true;
    std::vector<std::string> mutators;
    std::vector<LevelOffer> offers;
};

// either half of a friendly: a code to pass around when nobody was named,
// a match id when the duel is already open.
struct ChallengeResult {
    std::string code;
    std::string matchId;
};

struct QueueTicket {
    std::string id;
    int waiting = 0;
    int estimateSeconds = 0;
};

struct LeaderboardRow {
    int rank = 0;
    int accountId = 0;
    std::string name;
    int elo = 0;
    int wins = 0;
    int losses = 0;
};

// json field readers shared with the store.
int64_t intField(matjson::Value const& v, char const* key, int64_t fallback = 0);
std::string stringField(matjson::Value const& v, char const* key);
bool boolField(matjson::Value const& v, char const* key, bool fallback = false);

class VersusClient {
public:
    using OkCallback     = geode::CopyableFunction<void(bool ok, std::string const& message)>;
    using AuthCallback   = geode::CopyableFunction<void(bool ok, std::string const& message)>;
    using QueueCallback  = geode::CopyableFunction<void(bool ok, QueueTicket const& ticket)>;
    using MatchCallback  = geode::CopyableFunction<void(bool ok, MatchInfo const& match)>;
    using BoardCallback  = geode::CopyableFunction<void(bool ok, std::vector<LeaderboardRow> const& rows)>;
    using ChallengeCallback = geode::CopyableFunction<void(bool ok, ChallengeResult const& result,
                                                           std::string const& message)>;
    // only the local rank belongs in the store; profile visits must not overwrite it.
    using ProfileCallback = geode::CopyableFunction<void(bool ok, ModeProfile const& classic,
                                                         ModeProfile const& platformer)>;

    static VersusClient& get();

    std::string baseUrl() const;
    bool authenticated() const;
    SeasonInfo const& season() const { return m_season; }

    // trades the mod-code for a session token and fills both mode profiles.
    void authenticate(AuthCallback cb);

    void joinQueue(Mode mode, Format format, QueueCallback cb);
    void leaveQueue(OkCallback cb);

    // one lobby poll; the server answers at once, so a dropped connection
    // costs one tick, not the match.
    void pollLobby(MatchCallback cb);

    void acceptMatch(std::string const& matchId, bool accept, OkCallback cb);
    void banLevel(std::string const& matchId, int levelId, MatchCallback cb);
    void reportReady(std::string const& matchId, MatchCallback cb);
    void submitResult(std::string const& matchId, SideState const& own,
                      SideState const& rival, Outcome outcome, OkCallback cb);
    void forfeit(std::string const& matchId, OkCallback cb);

    // empty target asks for a shareable code; six chars join the invite
    // behind it; anything else reads as a username.
    void challenge(std::string const& target, Mode mode, Format format, ChallengeCallback cb);

    void fetchProfile(int accountId, ProfileCallback cb);
    void fetchLeaderboard(Mode mode, std::string const& scope, BoardCallback cb);
    void reportPlayer(std::string const& matchId, std::string const& note, OkCallback cb);

private:
    VersusClient() = default;

    struct ProfileCacheEntry {
        ModeProfile classic;
        ModeProfile platformer;
        int64_t fetchedAt = 0;
    };

    // both chips want the same numbers on profile open; without this every
    // visit costs two identical requests.
    static constexpr int64_t kProfileTtlSeconds = 60;
    std::unordered_map<int, ProfileCacheEntry> m_profileCache;
    std::unordered_map<int, std::vector<ProfileCallback>> m_profileWaiters;

    // the server may drop a session; allowRetry stops recursive re-auth on a rejected token.
    void send(std::string const& method, std::string const& path,
              matjson::Value const& body,
              geode::CopyableFunction<void(bool ok, matjson::Value const& json,
                                           std::string const& message)> cb,
              bool allowRetry = true);

    static MatchInfo parseMatch(matjson::Value const& v);

    std::string m_token;
    SeasonInfo m_season;
    bool m_authenticated = false;
};

} // namespace paimon::versus
