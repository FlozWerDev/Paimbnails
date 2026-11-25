// shades each own-profile slot by index and refreshes on the 1p/2p toggle.
// independent implementation inspired by zilko's "icon gradients".

#include "GradientProfilePage.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"
#include "../../global-icon/GlobalIconGradients.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

// GD 2.2091 displays ship and jetpack in separate slots.
static IconType slotKind(int slot) {
    return static_cast<IconType>(slot);
}

void GradientProfilePage::onSwap(CCObject* sender) {
    (this->*m_fields->m_originalCallback)(sender);

    m_fields->m_isSecondPlayer = !m_fields->m_isSecondPlayer;

    Loader::get()->queueInMainThread([self = Ref(this)] { self->updateGradient(); });
}

void GradientProfilePage::updateGradient() {
    if (!moduleEnabled()) return;
    // other players: the gradients they linked to their global icon, if any.
    if (!m_ownProfile) {
        paimon::globalicon::renderProfileGradients(this);
        return;
    }

    CCNode* menu = m_mainLayer->getChildByID("player-menu");
    if (!menu) return;

    bool p2 = m_fields->m_isSecondPlayer;

    int slot = 0;
    for (CCNode* entry : menu->getChildrenExt<CCNode*>()) {
        if (SimplePlayer* doll = entry->getChildByType<SimplePlayer>(0))
            GradientUtils::applyGradient(doll, GradientUtils::getGradient(slotKind(slot), p2), false, p2, 99);
        slot++;
    }
}

void GradientProfilePage::getUserInfoFinished(GJUserScore* p0) {
    ProfilePage::getUserInfoFinished(p0);

    updateGradient();

    Loader::get()->queueInMainThread([self = Ref(this)] {
        if (!sdiEnabled()) return;
        CCNode* menu = self->m_mainLayer->getChildByID("left-menu");
        CCNode* node = menu ? menu->getChildByID("2p-toggler"_spr) : nullptr;
        if (!node) return;
        auto toggle = static_cast<CCMenuItemToggler*>(node);
        self->m_fields->m_originalCallback = toggle->m_pfnSelector;
        toggle->m_pfnSelector = menu_selector(GradientProfilePage::onSwap);
    });
}

