// own-comment icon shading, after zilko's "icon gradients" (independent implementation, own expression).

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

        CCNode* menu = m_mainLayer->getChildByID("main-menu");
        CCNode* userMenu = menu ? menu->getChildByID("user-menu") : nullptr;
        auto* icon = userMenu
            ? typeinfo_cast<SimplePlayer*>(userMenu->getChildByID("player-icon"))
            : nullptr;
        if (icon) GradientUtils::paintMenuIcon(icon, false, 2);
    }
};
