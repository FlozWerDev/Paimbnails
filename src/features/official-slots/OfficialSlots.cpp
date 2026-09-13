#include "OfficialSlots.hpp"

namespace paimon::officialslots {

int difficultyFace(Difficulty difficulty) {
    return static_cast<int>(difficulty);
}

char const* difficultyName(Difficulty difficulty) {
    switch (difficulty) {
        case Difficulty::Auto:         return "Auto";
        case Difficulty::Unrated:      return "Unrated";
        case Difficulty::Easy:         return "Easy";
        case Difficulty::Normal:       return "Normal";
        case Difficulty::Hard:         return "Hard";
        case Difficulty::Harder:       return "Harder";
        case Difficulty::Insane:       return "Insane";
        case Difficulty::Demon:        return "Demon";
        case Difficulty::EasyDemon:    return "Easy Demon";
        case Difficulty::MediumDemon:  return "Medium Demon";
        case Difficulty::InsaneDemon:  return "Insane Demon";
        case Difficulty::ExtremeDemon: return "Extreme Demon";
    }
    return "Unrated";
}

char const* tierName(Tier tier) {
    switch (tier) {
        case Tier::None:      return "Rate";
        case Tier::Featured:  return "Featured";
        case Tier::Epic:      return "Epic";
        case Tier::Legendary: return "Legendary";
        case Tier::Mythic:    return "Mythic";
    }
    return "Rate";
}

std::vector<Difficulty> const& allDifficulties() {
    static std::vector<Difficulty> const kAll = {
        Difficulty::Auto,
        Difficulty::Unrated,
        Difficulty::Easy,
        Difficulty::Normal,
        Difficulty::Hard,
        Difficulty::Harder,
        Difficulty::Insane,
        Difficulty::Demon,
        Difficulty::EasyDemon,
        Difficulty::MediumDemon,
        Difficulty::InsaneDemon,
        Difficulty::ExtremeDemon,
    };
    return kAll;
}

std::vector<Tier> const& allTiers() {
    static std::vector<Tier> const kAll = {
        Tier::None,
        Tier::Featured,
        Tier::Epic,
        Tier::Legendary,
        Tier::Mythic,
    };
    return kAll;
}

bool isOfficialId(int levelId) {
    return levelId >= 1 && levelId <= 22;
}

} // namespace paimon::officialslots
