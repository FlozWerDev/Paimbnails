#include "IconStoreGarageGlue.hpp"

#include "../ui/IconStoreLayer.hpp"
#include "../../garage-hub/GarageButtonHub.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJGarageLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

namespace paimon::icon_gallery::garage {

namespace {

CCSprite* makeFace() {
    // Cualquiera de las tres existe en las hojas del juego; se encadenan por
    // si una version futura renombra alguna.
    for (char const* frame : {"GJ_downloadsIcon_001.png",
                              "GJ_sDownloadIcon_001.png",
                              "GJ_plusBtn_001.png"}) {
        if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frame)) return spr;
    }
    return nullptr;
}

void installButton(GJGarageLayer* layer) {
    if (layer->getChildByIDRecursive("icon-store-btn"_spr)) return;

    auto* face = makeFace();
    if (!face) return;

    // CircleButtonSprite escala su nodo interior por contentSize, y el sprite
    // ya lo tiene, asi que entra directo (a diferencia de un SimplePlayer).
    auto* spr = CircleButtonSprite::create(face, CircleBaseColor::Green,
                                           CircleBaseSize::Medium);
    if (!spr) return;

    auto* btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
        IconStoreLayer::open();
    });
    if (!btn) return;
    btn->setID("icon-store-btn"_spr);

    paimon::garage_hub::addButton(
        layer, btn, Localization::get().getString("garage-hub.icon-store"), 20);
}

}  // anonymous namespace

void onGarageInit(GJGarageLayer* layer) {
    if (!layer) return;
    if (!paimon::settings::icon_gallery::enabled()) return;
    installButton(layer);
}

}  // namespace paimon::icon_gallery::garage
