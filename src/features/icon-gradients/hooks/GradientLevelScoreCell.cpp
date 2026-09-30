// shades the player's own icon on score cells, after zilko's "icon gradients"
// (independent implementation; idea credit zilko144, unlicensed).

#include <Geode/Geode.hpp>
#include <Geode/modify/GJLevelScoreCell.hpp>

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

class $modify(GradientLevelScoreCell, GJLevelScoreCell) {
    void loadFromScore(GJUserScore* score) {
        GJLevelScoreCell::loadFromScore(score);

        if (!moduleEnabled() || score->m_accountID != GJAccountManager::get()->m_accountID) return;

        if (SimplePlayer* icon = m_mainLayer->getChildByType<SimplePlayer>(0))
            GradientUtils::paintMenuIcon(icon, false, 2);
    }
};
