#pragma once

// Read-only view of the Discord bot's request queue (/api/requests/list); not
// to be confused with twitch-requests, the per-stream live room.

#include <Geode/DefaultInclude.hpp>
#include <string>

namespace paimon::thumbreq {

constexpr char const* kModuleId = "paimbnails.thumbrequests.social";

enum class Status : int { Pending, Sent, Rejected };

struct Request {
    std::string id;
    int levelId = 0;
    std::string levelName;
    std::string mode;         // "classic" o "platformer"
    std::string difficulty;   // la que declaro quien pidio
    std::string video;
    std::string requester;
    Status status = Status::Pending;
    std::string sentDifficulty;  // la que decidio el equipo, solo si Sent
    int sentTier = 0;            // 0 star rate, 1 featured, 2 epic, 3 legendary, 4 mythic

    // The difficulty the card should draw: what the team decided once they
    // decided, and what the requester asked for until then.
    std::string const& shownDifficulty() const {
        return status == Status::Sent && !sentDifficulty.empty() ? sentDifficulty : difficulty;
    }
};

// Same table as thumb-alerts and the bot's DIFF_FILE_MAP: all three must agree
// or the face is wrong.
int difficultyFace(std::string const& name);

} // namespace paimon::thumbreq
