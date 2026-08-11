#include "GradientProfilePage.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientProfilePage::onSwap(CCObject* sender) {
    (this->*m_fields->m_originalCallback)(sender);

    m_fields->m_isSecondPlayer = !m_fields->m_isSecondPlayer;

    Loader::get()->queueInMainThread([self = Ref(this)] {
        self->updateGradient();
    });
}

void GradientProfilePage::updateGradient() {
    if (!m_ownProfile || !moduleEnabled()) return;

    CCNode* menu = m_mainLayer->getChildByID("player-menu");

    if (!menu) return;

    CCArrayExt<CCNode*> array = menu->getChildrenExt();

    for (int i = 0; i < array.size(); i++) {
        SimplePlayer* child = array[i]->getChildByType<SimplePlayer>(0);

        if (!child) continue;

        IconType type = static_cast<IconType>(i);

        if (type == IconType::Ship) {
            if (!m_fields->m_isShip) {
                type = IconType::Jetpack;
            }
        }

        Gradient gradient = GradientUtils::getGradient(type, m_fields->m_isSecondPlayer);

        GradientUtils::applyGradient(child, gradient, false, m_fields->m_isSecondPlayer, 99);
    }
}

void GradientProfilePage::getUserInfoFinished(GJUserScore* p0) {
    ProfilePage::getUserInfoFinished(p0);

    updateGradient();

    Loader::get()->queueInMainThread([self = Ref(this)] {
        if (sdiEnabled()) {
            if (CCNode* menu = self->m_mainLayer->getChildByID("left-menu")) {
                if (CCNode* toggleNode = menu->getChildByID("2p-toggler")) {
                    CCMenuItemToggler* toggle = static_cast<CCMenuItemToggler*>(toggleNode);

                    self->m_fields->m_originalCallback = toggle->m_pfnSelector;
                    toggle->m_pfnSelector = menu_selector(GradientProfilePage::onSwap);
                }
            }
        }
    });
}

void GradientProfilePage::toggleShip(CCObject* p0) {
    ProfilePage::toggleShip(p0);

    m_fields->m_isShip = !m_fields->m_isShip;

    Loader::get()->queueInMainThread([this] {
        updateGradient();
    });
}
