// Own-icon shading on level score cells, rethought for Paimbnails.
//
// Idea credit: "Icon Gradients" by zilko
// (https://github.com/zilko144/icon-gradients-geode, unlicensed —
// all rights reserved). Independent implementation: same behavior (shade
// the player's own icon on score cells), own expression.

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
