#pragma once

// Shared drawing pieces for wheel, preview and config lists: one look everywhere.

#include <Geode/Geode.hpp>
#include "../data/QuickHubCategories.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>

namespace paimon::quickhub {

constexpr cocos2d::ccColor4F kRadialCardFill  = {0.05f, 0.06f, 0.10f, 0.94f};
constexpr cocos2d::ccColor4F kRadialHubFill   = {0.04f, 0.05f, 0.09f, 0.55f};
constexpr cocos2d::ccColor3B kRadialHintColor = {150, 160, 185};

inline cocos2d::ccColor4F accentColor(cocos2d::ccColor3B c, float alpha) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha};
}

// GD sprites run 20-120px; fitting to a fixed box keeps cards consistent.
inline cocos2d::CCSprite* makeFittedIcon(std::string const& frame, float box) {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame.c_str());
    if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
    if (!icon) return nullptr;

    auto size = icon->getContentSize();
    float longest = std::max(size.width, size.height);
    icon->setScale(longest > 0.f ? box / longest : 1.f);
    icon->setAnchorPoint({0.5f, 0.5f});
    return icon;
}

// TextureCache load (cache only), no new frames registered.
inline cocos2d::CCSprite* makeBadgeIcon(RadialOptionDef const& def, float box) {
    if (!def.imagePath.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(def.imagePath, ec) && !ec) {
            auto* tex = cocos2d::CCTextureCache::sharedTextureCache()->addImage(
                def.imagePath.c_str(), false);
            if (tex) {
                auto px = tex->getContentSizeInPixels();
                if (px.width > 2.f || px.height > 2.f) {
                    if (auto* spr = cocos2d::CCSprite::createWithTexture(tex)) {
                        auto size = spr->getContentSize();
                        float longest = std::max(size.width, size.height);
                        spr->setScale(longest > 0.f ? box / longest : 1.f);
                        spr->setAnchorPoint({0.5f, 0.5f});
                        return spr;
                    }
                }
            }
        }
    }
    return makeFittedIcon(def.icon, box);
}

inline float clampBadgeScale(float v) {
    return std::clamp(v, 0.2f, 3.f);
}

inline float wrapBadgeRotation(float v) {
    while (v > 180.f) v -= 360.f;
    while (v < -180.f) v += 360.f;
    return v;
}
// Rounded rect, not a fan: at the center vertex opposite edges leave CCDrawNode extrusion unfilled.
inline cocos2d::CCDrawNode* makeCircle(
    float radius,
    cocos2d::ccColor4F fill,
    cocos2d::ccColor4F border = {0.f, 0.f, 0.f, 0.f},
    float borderWidth = 0.f
) {
    auto* node = paimon::SpriteHelper::createRoundedRect(
        radius * 2.f, radius * 2.f, radius, fill, border, borderWidth);
    if (node) node->setPosition({-radius, -radius});
    return node;
}

struct RadialBadge {
    cocos2d::CCNode* root = nullptr;
    cocos2d::CCNode* ring = nullptr; // accent ring: aim only
};

// Flat disc with icon, centered on returned (0,0); zero size/anchor since centered anchors shift local origin.
inline RadialBadge makeRadialBadge(
    RadialOptionDef const& def,
    RadialButtonShape shape,
    float size,
    bool dimmed = false
) {
    using namespace cocos2d;

    RadialBadge badge;
    badge.root = CCNode::create();

    float radius = shape == RadialButtonShape::Square ? size * 0.24f : size * 0.5f;

    if (shape != RadialButtonShape::Icon) {
        auto fill = kRadialCardFill;
        if (dimmed) fill.a = 0.45f;

        if (auto* card = paimon::SpriteHelper::createRoundedRect(size, size, radius, fill)) {
            card->setPosition({-size * 0.5f, -size * 0.5f});
            badge.root->addChild(card, 0);
        }

        float ringSize = size + 7.f;
        float ringRadius = shape == RadialButtonShape::Square ? radius + 3.5f : ringSize * 0.5f;
        if (auto* ring = paimon::SpriteHelper::createRoundedRectOutline(
                ringSize, ringSize, ringRadius, accentColor(def.color, 0.95f), 1.6f)) {
            ring->setPosition({-ringSize * 0.5f, -ringSize * 0.5f});
            ring->setVisible(false);
            badge.root->addChild(ring, 1);
            badge.ring = ring;
        }
    }

    if (auto* icon = makeBadgeIcon(def, size * 0.58f)) {
        icon->setPosition({0.f, 0.f});
        icon->setOpacity(dimmed ? 130 : 235);
        icon->setScale(icon->getScale() * clampBadgeScale(def.imageScale));
        icon->setRotation(wrapBadgeRotation(def.imageRotation));
        icon->setFlipX(def.imageFlipX);
        icon->setFlipY(def.imageFlipY);
        badge.root->addChild(icon, 2);
    }

    return badge;
}

struct RadialGeometry {
    float radius = 90.f;
    float badgeSize = 48.f;
};

// Fits count badges on screen: largest radius that fits, then arc-sized badges.
inline RadialGeometry radialGeometryFor(int count, cocos2d::CCSize winSize) {
    constexpr float kMaxBadge = 48.f;
    constexpr float kMinRadius = 80.f;
    constexpr float kTwoPi = 2.f * static_cast<float>(M_PI);

    RadialGeometry geometry;
    float maxRadius = std::max(kMinRadius,
        std::min(winSize.width, winSize.height) * 0.5f - kMaxBadge * 0.75f - 10.f);

    float needed = (kMaxBadge + 14.f) * static_cast<float>(std::max(count, 1)) / kTwoPi;
    geometry.radius = std::min(std::max(needed, kMinRadius), maxRadius);

    // Accent ring overflows 6px, so per-badge gap discounts it plus neighbor air.
    float arc = count > 1 ? kTwoPi * geometry.radius / static_cast<float>(count) : kMaxBadge * 4.f;
    geometry.badgeSize = std::clamp(arc - 14.f, 26.f, kMaxBadge);
    return geometry;
}

// Item 1 on top, clockwise: reads like a list.
inline float radialAngleFor(int index, int count) {
    if (count <= 0) return 90.f;
    return 90.f - (360.f / static_cast<float>(count)) * static_cast<float>(index);
}

} // namespace paimon::quickhub
