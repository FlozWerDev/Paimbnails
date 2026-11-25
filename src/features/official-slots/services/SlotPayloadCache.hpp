#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace paimon::officialslots {

// only playable source data goes on disk. Page IDs and cosmetic overrides stay
// in the slot store and are applied to a separate local level.
struct CachedSlotPayload {
    int levelId = 0;
    std::string levelString;
    int audioTrack = 0;
    int songId = 0;
    std::string songIds;
    std::string sfxIds;
    int levelLength = 0;
    std::string name;
    std::string author;
    int difficulty = 0;
    int ratings = 0;
    int ratingsSum = 0;
    int demon = 0;
    int demonDifficulty = 0;
    bool autoLevel = false;
    int stars = 0;
    int coins = 0;
    int featured = 0;
    int epic = 0;
};

std::optional<CachedSlotPayload> readSlotPayload(std::filesystem::path const& directory, int levelId);
bool writeSlotPayload(std::filesystem::path const& directory, CachedSlotPayload const& payload);

} // namespace paimon::officialslots
