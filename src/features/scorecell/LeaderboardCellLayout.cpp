#include "LeaderboardCellLayout.hpp"
#include "LeaderboardLayoutSettings.hpp"
#include "../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>

#include <array>

using namespace geode::prelude;

namespace paimon::scorecell {

namespace {

struct StatNodes {
    LeaderboardModule module;
    char const* labelID;
    char const* iconID;
};

constexpr std::array kStatNodes = {
    StatNodes{LeaderboardModule::Stars, "stars-label", "stars-icon"},
    StatNodes{LeaderboardModule::Moons, "moons-label", "moons-icon"},
    StatNodes{LeaderboardModule::Diamonds, "diamonds-label", "diamonds-icon"},
    StatNodes{LeaderboardModule::SecretCoins, "coins-label", "coins-icon"},
    StatNodes{LeaderboardModule::UserCoins, "user-coins-label", "user-coins-icon"},
    StatNodes{LeaderboardModule::Demons, "demons-label", "demons-icon"},
    StatNodes{LeaderboardModule::CreatorPoints, "creator-points-label", "creator-points-icon"},
};

CCNode* findNode(CCNode* root, char const* id) {
    return root ? root->getChildByIDRecursive(id) : nullptr;
}

void setVisible(CCNode* root, char const* id, bool visible) {
    if (auto node = findNode(root, id)) {
        node->setVisible(visible);
    }
}

LeaderboardModule selectedStat(GJUserScore* score) {
    if (!score) return LeaderboardModule::Stars;
    switch (score->m_leaderboardStat) {
        case LeaderboardStat::Moons: return LeaderboardModule::Moons;
        case LeaderboardStat::Demons: return LeaderboardModule::Demons;
        case LeaderboardStat::UserCoins: return LeaderboardModule::UserCoins;
        default: return LeaderboardModule::Stars;
    }
}

} // namespace

void applyLeaderboardLayout(GJScoreCell* cell) {
    if (!cell || !cell->m_mainLayer || !cell->m_score) return;
    if (!paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) return;

    auto mainLayer = cell->m_mainLayer;
    setVisible(mainLayer, "rank-label", moduleEnabled(LeaderboardModule::Rank));
    setVisible(mainLayer, "player-icon", moduleEnabled(LeaderboardModule::PlayerIcon));
    setVisible(mainLayer, "player-name", moduleEnabled(LeaderboardModule::PlayerName));

    auto activeStat = selectedStat(cell->m_score);
    for (auto const& entry : kStatNodes) {
        bool visible = moduleEnabled(entry.module) || entry.module == activeStat;
        setVisible(mainLayer, entry.labelID, visible);
        setVisible(mainLayer, entry.iconID, visible);
    }

    if (auto statsMenu = findNode(mainLayer, "stats-menu")) {
        statsMenu->updateLayout();
    }
}

} // namespace paimon::scorecell
