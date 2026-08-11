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

        if (SimplePlayer* icon = m_mainLayer->getChildByType<SimplePlayer>(0)) {
            IconType type = GradientUtils::getIconType(icon);

            Gradient gradient = GradientUtils::getGradient(type, false);

            GradientUtils::applyGradient(icon, gradient, false, false, 2);
        }
    }
};
