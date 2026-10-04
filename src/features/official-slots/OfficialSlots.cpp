#include "OfficialSlots.hpp"
#include "../../core/MainLevels.hpp"

namespace paimon::officialslots {

int difficultyFace(Difficulty difficulty) {
    return static_cast<int>(difficulty);
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
    return paimon::isMainLevelID(levelId);
}

} // namespace paimon::officialslots
