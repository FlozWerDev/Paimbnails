// Garage shading orchestration, rethought for Paimbnails.
//
// Idea credit: "Icon Gradients" by zilko
// (https://github.com/zilko144/icon-gradients-geode, unlicensed —
// all rights reserved). Independent implementation: same behavior (shade
// the garage doll, the 2P doll and the page dolls; clear everything while
// the module is off), own expression.

#include "GradientGarageLayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"
#include "../ui/GradientLayer.hpp"
#include "../../garage-hub/GarageButtonHub.hpp"
#include "../../../utils/Localization.hpp"

#include <utility>

using namespace geode::prelude;
using namespace paimon::icon_gradients;

namespace {

// One gradient channel: which config slot it lives in and which color
// kind its emptiness is tested against.
struct GradientLane {
    GradientConfig Gradient::* config;
    ColorType color;
};

constexpr GradientLane kGradientLanes[] = {
    {&Gradient::main, ColorType::Main},
    {&Gradient::secondary, ColorType::Secondary},
    {&Gradient::glow, ColorType::Glow},
    {&Gradient::white, ColorType::White},
    {&Gradient::line, ColorType::Line},
};

// Wipe a doll back to its plain colors.
void clearDoll(SimplePlayer* doll) {
    GradientUtils::applyGradient(doll, Gradient{}, false, false, 0);
}

// Paint every doll in a page with the same gradient.
void paintEach(std::vector<SimplePlayer*> const& dolls, Gradient const& gradient, bool second, int tag) {
    for (SimplePlayer* doll : dolls)
        GradientUtils::applyGradient(doll, gradient, false, second, tag);
}

// Dolls hidden inside a preview (opacity-gated) must not be painted.
bool dollShown(SimplePlayer* doll) {
    CCSprite* spr = doll->getChildByType<CCSprite>(0);
    return spr && spr->getOpacity() > 120;
}

bool pageSlotDisabled(bool second) {
    return second && GradientCache::is2PDisabled();
}

} // namespace

void GradientGarageLayer::onGradient(CCObject*) {
    GradientLayer::create()->show();
}

void GradientGarageLayer::onSwap(CCObject* sender) {
    auto callback = m_fields->m_originalCallback;
    if (!callback) return;

    (this->*callback)(sender);

    updateGradient();
}

std::vector<SimplePlayer*> GradientGarageLayer::getPageIcons() {
    std::vector<SimplePlayer*> dolls;

    if (!paimon::modules::isEnabled("paimbnails.paimonicons.global")) return dolls;

    CCNode* page = static_cast<CCNode*>(m_iconSelection->m_pages->firstObject());
    CCMenu* menu = page ? page->getChildByType<CCMenu>(0) : nullptr;
    if (!menu) return dolls;

    for (CCNode* node : menu->getChildrenExt()) {
        GJItemIcon* item = node->getChildByType<GJItemIcon>(0);
        SimplePlayer* doll = item ? item->getChildByType<SimplePlayer>(0) : nullptr;
        if (doll) dolls.push_back(doll);
    }

    return dolls;
}

IconType GradientGarageLayer::getType() {
    SimplePlayer* pageIcon = m_fields->m_pageIcon;
    return pageIcon ? GradientUtils::getIconType(pageIcon) : IconType::Cube;
}

void GradientGarageLayer::updatePageIcons() {
    bool p2 = sdiSaved<bool>("2pselected", false);
    if (pageSlotDisabled(p2)) return;

    auto dolls = getPageIcons();
    IconType kind = dolls.empty() ? IconType::Cube : GradientUtils::getIconType(dolls.front());
    paintEach(dolls, GradientUtils::getGradient(kind, p2), p2, 66);
}

void GradientGarageLayer::updateGradient() {
    auto f = m_fields.self();

    if (!moduleEnabled()) {
        if (!m_playerObject || f->m_isDisabled) return;

        f->m_isDisabled = true;

        clearDoll(m_playerObject);

        if (sdiEnabled()) {
            if (SimplePlayer* doll = typeinfo_cast<SimplePlayer*>(getChildByID("player2-icon")))
                clearDoll(doll);
        }

        paintEach(getPageIcons(), Gradient{}, false, 0);

        return;
    }

    if (f->m_isP2Separate != GradientCache::is2PSeparate()) {
        f->m_isP2Separate = GradientCache::is2PSeparate();

        updatePageIcons();
    }

    bool noP2 = GradientCache::is2PDisabled();

    if (noP2) {
        f->m_isP2Disabled = true;

        if (sdiEnabled()) {
            if (SimplePlayer* doll = typeinfo_cast<SimplePlayer*>(getChildByID("player2-icon")))
                clearDoll(doll);

            if (sdiSaved<bool>("2pselected", false))
                paintEach(getPageIcons(), Gradient{}, false, 0);
        }
    } else if (f->m_isP2Disabled) {
        f->m_isP2Disabled = false;

        if (sdiEnabled() && sdiSaved<bool>("2pselected", false)) {
            auto dolls = getPageIcons();
            IconType kind = dolls.empty() ? IconType::Cube : GradientUtils::getIconType(dolls.front());
            paintEach(dolls, GradientUtils::getGradient(kind, true), true, 66);
        }
    }

    // One-shot re-enable: the module just came back on, repaint the page.
    if (std::exchange(f->m_isDisabled, false))
        updatePageIcons();

    Loader::get()->queueInMainThread([self = Ref(this)] {
        if (!self->m_playerObject) return;

        bool p2 = sdiSaved<bool>("2pselected", false);

        GradientUtils::paintMenuIcon(self->m_playerObject, false, 201);

        if (sdiEnabled() && !GradientCache::is2PDisabled()) {
            if (SimplePlayer* doll = typeinfo_cast<SimplePlayer*>(self->getChildByID("player2-icon")))
                GradientUtils::paintMenuIcon(doll, true, 202);
        }

        if (pageSlotDisabled(p2) || !paimon::modules::isEnabled("paimbnails.paimonicons.global")) return;

        SimplePlayer* pageIcon = self->m_fields->m_pageIcon;
        if (!pageIcon) return;

        Gradient current = GradientUtils::getGradient(GradientUtils::getIconType(pageIcon), p2);

        bool emptyNow = false;
        for (auto& lane : kGradientLanes)
            emptyNow = emptyNow || (current.*lane.config).isEmpty(lane.color, p2);

        if (self->m_fields->m_wasEmptied || emptyNow) {
            self->m_fields->m_wasEmptied = emptyNow;

            for (SimplePlayer* doll : self->getPageIcons()) {
                if (dollShown(doll))
                    GradientUtils::applyGradient(doll, current, false, p2, 66);
            }
        }

        if (dollShown(pageIcon))
            GradientUtils::paintMenuIcon(pageIcon, p2, 66);
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

        if (!sdiEnabled()) return;

        // The 2P swap button lives in the hub rail, hung during the
        // separate-dual init, i.e. before this deferred pass.
        CCNode* menu = paimon::garage_hub::rail(self);
        auto swap = menu ? static_cast<CCMenuItemSpriteExtra*>(menu->getChildByID("swap-2p-button")) : nullptr;
        if (!swap) return;

        self->m_fields->m_originalCallback = swap->m_pfnSelector;
        swap->m_pfnSelector = menu_selector(GradientGarageLayer::onSwap);
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
        CCNode* page = static_cast<CCNode*>(self->m_iconSelection->m_pages->firstObject());
        CCMenu* menu = page ? page->getChildByType<CCMenu>(0) : nullptr;

        if (menu) {
            for (CCNode* node : menu->getChildrenExt()) {
                GJItemIcon* item = node->getChildByType<GJItemIcon>(0);
                SimplePlayer* doll = item ? item->getChildByType<SimplePlayer>(0) : nullptr;
                if (!doll) continue;
                self->m_fields->m_pageIcon = doll;
                break;
            }
        }

        self->updateGradient();
    });
}
