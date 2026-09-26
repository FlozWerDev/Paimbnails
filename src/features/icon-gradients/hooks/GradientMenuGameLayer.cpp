// Stashes the menu doll, repaints on init/reset (doll-replacer and Known Players
// workarounds), after zilko's "Icon Gradients" (independent implementation; idea credit zilko144, unlicensed).

#include "GradientMenuGameLayer.hpp"
#include "GradientPlayerObject.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

bool GradientMenuGameLayer::init() {
    if (!MenuGameLayer::init()) return false;

    m_fields->m_menuPlayer = m_playerObject;

    updateGradient();

    return true;
}

void GradientMenuGameLayer::resetPlayer() {
    MenuGameLayer::resetPlayer();

    updateGradient();
}

void GradientMenuGameLayer::updateGradient() {
    if (!moduleEnabled()
        || !GradientCache::isMenuGradientsEnabled()
        || !m_playerObject
        || !m_fields->m_menuPlayer
        || Loader::get()->isModLoaded("iandyhd3.known_players")) return;

    auto player = static_cast<GradientPlayerObject*>(m_fields->m_menuPlayer);
    auto f = player->m_fields.self();

    // The doll-replacement mod swaps the menu sprites, so reveal the
    // shaded copies while painting and put the doll's own back after.
    auto showCopies = [&](bool show) {
        if (f->m_iconSprite) f->m_iconSprite->setVisible(show);
        if (f->m_iconSpriteSecondary) f->m_iconSpriteSecondary->setVisible(show);
        if (f->m_iconGlow) f->m_iconGlow->setVisible(show);
    };

    IconType type = player->getIconType();
    Gradient gradient = GradientUtils::getGradient(type, false);

    if (f->m_menuDollPatchLoaded) showCopies(true);

    player->updateAnimSprite(type, gradient, f);
    player->updateVehicleSprite(gradient, f);
    player->updateIconSprite(GradientUtils::getGradient(IconType::Cube, false), f);

    if (!f->m_menuDollPatchLoaded) return;

    showCopies(false);

    // The doll's own sprites go back on top, fully opaque.
    for (CCSprite* spr : {player->m_iconSprite, player->m_iconSpriteSecondary}) {
        spr->setVisible(true);
        spr->setOpacity(255);
    }
}
