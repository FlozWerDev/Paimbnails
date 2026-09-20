// Color-page icon shading, rethought for Paimbnails.
//
// Idea credit: "Icon Gradients" by zilko
// (https://github.com/zilko144/icon-gradients-geode, unlicensed —
// all rights reserved). Independent implementation: same behavior (shade
// each doll on the color page by its slot index, with the Ship slot
// doubling as Jetpack), own expression.

#include "GradientCharacterColorPage.hpp"
#include "GradientGarageLayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientCharacterColorPage::updateGradient() {
    if (!moduleEnabled()) return;

    bool p2 = sdiSaved<bool>("2pselected", false);

    Loader::get()->queueInMainThread([self = Ref(this), p2] {
        CCArrayExt<SimplePlayer*> dolls = CCArrayExt<SimplePlayer*>(self->m_playerObjects);

        for (int i = 0; i < dolls.size(); i++) {
            // Slot 1 is the Ship doll, or the Jetpack one when toggled.
            IconType kind = (i == 1 && !self->m_fields->m_isShip)
                ? IconType::Jetpack
                : static_cast<IconType>(i);
            GradientUtils::applyGradient(dolls[i], GradientUtils::getGradient(kind, p2), false, p2, 372);
        }
    });
}

bool GradientCharacterColorPage::init() {
    if (!CharacterColorPage::init()) return false;

    updateGradient();

    return true;
}

void GradientCharacterColorPage::toggleShip(CCObject* p0) {
    CharacterColorPage::toggleShip(p0);

    m_fields->m_isShip = !m_fields->m_isShip;

    updateGradient();
}

void GradientCharacterColorPage::onPlayerColor(CCObject* sender) {
    CharacterColorPage::onPlayerColor(sender);

    updateGradient();
}

void GradientCharacterColorPage::onClose(CCObject* sender) {
    auto garage = static_cast<GradientGarageLayer*>(getParent());

    CharacterColorPage::onClose(sender);

    garage->updateGradient();
}

void GradientCharacterColorPage::keyBackClicked() {
    auto garage = static_cast<GradientGarageLayer*>(getParent());

    CharacterColorPage::keyBackClicked();

    garage->updateGradient();
}
