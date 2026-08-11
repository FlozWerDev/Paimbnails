#include "IconRecolorEngine.hpp"
#include "IconColorService.hpp"
#include "IconConfigStore.hpp"
#include "IconLockStyler.hpp"

#include <Geode/Geode.hpp>

#include <vector>

using namespace geode::prelude;

namespace paimon::icons {

namespace {

// Walk a node tree (BFS) and call `cb` for every GJItemIcon found.
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

bool isLockedIcon(GJItemIcon* icon, SimplePlayer* sp) {
    if (icon && icon->getUserObject(kIconLockedKey)) return true;
    // Fallback for icons locked before our hook stamped them: vanilla
    // changeToLockedState leaves the first layer at opacity 120.
    return sp && sp->m_firstLayer && sp->m_firstLayer->getOpacity() == 120;
}

// Gather every GJItemIcon across all cached pages of a ListButtonBar (pages
// that are swiped away are detached from the node tree, so a subtree walk
// misses them).
void collectListBarIcons(ListButtonBar* bar, std::vector<GJItemIcon*>& out) {
    if (!bar || !bar->m_pages) return;
    int pageCount = bar->m_pages->count();
    out.reserve(pageCount * 36);
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
                out.push_back(icon);
            }
        }
    }
}

// Everything vanilla changeToLockedState touched, captured right after it ran.
// Restore replays these values instead of guessing them: vanilla hides detail
// sprites and UFO domes and keeps its own layer colors, so a hardcoded
// "browser gray + everything visible" rebuild turns locked icons into pale
// white blobs.
class LockedVanillaSnapshot : public CCObject {
public:
    ccColor3B first{175, 175, 175};
    ccColor3B second{255, 255, 255};
    unsigned char opacity = 120;
    bool playerVisible = true;
    bool detailVisible = false;
    bool domeVisible = false;
    bool robotExtraVisible = false;
    bool spiderExtraVisible = false;
    bool lockVisible = true;
    ccColor3B lockColor{255, 255, 255};
    unsigned char lockOpacity = 255;

    static LockedVanillaSnapshot* capture(GJItemIcon* icon, SimplePlayer* sp) {
        auto* s = new LockedVanillaSnapshot();
        s->autorelease();
        if (sp->m_firstLayer) {
            s->first   = sp->m_firstLayer->getColor();
            s->opacity = sp->m_firstLayer->getOpacity();
        }
        if (sp->m_secondLayer) s->second = sp->m_secondLayer->getColor();
        s->playerVisible = sp->isVisible();
        if (sp->m_detailSprite) s->detailVisible = sp->m_detailSprite->isVisible();
        if (sp->m_birdDome)     s->domeVisible   = sp->m_birdDome->isVisible();
        if (sp->m_robotSprite && sp->m_robotSprite->m_extraSprite) {
            s->robotExtraVisible = sp->m_robotSprite->m_extraSprite->isVisible();
        }
        if (sp->m_spiderSprite && sp->m_spiderSprite->m_extraSprite) {
            s->spiderExtraVisible = sp->m_spiderSprite->m_extraSprite->isVisible();
        }
        if (auto* lock = IconLockStyler::findLockSprite(icon)) {
            s->lockVisible = lock->isVisible();
            s->lockColor   = lock->getColor();
            s->lockOpacity = lock->getOpacity();
        }
        return s;
    }

    void replay(GJItemIcon* icon, SimplePlayer* sp) const {
        sp->setVisible(playerVisible);
        if (sp->m_detailSprite) sp->m_detailSprite->setVisible(detailVisible);
        if (sp->m_birdDome)     sp->m_birdDome->setVisible(domeVisible);
        if (sp->m_robotSprite && sp->m_robotSprite->m_extraSprite) {
            sp->m_robotSprite->m_extraSprite->setVisible(robotExtraVisible);
        }
        if (sp->m_spiderSprite && sp->m_spiderSprite->m_extraSprite) {
            sp->m_spiderSprite->m_extraSprite->setVisible(spiderExtraVisible);
        }
        IconLockStyler::tintAllParts(sp, {255, 255, 255});
        sp->setColors(first, second);
        IconLockStyler::fadeAllParts(sp, opacity);
        if (auto* lock = IconLockStyler::findLockSprite(icon)) {
            lock->setVisible(lockVisible);
            lock->setColor(lockColor);
            lock->setOpacity(lockOpacity);
        }
    }
};

}  // anonymous namespace

IconRecolorEngine& IconRecolorEngine::get() {
    static IconRecolorEngine instance;
    return instance;
}

bool IconRecolorEngine::isAreaEnabled(RecolorArea area) const {
    auto const& cfg = IconConfigStore::get().config();
    switch (area) {
        case RecolorArea::IconKit: return cfg.apply.kit;
        case RecolorArea::Shop:    return cfg.apply.shops;
    }
    return false;
}

bool IconRecolorEngine::recolorOne(GJItemIcon* icon, int displayIndex, int totalCount, RecolorArea area) {
    if (!icon) return false;
    if (!IconConfigStore::get().isFeatureEnabled()) return false;
    if (!isAreaEnabled(area)) return false;

    auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player);
    if (!sp) return false;

    if (isLockedIcon(icon, sp)) {
        IconLockStyler::get().apply(icon);
        return true;
    }

    IconDescriptor desc;
    desc.unlockTypeRaw = static_cast<int>(icon->m_unlockType);
    desc.iconID        = icon->m_unlockID;
    desc.displayIndex  = displayIndex;
    desc.totalCount    = std::max(1, totalCount);

    auto triple = IconColorService::get().resolve(desc);
    sp->setColors(triple.primary, triple.secondary);
    if (triple.hasGlow) sp->setGlowOutline(triple.glow);
    else                sp->disableGlowOutline();

    icon->setUserObject(kIconRecoloredKey, cocos2d::CCBool::create(true));
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
    if (!IconConfigStore::get().isFeatureEnabled()) return;
    if (!isAreaEnabled(area)) return;

    std::vector<GJItemIcon*> icons;
    collectListBarIcons(bar, icons);
    int total = static_cast<int>(icons.size());
    for (int i = 0; i < total; ++i) {
        recolorOne(icons[i], i, total, area);
    }
}

void IconRecolorEngine::snapshotLockedVanilla(GJItemIcon* icon) {
    if (!icon) return;
    if (icon->getUserObject(kIconLockSnapshotKey)) return;
    auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player);
    if (!sp) return;
    icon->setUserObject(kIconLockSnapshotKey, LockedVanillaSnapshot::capture(icon, sp));
}

void IconRecolorEngine::restoreOne(GJItemIcon* icon) {
    if (!icon) return;
    auto* sp = typeinfo_cast<SimplePlayer*>(icon->m_player);
    if (!sp) return;

    bool const locked = isLockedIcon(icon, sp);
    // Icons we never touched are already vanilla; repainting them here only
    // introduces drift from the real stock look.
    if (!locked && !icon->getUserObject(kIconRecoloredKey)) return;

    if (locked) {
        auto* snap = typeinfo_cast<LockedVanillaSnapshot*>(
            icon->getUserObject(kIconLockSnapshotKey));
        if (snap) {
            snap->replay(icon, sp);
        } else {
            // No snapshot (icon locked before our hook existed): best-effort
            // vanilla, which hides detail layers and keeps the icon dimmed.
            sp->setVisible(true);
            if (sp->m_detailSprite) sp->m_detailSprite->setVisible(false);
            if (sp->m_birdDome)     sp->m_birdDome->setVisible(false);
            if (sp->m_robotSprite && sp->m_robotSprite->m_extraSprite) sp->m_robotSprite->m_extraSprite->setVisible(false);
            if (sp->m_spiderSprite && sp->m_spiderSprite->m_extraSprite) sp->m_spiderSprite->m_extraSprite->setVisible(false);
            IconLockStyler::tintAllParts(sp, {255, 255, 255});
            sp->setColors({175, 175, 175}, {255, 255, 255});
            IconLockStyler::fadeAllParts(sp, 120);
            if (auto* lock = IconLockStyler::findLockSprite(icon)) {
                lock->setVisible(true);
                lock->setColor({255, 255, 255});
                lock->setOpacity(255);
            }
        }
        icon->setUserObject(kIconRecoloredKey, nullptr);
        return;
    }

    // Unlocked browser items are gray/white with no glow in stock GD.
    sp->setVisible(true);
    if (sp->m_detailSprite) sp->m_detailSprite->setVisible(true);
    if (sp->m_birdDome)     sp->m_birdDome->setVisible(true);
    if (sp->m_robotSprite && sp->m_robotSprite->m_extraSprite) sp->m_robotSprite->m_extraSprite->setVisible(true);
    if (sp->m_spiderSprite && sp->m_spiderSprite->m_extraSprite) sp->m_spiderSprite->m_extraSprite->setVisible(true);

    IconLockStyler::tintAllParts(sp, {255, 255, 255});
    sp->setColors({175, 175, 175}, {255, 255, 255});
    sp->disableGlowOutline();
    IconLockStyler::fadeAllParts(sp, 255);

    icon->setUserObject(kIconRecoloredKey, nullptr);
}

void IconRecolorEngine::restoreVanilla(CCNode* root) {
    if (!root) return;
    forEachItemIcon(root, [this](GJItemIcon* icon) { restoreOne(icon); });
}

void IconRecolorEngine::restoreListBar(ListButtonBar* bar) {
    std::vector<GJItemIcon*> icons;
    collectListBarIcons(bar, icons);
    for (auto* icon : icons) restoreOne(icon);
}

}  // namespace paimon::icons
