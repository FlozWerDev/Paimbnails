#include "EditorAssets.hpp"
#include "EditorUIKit.hpp"

#include "../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
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
    // Some packs register as frames under the expanded name
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

} // namespace

bool hasCustom(char const* preferredPaim) {
    return tryModSprite(preferredPaim) != nullptr;
}

CCSprite* loadIcon(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float scale
) {
    CCSprite* spr = tryModSprite(preferredPaim);
    if (!spr) spr = tryFallbackFrames(fallbacks);
    if (!spr) spr = lastResort();
    if (spr && scale != 1.f) spr->setScale(scale);
    return spr;
}

CircleButtonSprite* circleIcon(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    CircleBaseColor color,
    CircleBaseSize size
) {
    auto* icon = loadIcon(preferredPaim, fallbacks, 1.f);
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

CCMenuItemToggler* circleToggler(
    char const* preferredOff,
    std::initializer_list<char const*> fallbacksOff,
    char const* preferredOn,
    std::initializer_list<char const*> fallbacksOn,
    float topScale,
    CircleBaseColor colorOff,
    CircleBaseColor colorOn,
    CCObject* target,
    SEL_MenuHandler selector,
    CircleBaseSize size
) {
    auto* offSpr = circleIcon(preferredOff, fallbacksOff, topScale, colorOff, size);
    auto* onSpr = circleIcon(preferredOn, fallbacksOn, topScale, colorOn, size);
    if (!onSpr || !offSpr) return nullptr;
    return CCMenuItemToggler::create(offSpr, onSpr, target, selector);
}

void normalizeToolbarItem(CCMenuItemSpriteExtra* item, CCSize size) {
    if (!item) return;
    item->setContentSize(size);
    if (auto* image = item->getNormalImage()) {
        image->setPosition(size / 2.f);
    }
}

void fitGridItem(CCMenuItemSpriteExtra* item, CCNode* menu) {
    if (!item) return;
    // Match the cell of an existing vanilla sibling so RowLayout keeps rows
    // uniform; oversize items overlap their neighbours otherwise.
    CCSize cell{40.f, 40.f};
    if (menu && menu->getChildren()) {
        for (auto* child : geode::cocos::CCArrayExt<CCNode*>(menu->getChildren())) {
            if (child == item) continue;
            auto cs = child->getContentSize();
            if (cs.width > 1.f && cs.height > 1.f) { cell = cs; break; }
        }
    }
    item->setContentSize(cell);
    if (auto* image = item->getNormalImage()) {
        auto is = image->getContentSize();
        if (is.width > 1.f && is.height > 1.f) {
            image->setScale(std::min(cell.width / is.width, cell.height / is.height));
        }
        image->setPosition(cell / 2.f);
    }
}

void normalizeToolbarToggle(CCMenuItemToggler* item, CCSize size) {
    if (!item) return;
    item->setContentSize(size);
    for (auto* face : {item->m_offButton, item->m_onButton}) {
        if (!face) continue;
        face->setContentSize(size);
        face->setPosition(size / 2.f);
        if (auto* image = face->getNormalImage()) {
            image->setPosition(size / 2.f);
        }
    }
}

CCMenuItemSpriteExtra* iconOrTextButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    char const* textFallback,
    char const* textTexture,
    float textScale,
    CircleBaseColor color,
    std::function<void()> onClick
) {
    // Prefer a real custom PNG when shipped; only then use circle icon.
    // If preferred is missing but vanilla fallbacks exist, still prefer text
    // for pause menus (Groups/Objects/Backups) when textFallback is set —
    // EXCEPT when preferred loads successfully.
    if (preferredPaim && *preferredPaim) {
        if (auto* custom = tryModSprite(preferredPaim)) {
            auto* wrap = CCNode::create();
            auto sz = custom->getContentSize();
            if (sz.width < 1.f || sz.height < 1.f) sz = CCSize{20.f, 20.f};
            wrap->setContentSize(sz);
            wrap->setAnchorPoint({0.5f, 0.5f});
            custom->setPosition(sz / 2.f);
            wrap->addChild(custom);
            auto* base = CircleButtonSprite::create(wrap, color, CircleBaseSize::Small);
            if (base) {
                base->setTopRelativeScale(0.85f);
                return CCMenuItemExt::createSpriteExtra(
                    base, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
                        if (cb) cb();
                    }
                );
            }
        }
    }

    // Vanilla-frame circle if no text fallback wanted
    if (!textFallback || !*textFallback) {
        return circleButton(preferredPaim, fallbacks, 0.85f, color, std::move(onClick), CircleBaseSize::Small);
    }

    auto* spr = ButtonSprite::create(
        textFallback, 54, true, "bigFont.fnt", textTexture, 30.f, textScale
    );
    if (!spr) return nullptr;
    return CCMenuItemExt::createSpriteExtra(
        spr, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
}

std::function<CCNode*()> tabIcon(char const* preferredPaim, char const* fallbackFrame) {
    return [preferredPaim, fallbackFrame]() -> CCNode* {
        return loadIcon(preferredPaim, {fallbackFrame}, 1.f);
    };
}

CCMenuItemSpriteExtra* editBarToolButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    std::function<void()> onClick
) {
    constexpr float kSide = 40.f;
    auto* host = CCNode::create();
    host->setContentSize({kSide, kSide});
    host->setAnchorPoint({0.5f, 0.5f});

    // Plate — same family as vanilla edit-bar tools / Tinker rebuildButtons.
    CCSprite* bg = paimon::SpriteHelper::safeCreate("GJ_button_04.png");
    if (!bg) bg = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_04.png");
    if (!bg) bg = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_05.png");
    if (bg) {
        auto bs = bg->getContentSize();
        float sc = (bs.width > 1.f) ? (kSide / bs.width) : 1.f;
        bg->setScale(sc);
        bg->setPosition({kSide / 2.f, kSide / 2.f});
        host->addChild(bg, 0);
    }

    if (auto* icon = loadIcon(preferredPaim, fallbacks, 1.f)) {
        float maxDim = std::max(icon->getContentSize().width, icon->getContentSize().height);
        if (maxDim > 1.f) icon->setScale(std::min(0.72f, 26.f / maxDim));
        icon->setPosition({kSide / 2.f, kSide / 2.f});
        host->addChild(icon, 1);
    }

    return CCMenuItemExt::createSpriteExtra(
        host, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
}

namespace {

CCNode* makeTabFace(CCSprite* icon, bool on) {
    auto* bg = paimon::SpriteHelper::safeCreateWithFrameName(
        on ? "GJ_tabOn_001.png" : "GJ_tabOff_001.png"
    );
    if (!bg) {
        // Fallback plate when tab textures missing
        auto* btn = ButtonSprite::create(
            icon ? icon : CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"),
            36, true, 36,
            on ? "GJ_button_02.png" : "GJ_button_05.png", 0.55f
        );
        return btn;
    }
    if (icon) {
        auto bgSize = bg->getContentSize();
        float maxDim = std::max(icon->getContentSize().width, icon->getContentSize().height);
        if (maxDim > 1.f) {
            icon->setScale(std::min(1.f, bgSize.height * 0.62f / maxDim));
        }
        icon->setPosition({bgSize.width / 2.f, bgSize.height / 2.f});
        if (!on) {
            icon->setColor({140, 140, 140});
            icon->setOpacity(200);
        }
        bg->addChild(icon);
    }
    return bg;
}

} // namespace

CCMenuItemToggler* tabStyleToggler(
    CCSprite* iconOn,
    CCSprite* iconOff,
    std::function<void(bool toggledOn)> onToggle
) {
    auto* onBg = makeTabFace(iconOn, true);
    auto* offBg = makeTabFace(iconOff, false);
    if (!onBg || !offBg) return nullptr;
    auto* tog = CCMenuItemExt::createToggler(
        onBg, offBg,
        [cb = std::move(onToggle)](CCMenuItemToggler* self) {
            if (cb) cb(!self->isToggled());
        }
    );
    return tog;
}

} // namespace paimon::editor::assets
