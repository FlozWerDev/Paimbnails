#include "IconRecolorEngine.hpp"
#include "IconColorService.hpp"
#include "IconConfigStore.hpp"
#include "IconLockStyler.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJItemIcon.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GJRobotSprite.hpp>
#include <Geode/binding/GJSpiderSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ListButtonBar.hpp>
#include <Geode/binding/ListButtonPage.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/utils/cocos.hpp>

#include <vector>

using namespace geode::prelude;

namespace paimon::icons {

namespace {

// Walk a node tree (BFS) and call `cb` for every GJItemIcon found. We don't
// recurse into already-recolored sub-icons (e.g. nested previews), but the
// idiomatic case is shallow trees so this is rarely an issue.
template <class Fn>
void forEachItemIcon(CCNode* root, Fn&& cb) {
    if (!root) return;
    std::vector<CCNode*> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        CCNode* node = stack.back();
        stack.pop_back();
        if (auto* icon = typeinfo_cast<GJItemIcon*>(node)) {
            cb(icon);
            // Don't recurse - the SimplePlayer is inside, no point.
            continue;
        }
        auto* children = node->getChildren();
        if (!children) continue;
        for (int i = 0; i < children->count(); ++i) {
            if (auto* c = static_cast<CCNode*>(children->objectAtIndex(i))) {
                stack.push_back(c);
            }
        }
    }
}

// Best-effort detection of the icon's gamemode based on which sub-sprite is
// active. Returns 0..8 (Cube..Jetpack). Defaults to Cube when unsure.
int detectGamemode(SimplePlayer* sp) {
    if (!sp) return 0;
    if (sp->m_robotSprite && sp->m_robotSprite->isVisible())  return 5;  // Robot
    if (sp->m_spiderSprite && sp->m_spiderSprite->isVisible()) return 6;  // Spider
    if (sp->m_birdDome && sp->m_birdDome->isVisible())         return 3;  // Ufo
    // First/second layers are present on every gamemode; we can't easily
    // distinguish Cube/Ship/Ball/Wave/Swing/Jetpack just from members, so
    // we leave them all as Cube. Modes that care about per-gamemode color
    // (PerGamemode mode) typically configure cube + ship + the rest fall
    // back to player colors via the optional palette slot.
    return 0;
}

// "Currently locked" is most reliably detected by opacity of m_firstLayer
// being 120 (the value GJItemIcon::changeToLockedState uses). We avoid
// touching SimplePlayer state when locked - the lock styler owns it.
bool isLocked(SimplePlayer* sp) {
    if (!sp || !sp->m_firstLayer) return false;
    return sp->m_firstLayer->getOpacity() == 120;
}

}  // anonymous namespace

IconRecolorEngine& IconRecolorEngine::get() {
    static IconRecolorEngine instance;
    return instance;
}

bool IconRecolorEngine::isAreaEnabled(RecolorArea area) const {
    auto const& cfg = IconConfigStore::get().config();
    switch (area) {
        case RecolorArea::IconKit:    return cfg.apply.kit;
        case RecolorArea::Shop:       return cfg.apply.shops;
        case RecolorArea::Achievement:return cfg.apply.achievements;
        case RecolorArea::Reward:     return cfg.apply.rewards;
        case RecolorArea::Profile:    return cfg.apply.profiles;
        case RecolorArea::Comment:    return cfg.apply.comments;
        case RecolorArea::LevelCell:  return cfg.apply.levelCells;
    }
    return false;
}

int IconRecolorEngine::gamemodeIndexOf(SimplePlayer* sp) const {
    return detectGamemode(sp);
}

bool IconRecolorEngine::recolorOne(GJItemIcon* icon, int displayIndex, int totalCount, RecolorArea area) {
    if (!icon) return false;
    if (!IconConfigStore::get().isFeatureEnabled()) return false;
    if (!isAreaEnabled(area)) {
        log::info("[paimon-icons] recolorOne: area disabled");
        return false;
    }

    auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player);
    if (!sp) {
        log::info("[paimon-icons] recolorOne: m_player not a SimplePlayer");
        return false;  // m_player may be a plain CCSprite (trail icons, etc.)
    }

    // If the icon is in its locked visual state, defer to the styler.
    if (isLocked(sp)) {
        IconLockStyler::get().apply(icon);
        return true;
    }

    IconDescriptor desc;
    desc.unlockTypeRaw  = static_cast<int>(icon->m_unlockType);
    desc.iconID         = icon->m_unlockID;
    desc.gamemodeIndex  = gamemodeIndexOf(sp);
    desc.displayIndex   = displayIndex;
    desc.totalCount     = std::max(1, totalCount);
    desc.isLocked       = false;

    auto triple = IconColorService::get().resolve(desc);
    sp->setColors(triple.primary, triple.secondary);
    if (triple.hasGlow) sp->setGlowOutline(triple.glow);
    else                sp->disableGlowOutline();

    log::info("[paimon-icons] recolorOne id={} type={} ({} {} {})",
        icon->m_unlockID, static_cast<int>(icon->m_unlockType),
        triple.primary.r, triple.primary.g, triple.primary.b);

    // Mark the SimplePlayer so other recolor mods know we own it. Use a
    // user-flag on the icon root so it survives child rebuilds.
    icon->setUserObject("paimbnails/icon-recolored",
        cocos2d::CCBool::create(true));
    return true;
}

void IconRecolorEngine::recolorSubtree(CCNode* root, RecolorArea area) {
    if (!root) return;
    if (!IconConfigStore::get().isFeatureEnabled()) return;
    if (!isAreaEnabled(area)) return;

    // Collect first so we have totalCount for Gradient mode.
    std::vector<GJItemIcon*> icons;
    forEachItemIcon(root, [&](GJItemIcon* icon) { icons.push_back(icon); });
    int total = static_cast<int>(icons.size());
    for (int i = 0; i < total; ++i) {
        recolorOne(icons[i], i, total, area);
    }
}

void IconRecolorEngine::recolorListBar(ListButtonBar* bar, RecolorArea area) {
    if (!bar) return;
    if (!bar->m_pages || bar->m_pages->count() == 0) {
        log::info("[paimon-icons] recolorListBar: no pages");
        return;
    }

    int pageCount = bar->m_pages->count();
    log::info("[paimon-icons] recolorListBar: {} pages", pageCount);

    // Walk EVERY page of the bar, not just the first one. The garage
    // contains 9 gamemode tabs, each with multiple pages, and the user
    // can swipe to any of them. We want all visible/cached icons to be
    // recolored so the moment they swap pages they see correct colors.
    std::vector<GJItemIcon*> icons;
    icons.reserve(pageCount * 36);  // rough upper bound

    for (int p = 0; p < pageCount; ++p) {
        auto* page = static_cast<ListButtonPage*>(bar->m_pages->objectAtIndex(p));
        if (!page) continue;
        auto* menu = page->getChildByType<CCMenu>(0);
        if (!menu) continue;
        auto* children = menu->getChildren();
        if (!children) continue;
        for (int i = 0; i < children->count(); ++i) {
            auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(
                static_cast<CCObject*>(children->objectAtIndex(i)));
            if (!btn) continue;
            if (auto* icon = btn->getChildByType<GJItemIcon>(0)) {
                icons.push_back(icon);
            }
        }
    }
    int total = static_cast<int>(icons.size());
    log::info("[paimon-icons] recolorListBar: collected {} icons", total);
    for (int i = 0; i < total; ++i) {
        recolorOne(icons[i], i, total, area);
    }
}

void IconRecolorEngine::applySelectedHighlight(CCNode* root, int equippedID, int gamemodeIndex) {
    if (!root) return;
    auto const& cfg = IconConfigStore::get().config();
    if (!cfg.anim.selectedHighlight) return;
    if (!IconConfigStore::get().isFeatureEnabled()) return;
    (void)gamemodeIndex;  // reserved for future use - we only highlight the
                          // icon whose unlockID matches equippedID.

    forEachItemIcon(root, [&](GJItemIcon* icon) {
        if (!icon) return;
        // Remove any previous highlight ring we may have added.
        if (auto* existing = icon->getChildByID("paimbnails/highlight-ring"_spr)) {
            existing->removeFromParent();
        }
        if (icon->m_unlockID != equippedID) return;

        // Add a soft glow ring behind the icon.
        auto* ring = CCSprite::createWithSpriteFrameName("GJ_circleSelectGlow_001.png");
        if (!ring) return;
        ring->setID("paimbnails/highlight-ring"_spr);
        ring->setColor(cfg.anim.highlightColor);
        ring->setOpacity(220);
        ring->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        ring->setPosition(icon->getContentSize() / 2);
        ring->setScale(0.85f);
        icon->addChild(ring, -1);
    });
}

void IconRecolorEngine::applyAnimations(CCNode* root, RecolorArea area) {
    if (!root) return;
    if (!isAreaEnabled(area)) return;
    auto const& cfg = IconConfigStore::get().config();

    forEachItemIcon(root, [&](GJItemIcon* icon) {
        auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player);
        if (!sp) return;

        // Remove our previous animations by ID. Cocos2d doesn't let us tag
        // by string, so we instead store a child node sentinel that owns
        // the actions; cleanup is to remove that sentinel.
        if (auto* sentinel = icon->getChildByID("paimbnails/anim-sentinel"_spr)) {
            sentinel->removeFromParent();
        }

        bool needsAnim = false;
        if (cfg.anim.pulseLocked && isLocked(sp)) needsAnim = true;
        if (cfg.anim.idleFloat) needsAnim = true;
        if (!needsAnim) return;

        auto* sentinel = CCNode::create();
        sentinel->setID("paimbnails/anim-sentinel"_spr);
        icon->addChild(sentinel);

        if (cfg.anim.pulseLocked && isLocked(sp)) {
            float dur = std::max(0.3f, 1.4f / std::max(0.1f, cfg.anim.pulseSpeed));
            auto* fadeOut = CCFadeTo::create(dur * 0.5f, 80);
            auto* fadeIn  = CCFadeTo::create(dur * 0.5f, 200);
            auto* seq = CCSequence::create(fadeOut, fadeIn, nullptr);
            sp->stopAllActions();
            sp->runAction(CCRepeatForever::create(seq));
        }

        if (cfg.anim.idleFloat) {
            float amount = std::clamp(cfg.anim.floatAmount, 0.0f, 8.0f);
            float dur    = 1.5f;
            auto* up   = CCMoveBy::create(dur * 0.5f, {0, amount});
            auto* down = CCMoveBy::create(dur * 0.5f, {0, -amount});
            auto* seq  = CCSequence::create(up, down, nullptr);
            // Apply to the icon container so glow stays in sync.
            icon->runAction(CCRepeatForever::create(seq));
        }
    });
}

void IconRecolorEngine::revertSubtree(CCNode* root) {
    if (!root) return;
    forEachItemIcon(root, [](GJItemIcon* icon) {
        if (auto* sentinel = icon->getChildByID("paimbnails/anim-sentinel"_spr)) {
            sentinel->removeFromParent();
        }
        if (auto* ring = icon->getChildByID("paimbnails/highlight-ring"_spr)) {
            ring->removeFromParent();
        }
        icon->stopAllActions();
        if (auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player)) {
            sp->stopAllActions();
        }
        icon->setUserObject("paimbnails/icon-recolored", nullptr);
    });
}

}  // namespace paimon::icons
