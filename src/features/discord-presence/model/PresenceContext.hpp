#pragma once

#include "PresencePayload.hpp"
#include "PresenceCustomization.hpp"

namespace paimon::discord {
struct PresenceContext {
    PresencePayload automatic;
    PresenceTokens tokens;
    std::string category = "menu";
    std::string activityKey = "menu";
    std::string levelKey;
    std::string attemptKey;
    int partySize = 0;
    int partyMax = 0;
};
}
