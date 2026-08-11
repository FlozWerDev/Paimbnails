#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

class $modify(GradientCommentCell, CommentCell) {
    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);

        if (!moduleEnabled() || m_accountComment || !comment) return;
        if (comment->m_accountID != GJAccountManager::get()->m_accountID) return;

        if (CCNode* menu = m_mainLayer->getChildByID("main-menu")) {
            if (CCNode* userMenu = menu->getChildByID("user-menu")) {
                if (SimplePlayer* icon = typeinfo_cast<SimplePlayer*>(userMenu->getChildByID("player-icon"))) {
                    IconType type = GradientUtils::getIconType(icon);

                    Gradient gradient = GradientUtils::getGradient(type, false);

                    GradientUtils::applyGradient(icon, gradient, false, false, 2);
                }
            }
        }
    }
};
