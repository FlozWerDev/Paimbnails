#include "GarageButtonHub.hpp"

#include "ui/GarageHubPopup.hpp"
#include "../../utils/SpriteHelper.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::garage_hub {

namespace {

constexpr auto kRailID = "garage-hub-rail"_spr;
constexpr auto kHubButtonID = "garage-hub-btn"_spr;
constexpr auto kLabelKey = "hub-label"_spr;
constexpr auto kOrderKey = "hub-order"_spr;

// shared hub icon size inside the popup.
constexpr float kIconSize = 42.f;

int orderOf(CCNode* btn) {
    auto* value = typeinfo_cast<CCInteger*>(btn->getUserObject(kOrderKey));
    return value ? value->getValue() : 0;
}

void fitButton(CCMenuItem* btn) {
    auto const size = btn->getContentSize();
    float const dim = std::max(size.width, size.height);
    if (dim <= 0.f) return;

    float const scale = kIconSize / dim;
    btn->setScale(scale);
    // touch animation runs against m_basescale; keep it in sync.
    if (auto* extra = typeinfo_cast<CCMenuItemSpriteExtra*>(btn)) extra->m_baseScale = scale;
}

CCMenu* ensureRail(GJGarageLayer* layer) {
    if (auto* existing = rail(layer)) return existing;

    auto* menu = CCMenu::create();
    menu->setID(kRailID);
    menu->setPosition({0.f, 0.f});
    // hidden on purpose: invisible ccmenu ignores touches.
    menu->setVisible(false);
    layer->addChild(menu);
    return menu;
}

GJGarageLayer* garageOf(CCNode* node) {
    for (auto* parent = node; parent; parent = parent->getParent()) {
        if (auto* garage = typeinfo_cast<GJGarageLayer*>(parent)) return garage;
    }
    return nullptr;
}

}  // anonymous namespace

CCMenu* rail(GJGarageLayer* layer) {
    if (!layer) return nullptr;
    return typeinfo_cast<CCMenu*>(layer->getChildByID(kRailID));
}

void addButton(GJGarageLayer* layer, CCMenuItem* btn, std::string const& label, int order) {
    if (!layer || !btn) return;

    btn->setUserObject(kLabelKey, CCString::create(label));
    btn->setUserObject(kOrderKey, CCInteger::create(order));
    fitButton(btn);
    ensureRail(layer)->addChild(btn);
}

std::vector<CCMenuItem*> entries(GJGarageLayer* layer) {
    std::vector<CCMenuItem*> found;

    auto* menu = rail(layer);
    if (!menu) return found;

    for (auto* child : menu->getChildrenExt()) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(child)) found.push_back(item);
    }
    std::stable_sort(found.begin(), found.end(), [](CCMenuItem* a, CCMenuItem* b) {
        return orderOf(a) < orderOf(b);
    });
    return found;
}

std::string labelOf(CCNode* btn) {
    if (!btn) return {};
    auto* value = typeinfo_cast<CCString*>(btn->getUserObject(kLabelKey));
    return value ? value->getCString() : std::string{};
}

void installHubButton(GJGarageLayer* layer) {
    if (!layer || layer->getChildByIDRecursive(kHubButtonID)) return;

    auto* face = paimon::SpriteHelper::safeCreate("paim_Paimon.png"_spr);
    if (!face) face = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
    if (!face) return;

    auto* spr = CircleButtonSprite::create(face, CircleBaseColor::Pink, CircleBaseSize::Medium);
    if (!spr) return;

    auto* btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra* sender) {
        auto* garage = garageOf(sender);
        if (!garage) return;
        if (auto* popup = ui::GarageHubPopup::create(garage)) popup->show();
    });
    if (!btn) return;
    btn->setID(kHubButtonID);
    btn->setScale(0.7f);

    // node-ids moves shards/colors buttons to their own column; hang hub button there.
    if (auto* column = typeinfo_cast<CCMenu*>(layer->getChildByID("shards-menu"))) {
        column->addChild(btn);
        column->updateLayout();
        return;
    }

    // no node-ids: own menu bottom-left, next to colorful-icons (x = 26).
    auto* host = CCMenu::create();
    host->setID("garage-hub-host-menu"_spr);
    host->setPosition({0.f, 0.f});
    layer->addChild(host, 100);
    btn->setPosition({70.f, 26.f});
    host->addChild(btn);
}

}  // namespace paimon::garage_hub
