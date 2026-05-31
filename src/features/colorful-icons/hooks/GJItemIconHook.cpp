//
// GJItemIconHook.cpp - Hook GJItemIcon::init and changeToLockedState so every
// icon gets recolored at the moment it is constructed, regardless of which
// layer hosts it (icon kit, shops, achievements, rewards, etc.).
//
// This avoids the need for a per-frame ticker on the garage; the color is
// baked in at construction time and updated on user actions (color change,
// config change) which trigger the original GJGarageLayer::playerColorChanged
// path.
//
// Note on the last `ccColor3B unlockColor` parameter: at least on Windows,
// reading that parameter has historically caused crashes in mods that hooked
// this exact signature. We accept it but never read it; we forward `{}` to
// the original. This matches the workaround used by reference Geode mods.
//

#include "../services/IconColorService.hpp"
#include "../services/IconConfigStore.hpp"
#include "../services/IconLockStyler.hpp"
#include "../services/IconRecolorEngine.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJItemIcon.hpp>
#include <Geode/binding/SimplePlayer.hpp>

using namespace geode::prelude;
using paimon::icons::IconColorService;
using paimon::icons::IconConfigStore;
using paimon::icons::IconDescriptor;
using paimon::icons::IconLockStyler;
using paimon::icons::IconRecolorEngine;
using paimon::icons::RecolorArea;

namespace {

// Apply our recolor to a freshly-built GJItemIcon. Returns silently if the
// feature is disabled or the icon's m_player is not a SimplePlayer (some
// icons - like trail samples - use a plain CCSprite there).
void recolorFreshIcon(GJItemIcon* icon) {
    if (!icon) return;
    if (!IconConfigStore::get().isFeatureEnabled()) return;

    auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player);
    if (!sp) return;

    // If the icon is locked we defer to the lock styler entirely.
    if (sp->m_firstLayer && sp->m_firstLayer->getOpacity() == 120) {
        IconLockStyler::get().apply(icon);
        return;
    }

    IconDescriptor desc;
    desc.unlockTypeRaw = static_cast<int>(icon->m_unlockType);
    desc.iconID        = icon->m_unlockID;
    // We don't know totalCount here; per-construction is fine for all
    // modes except Gradient (which the engine handles via a separate
    // pass when iterating a known container).
    desc.displayIndex  = 0;
    desc.totalCount    = 1;

    auto triple = IconColorService::get().resolve(desc);
    sp->setColors(triple.primary, triple.secondary);
    if (triple.hasGlow) sp->setGlowOutline(triple.glow);
    else                sp->disableGlowOutline();
}

}  // anonymous namespace

class $modify(PaimonGJItemIcon, GJItemIcon) {
    static void onModify(auto& self) {
        // Some compilers/configurations are sensitive to this hook. Setting
        // a Late priority makes sure we run AFTER any bindings-level work
        // and gives other mods a chance to set up their own state.
        (void)self.setHookPriorityPost(
            "GJItemIcon::init", geode::Priority::Late);
        (void)self.setHookPriorityPost(
            "GJItemIcon::changeToLockedState", geode::Priority::Late);
    }

    $override
    bool init(
        UnlockType type, int id,
        cocos2d::ccColor3B color1, cocos2d::ccColor3B color2,
        bool dark, bool unused, bool noLabel,
        cocos2d::ccColor3B unlockColor
    ) {
        // Forward exactly as received but neutralize the trailing color
        // arg that has caused issues for hooks at this exact signature
        // in other mods. Passing {} is safe and matches what unlocked
        // call sites do anyway.
        if (!GJItemIcon::init(type, id, color1, color2, dark, unused, noLabel, {})) {
            return false;
        }
        // log::debug en lugar de log::info: GJItemIcon::init se llama
        // cientos de veces al abrir el icon kit y los shops, asi que el
        // ruido en INFO opaca otros mensajes utiles. Usar debug deja el
        // trace disponible cuando el setting "enable-debug-logs" esta
        // activo, sin spamear sesiones normales.
        log::debug("[paimon-icons] GJItemIcon::init type={} id={}",
            static_cast<int>(type), id);
        recolorFreshIcon(this);
        return true;
    }

    $override
    void changeToLockedState(float p0) {
        GJItemIcon::changeToLockedState(p0);
        IconLockStyler::get().apply(this);
    }
};
