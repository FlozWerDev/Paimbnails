// shades each own-profile slot by index (ship doubles as jetpack; re-shade on
// 1p/2p toggle), after zilko's "icon gradients" (independent implementation; idea credit zilko144, unlicensed).

#include "GradientProfilePage.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

// slot index -> icon kind. slot 1 shows the ship doll, or the jetpack
// one while the ship toggle is off.
static IconType slotKind(int slot, bool shipOn) {
    return (slot == 1 && !shipOn) ? IconType::Jetpack : static_cast<IconType>(slot);
}

void GradientProfilePage::onSwap(CCObject* sender) {
    (this->*m_fields->m_originalCallback)(sender);

    m_fields->m_isSecondPlayer = !m_fields->m_isSecondPlayer;

    Loader::get()->queueInMainThread([self = Ref(this)] { self->updateGradient(); });
}

void GradientProfilePage::updateGradient() {
    if (!m_ownProfile || !moduleEnabled()) return;

    CCNode* menu = m_mainLayer->getChildByID("player-menu");
    if (!menu) return;

    bool p2 = m_fields->m_isSecondPlayer;
    bool shipOn = m_fields->m_isShip;

    int slot = 0;
    for (CCNode* entry : menu->getChildrenExt<CCNode*>()) {
        if (SimplePlayer* doll = entry->getChildByType<SimplePlayer>(0))
            GradientUtils::applyGradient(doll, GradientUtils::getGradient(slotKind(slot, shipOn), p2), false, p2, 99);
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

void GradientProfilePage::toggleShip(CCObject* p0) {
    ProfilePage::toggleShip(p0);

    m_fields->m_isShip = !m_fields->m_isShip;

    Loader::get()->queueInMainThread([self = Ref(this)] { self->updateGradient(); });
}
