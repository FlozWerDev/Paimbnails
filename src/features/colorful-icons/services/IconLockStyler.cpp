#include "IconLockStyler.hpp"
#include "IconConfigStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJItemIcon.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/binding/GJRobotSprite.hpp>
#include <Geode/binding/GJSpiderSprite.hpp>
#include <Geode/binding/CCPartAnimSprite.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

namespace paimon::icons {

IconLockStyler& IconLockStyler::get() {
    static IconLockStyler instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────

bool IconLockStyler::isUnobtainable(GJItemIcon* icon) const {
    if (!icon) return false;
    auto* gsm = GameStatsManager::get();
    if (!gsm) return false;
    // getItemUnlockState returns 0 for "currently unobtainable" (e.g. locked
    // behind a 2.21 reward, gauntlets that have been removed, Wraith codes…)
    return gsm->getItemUnlockState(icon->m_unlockID, icon->m_unlockType) == 0;
}

cocos2d::CCSprite* IconLockStyler::findLockSprite(GJItemIcon* icon) const {
    if (!icon) return nullptr;
    // GJItemIcon::changeToLockedState adds a "GJ_lock_001.png" CCSprite as a
    // direct child. We look for a CCSprite that is NOT the SimplePlayer
    // wrapper. typeinfo_cast handles the SimplePlayer sub-class detection.
    auto* children = icon->getChildren();
    if (!children) return nullptr;
    for (int i = 0; i < children->count(); ++i) {
        auto* node = static_cast<CCNode*>(children->objectAtIndex(i));
        if (typeinfo_cast<SimplePlayer*>(node)) continue;  // skip the icon itself
        if (auto* spr = typeinfo_cast<CCSprite*>(node)) {
            return spr;
        }
    }
    return nullptr;
}

// Walk the SimplePlayer's tree and force every visible part to a given color.
// Used by ShowDimmed/Silhouette so the icon is uniformly tinted.
static void recolorAllParts(SimplePlayer* sp, ccColor3B tint) {
    if (!sp) return;
    if (sp->m_firstLayer)   sp->m_firstLayer->setColor(tint);
    if (sp->m_secondLayer)  sp->m_secondLayer->setColor(tint);
    if (sp->m_outlineSprite) sp->m_outlineSprite->setColor(tint);
    if (sp->m_detailSprite)  sp->m_detailSprite->setColor(tint);
    if (sp->m_birdDome)      sp->m_birdDome->setColor(tint);

    auto handleVehicle = [&](GJRobotSprite* vehicle) {
        if (!vehicle) return;
        if (vehicle->m_extraSprite) vehicle->m_extraSprite->setColor(tint);
        // m_secondArray contains the back/leg sprites for robot/spider.
        if (auto* arr = vehicle->m_secondArray) {
            for (auto* part : CCArrayExt<CCSprite*>(arr)) {
                if (part) part->setColor(tint);
            }
        }
        // The front anim sprite hangs off the vehicle as a child by index.
        if (auto* anim = vehicle->getChildByType<CCPartAnimSprite>(0)) {
            anim->setColor(tint);
        }
        for (auto* child : CCArrayExt<CCNode*>(vehicle->getChildren())) {
            if (auto* spr = typeinfo_cast<CCSprite*>(child)) {
                spr->setColor(tint);
            }
        }
    };

    handleVehicle(sp->m_robotSprite);
    handleVehicle(sp->m_spiderSprite);
}

// Set the same opacity recursively on all CCSprites under a SimplePlayer.
static void recolorAllOpacity(SimplePlayer* sp, GLubyte opacity) {
    if (!sp) return;
    sp->setOpacity(opacity);
    if (sp->m_firstLayer)    sp->m_firstLayer->setOpacity(opacity);
    if (sp->m_secondLayer)   sp->m_secondLayer->setOpacity(opacity);
    if (sp->m_outlineSprite) sp->m_outlineSprite->setOpacity(opacity);
    if (sp->m_detailSprite)  sp->m_detailSprite->setOpacity(opacity);
    if (sp->m_birdDome)      sp->m_birdDome->setOpacity(opacity);

    auto fadeVehicle = [&](GJRobotSprite* vehicle) {
        if (!vehicle) return;
        vehicle->setOpacity(opacity);
        if (vehicle->m_extraSprite) vehicle->m_extraSprite->setOpacity(opacity);
        if (auto* arr = vehicle->m_secondArray) {
            for (auto* part : CCArrayExt<CCSprite*>(arr)) {
                if (part) part->setOpacity(opacity);
            }
        }
        for (auto* child : CCArrayExt<CCNode*>(vehicle->getChildren())) {
            if (auto* spr = typeinfo_cast<CCSprite*>(child)) {
                spr->setOpacity(opacity);
            }
        }
    };
    fadeVehicle(sp->m_robotSprite);
    fadeVehicle(sp->m_spiderSprite);
}

// ─────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────

void IconLockStyler::apply(GJItemIcon* icon) {
    if (!icon) return;
    auto& store = IconConfigStore::get();
    if (!store.isFeatureEnabled()) return;
    auto const& cfg = store.config();
    if (cfg.lockStyle == LockStyle::Default) return;

    auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player);
    bool unobtainable = isUnobtainable(icon);

    switch (cfg.lockStyle) {
        case LockStyle::Default:                                         break;
        case LockStyle::ShowDimmed:  applyShowDimmed (icon, sp, unobtainable, cfg); break;
        case LockStyle::TintedLock:  applyTinted    (icon, sp, unobtainable, cfg); break;
        case LockStyle::Silhouette:  applySilhouette(icon, sp, unobtainable, cfg); break;
        case LockStyle::CustomMix:   applyCustomMix (icon, sp, unobtainable, cfg); break;
        case LockStyle::HideBoth:    applyHideBoth  (icon, sp); break;
    }
}

// ─────────────────────────────────────────────────────────────
// Style implementations
//
// All five styles work by toggling the visibility of the lock sprite, the
// SimplePlayer, and individual sub-parts; the only state we touch on the
// SimplePlayer is opacity / color, both of which the original code paths
// also touch.
// ─────────────────────────────────────────────────────────────

void IconLockStyler::applyShowDimmed(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg) {
    if (auto* lock = findLockSprite(icon)) lock->setVisible(false);
    if (!sp) return;

    sp->setVisible(true);
    // Restore detail/dome visibility the original locked-state hid.
    if (sp->m_detailSprite) sp->m_detailSprite->setVisible(true);
    if (sp->m_birdDome)     sp->m_birdDome->setVisible(true);
    if (sp->m_robotSprite && sp->m_robotSprite->m_extraSprite) sp->m_robotSprite->m_extraSprite->setVisible(true);
    if (sp->m_spiderSprite && sp->m_spiderSprite->m_extraSprite) sp->m_spiderSprite->m_extraSprite->setVisible(true);

    GLubyte op = static_cast<GLubyte>(std::clamp(
        unobtainable && cfg.dimUnobtainable ? cfg.unobtainableOpacity : cfg.dimOpacity,
        0, 255));
    recolorAllOpacity(sp, op);

    if (unobtainable && cfg.dimUnobtainable) {
        recolorAllParts(sp, cfg.unobtainableTint);
    }
}

void IconLockStyler::applyTinted(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg) {
    if (auto* lock = findLockSprite(icon)) {
        lock->setVisible(true);
        lock->setColor(unobtainable && cfg.dimUnobtainable ? cfg.unobtainableTint : cfg.lockTint);
    }
    if (sp) sp->setVisible(false);
}

void IconLockStyler::applySilhouette(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg) {
    if (auto* lock = findLockSprite(icon)) lock->setVisible(false);
    if (!sp) return;

    sp->setVisible(true);
    if (sp->m_detailSprite) sp->m_detailSprite->setVisible(false);
    if (sp->m_birdDome)     sp->m_birdDome->setVisible(true);
    if (sp->m_robotSprite && sp->m_robotSprite->m_extraSprite) sp->m_robotSprite->m_extraSprite->setVisible(false);
    if (sp->m_spiderSprite && sp->m_spiderSprite->m_extraSprite) sp->m_spiderSprite->m_extraSprite->setVisible(false);

    recolorAllOpacity(sp, 230);
    recolorAllParts(sp, unobtainable && cfg.dimUnobtainable ? cfg.unobtainableTint : cfg.silhouetteColor);
}

void IconLockStyler::applyCustomMix(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg) {
    // Custom mix re-uses the dim style fields but lets the lock stay visible.
    if (auto* lock = findLockSprite(icon)) {
        lock->setVisible(true);
        lock->setColor(unobtainable && cfg.dimUnobtainable ? cfg.unobtainableTint : cfg.lockTint);
        lock->setOpacity(200);
    }
    if (!sp) return;
    sp->setVisible(true);
    GLubyte op = static_cast<GLubyte>(std::clamp(
        unobtainable && cfg.dimUnobtainable ? cfg.unobtainableOpacity : cfg.dimOpacity,
        0, 255));
    recolorAllOpacity(sp, op);
}

void IconLockStyler::applyHideBoth(GJItemIcon* icon, SimplePlayer* sp) {
    if (auto* lock = findLockSprite(icon)) lock->setVisible(false);
    if (sp) sp->setVisible(false);
}

}  // namespace paimon::icons
