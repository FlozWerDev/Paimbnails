#include "IconCopyGarageGlue.hpp"

#include "../IconCopyStore.hpp"
#include "../ui/CopiedIconsPopup.hpp"
#include "../../garage-hub/GarageButtonHub.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGarageLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::iconcopy::garage {

namespace {

GJGarageLayer* findGarageLayer(CCNode* root) {
    if (!root) return nullptr;
    if (auto* garage = typeinfo_cast<GJGarageLayer*>(root)) return garage;
    if (auto* children = root->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (auto* garage = findGarageLayer(child)) return garage;
        }
    }
    return nullptr;
}

bool isGamemode(IconType type) {
    int const raw = static_cast<int>(type);
    return raw >= static_cast<int>(IconType::Cube) && raw <= static_cast<int>(IconType::Jetpack);
}

int selectedIcon(GameManager* gm, IconType type) {
    switch (type) {
        case IconType::Ship: return gm->getPlayerShip();
        case IconType::Ball: return gm->getPlayerBall();
        case IconType::Ufo: return gm->getPlayerBird();
        case IconType::Wave: return gm->getPlayerDart();
        case IconType::Robot: return gm->getPlayerRobot();
        case IconType::Spider: return gm->getPlayerSpider();
        case IconType::Swing: return gm->getPlayerSwing();
        case IconType::Jetpack: return gm->getPlayerJetpack();
        default: return gm->getPlayerFrame();
    }
}

void syncGarage(GJGarageLayer* garage) {
    auto* gm = GameManager::get();
    if (!garage || !gm) return;

    IconType const tab = garage->m_selectedIconType;
    IconType const previewType = isGamemode(tab) ? tab : IconType::Cube;
    if (auto* preview = garage->m_playerObject) {
        preview->updatePlayerFrame(std::max(selectedIcon(gm, previewType), 1), previewType);
    }
    garage->updatePlayerColors();
    // moves the selection cursor onto the icons we just switched to.
    garage->selectTab(tab);
}

void installButton(GJGarageLayer* layer) {
    if (layer->getChildByIDRecursive("copied-icons-btn"_spr)) return;

    CCNode* spr = nullptr;
    if (auto* glyph = paimon::SpriteHelper::safeCreate("paim_garage_copiedIcons.png"_spr)) {
        spr = CircleButtonSprite::create(glyph, CircleBaseColor::Blue, CircleBaseSize::Medium);
    }
    if (!spr) {
        // vanilla fallbacks are full button sprites (base included), so they go in as-is.
        CCSprite* full = paimon::SpriteHelper::safeCreateWithFrameName("GJ_profileButton_001.png");
        if (!full) full = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
        if (!full) return;
        float const dim = std::max(full->getContentWidth(), full->getContentHeight());
        if (dim > 0.f) full->setScale(28.f / dim);
        spr = full;
    }

    auto* btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
        if (auto* popup = CopiedIconsPopup::create()) popup->show();
    });
    if (!btn) return;
    btn->setID("copied-icons-btn"_spr);

    paimon::garage_hub::addButton(
        layer, btn, Localization::get().getString("garage-hub.copied-icons"), 10);
}

}  // anonymous namespace

void onGarageInit(GJGarageLayer* layer) {
    if (!layer) return;
    if (!enabled()) return;
    installButton(layer);
}

void refreshVisibleGarage() {
    Loader::get()->queueInMainThread([] {
        if (paimon::isRuntimeShuttingDown()) return;
        auto* dir = CCDirector::sharedDirector();
        if (!dir) return;
        syncGarage(findGarageLayer(dir->getRunningScene()));
    });
}

}  // namespace paimon::iconcopy::garage
