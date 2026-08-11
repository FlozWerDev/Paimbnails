#include "GradientGarageLayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"
#include "../ui/GradientLayer.hpp"
#include "../../garage-hub/GarageButtonHub.hpp"
#include "../../../utils/Localization.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientGarageLayer::onGradient(CCObject*) {
    GradientLayer::create()->show();
}

void GradientGarageLayer::onSwap(CCObject* sender) {
    if (!m_fields->m_originalCallback) return;

    (this->*m_fields->m_originalCallback)(sender);

    updateGradient();
}

std::vector<SimplePlayer*> GradientGarageLayer::getPageIcons() {
    std::vector<SimplePlayer*> ret;

    if (!paimon::modules::isEnabled("paimbnails.paimonicons.global")) return ret;

    if (CCMenu* menu = static_cast<CCNode*>(m_iconSelection->m_pages->firstObject())->getChildByType<CCMenu>(0)) {
        for (CCNode* node : menu->getChildrenExt()) {
            if (GJItemIcon* item = node->getChildByType<GJItemIcon>(0)) {
                if (SimplePlayer* icon = item->getChildByType<SimplePlayer>(0)) {
                    ret.push_back(icon);
                }
            }
        }
    }

    return ret;
}

IconType GradientGarageLayer::getType() {
    auto f = m_fields.self();

    if (!f->m_pageIcon) return IconType::Cube;

    return GradientUtils::getIconType(f->m_pageIcon);
}

void GradientGarageLayer::updatePageIcons() {
    bool p2 = sdiSaved<bool>("2pselected", false);

    if (!p2 || !GradientCache::is2PDisabled()) {
        IconType type = getPageIcons().empty() ? IconType::Cube : GradientUtils::getIconType(getPageIcons().front());
        Gradient gradient = GradientUtils::getGradient(type, p2);

        for (SimplePlayer* icon : getPageIcons()) {
            GradientUtils::applyGradient(icon, gradient, false, p2, 66);
        }
    }
}

void GradientGarageLayer::updateGradient() {
    auto f = m_fields.self();

    if (!moduleEnabled()) {
        if (!m_playerObject || f->m_isDisabled) return;

        f->m_isDisabled = true;

        Gradient emptyGradient = {};

        GradientUtils::applyGradient(m_playerObject, emptyGradient, false, false, 0);

        if (sdiEnabled()) {
            if (SimplePlayer* icon = typeinfo_cast<SimplePlayer*>(getChildByID("player2-icon"))) {
                GradientUtils::applyGradient(icon, emptyGradient, false, false, 0);
            }
        }

        for (SimplePlayer* icon : getPageIcons()) {
            GradientUtils::applyGradient(icon, emptyGradient, false, false, 0);
        }

        return;
    }

    if (f->m_isP2Separate != GradientCache::is2PSeparate()) {
        f->m_isP2Separate = GradientCache::is2PSeparate();

        updatePageIcons();
    }

    if (GradientCache::is2PDisabled()) {
        f->m_isP2Disabled = true;

        if (sdiEnabled()) {
            Gradient emptyGradient = {};

            if (SimplePlayer* icon = typeinfo_cast<SimplePlayer*>(getChildByID("player2-icon"))) {
                GradientUtils::applyGradient(icon, emptyGradient, false, false, 0);
            }

            if (sdiSaved<bool>("2pselected", false)) {
                for (SimplePlayer* icon : getPageIcons()) {
                    GradientUtils::applyGradient(icon, emptyGradient, false, false, 0);
                }
            }
        }
    }

    if (f->m_isP2Disabled && !GradientCache::is2PDisabled()) {
        f->m_isP2Disabled = false;

        if (sdiEnabled() && sdiSaved<bool>("2pselected", false)) {
            IconType type = getPageIcons().empty() ? IconType::Cube : GradientUtils::getIconType(getPageIcons().front());
            Gradient gradient = GradientUtils::getGradient(type, true);

            for (SimplePlayer* icon : getPageIcons()) {
                GradientUtils::applyGradient(icon, gradient, false, true, 66);
            }
        }
    }

    if (f->m_isDisabled) {
        f->m_isDisabled = false;

        updatePageIcons();
    }

    Loader::get()->queueInMainThread([self = Ref(this)] {
        if (!self->m_playerObject) return;

        bool p2 = sdiSaved<bool>("2pselected", false);

        GradientUtils::applyGradient(self->m_playerObject, GradientUtils::getGradient(GradientUtils::getIconType(self->m_playerObject), false), false, false, 201);

        if (sdiEnabled() && !GradientCache::is2PDisabled()) {
            if (SimplePlayer* icon = typeinfo_cast<SimplePlayer*>(self->getChildByID("player2-icon"))) {
                Gradient gradient = GradientUtils::getGradient(GradientUtils::getIconType(icon), true);
                GradientUtils::applyGradient(icon, gradient, false, true, 202);
            }
        }

        if ((!p2 || !GradientCache::is2PDisabled()) && paimon::modules::isEnabled("paimbnails.paimonicons.global")) {
            auto f = self->m_fields.self();

            if (f->m_pageIcon) {
                Gradient gradient = GradientUtils::getGradient(GradientUtils::getIconType(f->m_pageIcon), p2);
                GradientConfig emptyConfig = {};

                if (
                    f->m_wasEmptied
                    || gradient.main.isEmpty(ColorType::Main, p2)
                    || gradient.secondary.isEmpty(ColorType::Secondary, p2)
                    || gradient.glow.isEmpty(ColorType::Glow, p2)
                    || gradient.white.isEmpty(ColorType::White, p2)
                    || gradient.line.isEmpty(ColorType::Line, p2)
                ) {
                    f->m_wasEmptied = gradient.main.isEmpty(ColorType::Main, p2)
                        || gradient.secondary.isEmpty(ColorType::Secondary, p2)
                        || gradient.glow.isEmpty(ColorType::Glow, p2)
                        || gradient.white.isEmpty(ColorType::White, p2)
                        || gradient.line.isEmpty(ColorType::Line, p2);

                    for (SimplePlayer* icon : self->getPageIcons()) {
                        if (CCSprite* spr = icon->getChildByType<CCSprite>(0)) {
                            if (spr->getOpacity() > 120) {
                                GradientUtils::applyGradient(icon, gradient, false, p2, 66);
                            }
                        }
                    }
                }

                if (CCSprite* spr = f->m_pageIcon->getChildByType<CCSprite>(0)) {
                    if (spr->getOpacity() > 120) {
                        GradientUtils::applyGradient(f->m_pageIcon, gradient, false, p2, 66);
                    }
                }
            }
        }
    });
}

bool GradientGarageLayer::init() {
    if (!GJGarageLayer::init()) return false;

    m_fields->m_isP2Separate = GradientCache::is2PSeparate();

    updateGradient();

    Loader::get()->queueInMainThread([self = Ref(this)] {
        CircleButtonSprite* spr = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_paintBtn_001.png", 0.87f, CircleBaseColor::Gray, CircleBaseSize::Small
        );
        spr->getTopNode()->setRotation(8);

        CCMenuItemSpriteExtra* btn = CCMenuItemSpriteExtra::create(spr, self, menu_selector(GradientGarageLayer::onGradient));
        btn->setID("gradient-button"_spr);

        paimon::garage_hub::addButton(
            self, btn, Localization::get().getString("garage-hub.gradients"), 30);

        if (sdiEnabled()) {
            // El boton de swap tambien vive en el hub, y se cuelga en el init de
            // separate-dual, o sea antes que esta pasada diferida.
            if (CCNode* menu = paimon::garage_hub::rail(self)) {
                if (CCNode* buttonNode = menu->getChildByID("swap-2p-button")) {
                    CCMenuItemSpriteExtra* button = static_cast<CCMenuItemSpriteExtra*>(buttonNode);

                    self->m_fields->m_originalCallback = button->m_pfnSelector;
                    button->m_pfnSelector = menu_selector(GradientGarageLayer::onSwap);
                }
            }
        }
    });

    return true;
}

void GradientGarageLayer::onSelect(CCObject* sender) {
    GJGarageLayer::onSelect(sender);
    updateGradient();
}

void GradientGarageLayer::setupPage(int p0, IconType p1) {
    GJGarageLayer::setupPage(p0, p1);

    Loader::get()->queueInMainThread([self = Ref(this)] {
        if (CCMenu* menu = static_cast<CCNode*>(self->m_iconSelection->m_pages->firstObject())->getChildByType<CCMenu>(0)) {
            for (CCNode* node : menu->getChildrenExt()) {
                if (GJItemIcon* item = node->getChildByType<GJItemIcon>(0)) {
                    if (SimplePlayer* icon = item->getChildByType<SimplePlayer>(0)) {
                        self->m_fields->m_pageIcon = icon;
                        break;
                    }
                }
            }
        }

        self->updateGradient();
    });
}
