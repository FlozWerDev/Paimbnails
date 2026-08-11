#include "GradientCharacterColorPage.hpp"
#include "GradientGarageLayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientCharacterColorPage::updateGradient() {
    if (!moduleEnabled()) return;

    bool p2Selected = sdiSaved<bool>("2pselected", false);

    Loader::get()->queueInMainThread([self = Ref(this), p2Selected] {
        CCArrayExt<SimplePlayer*> array = CCArrayExt<SimplePlayer*>(self->m_playerObjects);

        for (int i = 0; i < array.size(); i++) {
            IconType type = static_cast<IconType>(i);

            if (type == IconType::Ship) {
                if (!self->m_fields->m_isShip) {
                    type = IconType::Jetpack;
                }
            }

            Gradient gradient = GradientUtils::getGradient(type, p2Selected);
            GradientUtils::applyGradient(array[i], gradient, false, p2Selected, 372);
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
    GradientGarageLayer* garage = static_cast<GradientGarageLayer*>(getParent());

    CharacterColorPage::onClose(sender);

    garage->updateGradient();
}

void GradientCharacterColorPage::keyBackClicked() {
    GradientGarageLayer* garage = static_cast<GradientGarageLayer*>(getParent());

    CharacterColorPage::keyBackClicked();

    garage->updateGradient();
}
