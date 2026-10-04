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
    // expandspritename -> "flozwer.paimbnails2/paim_....png"
    std::string expanded = mod->expandSpriteName(preferredPaim);
    if (auto* spr = paimon::SpriteHelper::safeCreate(expanded.c_str())) {
        return spr;
    }
    if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(expanded.c_str())) {
        return spr;
    }
    // bare filename (dev / loose file)
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
    // absolute last: empty node-sized sprite so callers never null-deref
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

CCNode* wrappedIcon(char const* preferredPaim, std::initializer_list<char const*> fallbacks) {
    auto* icon = loadIcon(preferredPaim, fallbacks);
    if (!icon) return nullptr;

    // basedbuttonsprite positions the top by center assuming anchor 0.5,
    // so it needs a sized top node.
    auto* wrap = CCNode::create();
    auto sz = icon->getContentSize();
    if (sz.width < 1.f || sz.height < 1.f) sz = CCSize{20.f, 20.f};
    wrap->setContentSize(sz);
    wrap->setAnchorPoint({0.5f, 0.5f});
    icon->setPosition(sz / 2.f);
    wrap->addChild(icon);
    return wrap;
}

CircleButtonSprite* circleIcon(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    CircleBaseColor color,
    CircleBaseSize size
) {
    auto* wrap = wrappedIcon(preferredPaim, fallbacks);
    if (!wrap) return nullptr;
    auto* base = CircleButtonSprite::create(wrap, color, size);
    if (!base) return nullptr;
    base->setTopRelativeScale(topScale);
    return base;
}

CCMenuItemSpriteExtra* menuItem(CCSprite* sprite, std::function<void()> onClick) {
    return CCMenuItemExt::createSpriteExtra(
        sprite, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
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
    return menuItem(base, std::move(onClick));
}

CCMenuItemSpriteExtra* squareButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    EditorBaseColor color,
    std::function<void()> onClick,
    float targetSize
) {
    auto* wrap = wrappedIcon(preferredPaim, fallbacks);
    if (!wrap) return nullptr;
    auto* base = EditorButtonSprite::create(wrap, color);
    if (!base) return nullptr;
    base->setTopRelativeScale(topScale);
    auto const h = base->getContentSize().height;
    if (targetSize > 0.f && h > 0.f) base->setScale(targetSize / h);
    return menuItem(base, std::move(onClick));
}

} // namespace paimon::editor::assets
