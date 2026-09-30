#pragma once

// queue filters: accepted mode, difficulty faces and lengths.
// requests that miss stay stored but out of the list.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace paimon::twitch {

enum class ModeFilter : int { All, Classic, Platformer };
constexpr int kModeFilterCount = 3;

// one bit per face/length, in draw order.
constexpr int kDifficultySlotCount = 8;
constexpr int kLengthSlotCount = 5;
constexpr uint32_t kAllDifficulties = (1u << kDifficultySlotCount) - 1;
constexpr uint32_t kAllLengths = (1u << kLengthSlotCount) - 1;

struct VideoRequirementRule {
    ModeFilter mode = ModeFilter::All;
    uint32_t difficulties = (1u << 6);
};

struct RequestFilters {
    ModeFilter mode = ModeFilter::All;
    uint32_t difficulties = kAllDifficulties;
    uint32_t lengths = kAllLengths;
    bool verifiedOnly = false;
    bool blockDuplicates = true;
    int maxPerUser = 0;
    int cooldownSeconds = 0;
    std::vector<VideoRequirementRule> videoRules;

    // nothing checked counts as everything checked.
    bool allDifficulties() const {
        uint32_t const mask = difficulties & kAllDifficulties;
        return mask == 0 || mask == kAllDifficulties;
    }
    bool allLengths() const {
        uint32_t const mask = lengths & kAllLengths;
        return mask == 0 || mask == kAllLengths;
    }
    bool hasLevelFilters() const {
        return mode != ModeFilter::All || !allDifficulties() || !allLengths() || !videoRules.empty();
    }
};

std::vector<std::string> modeFilterNames();
char const* difficultySlotName(int slot);
char const* lengthSlotName(int slot);
int difficultySlotSprite(int slot);

// one-line summary of a required-video rule.
std::string videoRuleSummary(VideoRequirementRule const& rule);

// short summary for the queue header; empty without filters.
std::string filterSummary(RequestFilters const& filters);

// difficulty uses gjdifficultysprite values: -1 auto, 0 unrated,
// 1-5 easy..insane, 6+ demon.
bool matchesFilters(
    RequestFilters const& filters,
    int difficulty,
    int length,
    bool platformer,
    bool hasVideo = true
);

// nullopt while the level is unresolved.
std::optional<bool> requestPasses(int levelID, bool hasVideo = true);

} // namespace paimon::twitch
