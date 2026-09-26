#pragma once

// Read-only view of the Discord bot's request queue; not twitch-requests (per-stream live room).

#include <Geode/DefaultInclude.hpp>
#include <string>

namespace paimon::thumbreq {

constexpr char const* kModuleId = "paimbnails.thumbrequests.social";

enum class Status : int { Pending, Sent, Rejected };

struct Request {
    std::string id;
    int levelId = 0;
    std::string levelName;
    std::string mode;         // "classic" or "platformer"
    std::string difficulty;   // what the requester declared
    std::string video;
    std::string requester;
    Status status = Status::Pending;
    std::string sentDifficulty;  // what the team decided, Sent only
    int sentTier = 0;            // 0 star rate, 1 featured, 2 epic, 3 legendary, 4 mythic

    // Card difficulty: team's call once decided, requester's ask until then.
    std::string const& shownDifficulty() const {
        return status == Status::Sent && !sentDifficulty.empty() ? sentDifficulty : difficulty;
    }
};

// Same table as thumb-alerts and the bot's DIFF_FILE_MAP: all three must agree.
int difficultyFace(std::string const& name);

} // namespace paimon::thumbreq
