#pragma once

#include "sources/ChatSource.hpp"

namespace paimon::twitch {

struct LevelRequest {
    int64_t entryID = 0;
    int levelID = 0;
    std::string webRequestID;
    int requesterAccountID = 0;
    std::string requester;
    bool requesterVerified = false;
    std::string message;
    std::string description;
    std::string queue = "General";
    std::string command;
    std::string rewardID;
    std::string sourceName;
    std::string eventID;
    std::string userID;
    int64_t receivedAt = 0;
    bool played = false;
    int percent = 0;
    std::string videoUrl;
    Platform platform = Platform::Twitch;
};

} // namespace paimon::twitch
