#include "GradientMenuGameLayer.hpp"
#include "GradientPlayerObject.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

bool GradientMenuGameLayer::init() {
    if (!MenuGameLayer::init()) return false;

    m_fields->m_realPlayerObject = m_playerObject;

    updateGradient();

    return true;
}

void GradientMenuGameLayer::resetPlayer() {
    MenuGameLayer::resetPlayer();

    updateGradient();
}

void GradientMenuGameLayer::updateGradient() {
    if (
        !moduleEnabled()
        || !GradientCache::isMenuGradientsEnabled()
        || !m_playerObject
        || Loader::get()->isModLoaded("iandyhd3.known_players")
    ) {
        return;
    }

    if (!m_fields->m_realPlayerObject) return;

    GradientPlayerObject* player = static_cast<GradientPlayerObject*>(m_fields->m_realPlayerObject);

    auto f = player->m_fields.self();

    IconType type = player->getIconType();
    Gradient gradient = GradientUtils::getGradient(type, false);

    if (f->m_thatOneUfoShipAndCubeModIsLoaded) {
        if (f->m_iconSprite) f->m_iconSprite->setVisible(true);
        if (f->m_iconSpriteSecondary) f->m_iconSpriteSecondary->setVisible(true);
        if (f->m_iconGlow) f->m_iconGlow->setVisible(true);
    }

    player->updateAnimSprite(type, gradient, f);
    player->updateVehicleSprite(gradient, f);
    player->updateIconSprite(GradientUtils::getGradient(IconType::Cube, false), f);

    if (f->m_thatOneUfoShipAndCubeModIsLoaded) {
        if (f->m_iconSprite) f->m_iconSprite->setVisible(false);
        if (f->m_iconSpriteSecondary) f->m_iconSpriteSecondary->setVisible(false);
        if (f->m_iconGlow) f->m_iconGlow->setVisible(false);

        player->m_iconSprite->setVisible(true);
        player->m_iconSpriteSecondary->setVisible(true);

        player->m_iconSprite->setOpacity(255);
        player->m_iconSpriteSecondary->setOpacity(255);
    }
}
