#include "EditorAssets.hpp"

#include "../../utils/SpriteHelper.hpp"

#include <Geode/loader/Mod.hpp>
#include <string>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editor::assets {

namespace {

CCSprite* tryModSprite(char const* preferredPaim) {
    if (!preferredPaim || !*preferredPaim) return nullptr;
    auto* mod = Mod::get();
    if (!mod) return nullptr;
    // expandSpriteName -> "flozwer.paimbnails2/paim_....png"
    std::string expanded = mod->expandSpriteName(preferredPaim);
    if (auto* spr = paimon::SpriteHelper::safeCreate(expanded.c_str())) {
        return spr;
    }
    if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(expanded.c_str())) {
        return spr;
    }
    // Bare filename (dev / loose file)
    if (auto* spr = paimon::SpriteHelper::safeCreate(preferredPaim)) {
        return spr;
    }
    return nullptr;
}

CCSprite* tryFallbackFrames(std::initializer_list<char const*> fallbacks) {
    for (auto* f : fallbacks) {
        if (!f || !*f) continue;
        if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(f)) {
            return spr;
        }
        if (auto* spr = paimon::SpriteHelper::safeCreate(f)) {
            return spr;
        }
    }
    return nullptr;
}

CCSprite* lastResort() {
    if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        return spr;
    }
    auto* spr = CCSprite::create("square02_001.png");
    if (spr && paimon::SpriteHelper::isValidSprite(spr)) return spr;
    // Absolute last: empty node-sized sprite so callers never null-deref
    auto* empty = CCSprite::create();
    if (empty) empty->setContentSize({20.f, 20.f});
    return empty;
}

CCSprite* loadIcon(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks
) {
    CCSprite* spr = tryModSprite(preferredPaim);
    if (!spr) spr = tryFallbackFrames(fallbacks);
    if (!spr) spr = lastResort();
    return spr;
}

CircleButtonSprite* circleIcon(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    CircleBaseColor color,
    CircleBaseSize size
) {
    auto* icon = loadIcon(preferredPaim, fallbacks);
    if (!icon) return nullptr;

    // CircleButtonSprite needs a sized top node with a centered anchor —
    // BasedButtonSprite positions the top by center assuming anchor 0.5.
    auto* wrap = CCNode::create();
    auto sz = icon->getContentSize();
    if (sz.width < 1.f || sz.height < 1.f) sz = CCSize{20.f, 20.f};
    wrap->setContentSize(sz);
    wrap->setAnchorPoint({0.5f, 0.5f});
    icon->setPosition(sz / 2.f);
    wrap->addChild(icon);

    auto* base = CircleButtonSprite::create(wrap, color, size);
    if (!base) return nullptr;
    base->setTopRelativeScale(topScale);
    return base;
}

} // namespace

bool hasCustom(char const* preferredPaim) {
    return tryModSprite(preferredPaim) != nullptr;
}

CCMenuItemSpriteExtra* circleButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    CircleBaseColor color,
    std::function<void()> onClick,
    CircleBaseSize size
) {
    auto* base = circleIcon(preferredPaim, fallbacks, topScale, color, size);
    if (!base) return nullptr;
    return CCMenuItemExt::createSpriteExtra(
        base, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
}

} // namespace paimon::editor::assets
