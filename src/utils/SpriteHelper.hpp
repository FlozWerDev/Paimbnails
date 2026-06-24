#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/ui/NineSlice.hpp>
#include "PaimonDrawNode.hpp"

namespace paimon {

// Validates sprites and keeps deterministic fallbacks.
// With Geode 5.4, createWithSpriteFrameName / create(file) may return a
// fallback sprite instead of nullptr when the asset is missing.
struct SpriteHelper {

    // Rectangular CCDrawNode stencil for CCClippingNode.
    // Avoids CCScale9Sprite (hooked by HappyTextures) and CCLayerColor
    // (anchorPoint issues); CCDrawNode is pure geometry.
    static cocos2d::CCDrawNode* createRectStencil(float width, float height) {
        auto stencil = PaimonDrawNode::create();
        cocos2d::CCPoint rect[4] = {
            ccp(0, 0),
            ccp(width, 0),
            ccp(width, height),
            ccp(0, height)
        };
        cocos2d::ccColor4F white = {1, 1, 1, 1};
        stencil->drawPolygon(rect, 4, white, 0, white);
        return stencil;
    }

    // Stencil with a diagonal left edge (parallelogram).
    // skewOffset: horizontal shift of the top-left edge.
    // Shape: (0,0)->(w,0)->(w,h)->(skew,h).
    static cocos2d::CCDrawNode* createDiagonalStencil(float width, float height, float skewOffset) {
        auto stencil = PaimonDrawNode::create();
        cocos2d::CCPoint poly[4] = {
            ccp(0, 0),
            ccp(width, 0),
            ccp(width, height),
            ccp(skewOffset, height)
        };
        cocos2d::ccColor4F white = {1, 1, 1, 1};
        stencil->drawPolygon(poly, 4, white, 0, white);
        return stencil;
    }

    // True if the sprite is usable by the mod.
    static bool isValidSprite(cocos2d::CCSprite* spr) {
        if (!spr) return false;
        if (spr->isUsingFallback()) return false;
        auto tex = spr->getTexture();
        if (!tex) return false;
        auto size = tex->getContentSizeInPixels();
        // cocos2d's placeholder texture is 2x2 magenta
        if (size.width <= 2.f && size.height <= 2.f) return false;
        return true;
    }

    // Safe createWithSpriteFrameName: returns null if the frame is missing or
    // if Geode returned a fallback sprite/frame.
    static cocos2d::CCSprite* safeCreateWithFrameName(const char* frameName) {
        auto frame = cocos2d::CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName);
        if (!frame || frame->isUsingFallback()) return nullptr;
        auto spr = cocos2d::CCSprite::createWithSpriteFrame(frame);
        if (!isValidSprite(spr)) return nullptr;
        return spr;
    }

    // Safe create(file): treats Geode's built-in fallback as a real failure so
    // callers can chain their own fallbacks.
    static cocos2d::CCSprite* safeCreate(const char* file) {
        auto spr = cocos2d::CCSprite::create(file);
        if (!isValidSprite(spr)) return nullptr;
        return spr;
    }

    // Safe CCScale9Sprite::create: returns null if the texture is missing.
    // CCScale9Sprite::create crashes internally instead of returning nullptr.
    static cocos2d::extension::CCScale9Sprite* safeCreateScale9(const char* file) {
        auto* tex = cocos2d::CCTextureCache::sharedTextureCache()->addImage(file, false);
        if (!tex) return nullptr;
        return cocos2d::extension::CCScale9Sprite::create(file);
    }

    // Safe sprite-frame variant; stops Geode 5.4's fallback from breaking the
    // mod's fallback chain.
    static cocos2d::extension::CCScale9Sprite* safeCreateScale9WithFrameName(const char* frameName) {
        auto frame = cocos2d::CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName);
        if (!frame || frame->isUsingFallback()) return nullptr;
        return cocos2d::extension::CCScale9Sprite::createWithSpriteFrame(frame);
    }

    // Safe wrapper for geode::NineSlice (the native 9-slice primitive used by
    // the loader and texture-pack mods, so it's the most compatible option).
    // Returns nullptr if the frame is missing, is Geode 5.4's magenta fallback,
    // or its texture is a 2x2 placeholder, so callers can chain fallbacks.
    static geode::NineSlice* safeCreateNineSlice(
        const char* frameName,
        geode::NineSlice::Insets const& insets = {10.f, 10.f, 10.f, 10.f}
    ) {
        auto* cache = cocos2d::CCSpriteFrameCache::sharedSpriteFrameCache();
        auto* frame = cache->spriteFrameByName(frameName);
        if (!frame || frame->isUsingFallback()) return nullptr;

        if (auto* tex = frame->getTexture()) {
            auto px = tex->getContentSizeInPixels();
            if (px.width <= 2.f && px.height <= 2.f) return nullptr;
        }

        return geode::NineSlice::createWithSpriteFrameName(frameName, insets);
    }

    // Like safeCreateNineSlice but loads from file (not the sprite-frame cache),
    // for loose GD resources like square02b_001.png that live in the texture
    // cache but not always the frame cache.
    static geode::NineSlice* safeCreateNineSliceFromFile(
        const char* file,
        geode::NineSlice::Insets const& insets = {}
    ) {
        // addImage(forceReload=false) is cache-friendly: reuses a loaded texture
        // or loads it; returns null only if the file doesn't exist.
        auto* tex = cocos2d::CCTextureCache::sharedTextureCache()->addImage(file, false);
        if (!tex) return nullptr;
        auto px = tex->getContentSizeInPixels();
        if (px.width <= 2.f && px.height <= 2.f) return nullptr;
        return geode::NineSlice::create(file, {}, insets);
    }

    // Rounded rect via CCDrawNode (pure geometry; no game textures or
    // CCScale9Sprite, immune to HappyTextures and GD updates).
    // Fill is a single triangle fan; the border (when borderWidth > 0 and
    // borderColor.a > 0) is a closed ring between two concentric outlines,
    // giving uniform corner thickness without the jagged drawPolygon seams.
    static cocos2d::CCDrawNode* createRoundedRect(
        float width, float height,
        float radius,
        cocos2d::ccColor4F fillColor,
        cocos2d::ccColor4F borderColor = {0, 0, 0, 0},
        float borderWidth = 0.5f
    ) {
        auto node = PaimonDrawNode::create();
        if (!node) return nullptr;

        // clamp radius to half the shortest side
        float maxR = std::min(width, height) * 0.5f;
        if (radius > maxR) radius = maxR;
        if (radius < 0.f) radius = 0.f;

        const bool hasVisibleBorder = borderColor.a > 0.001f && borderWidth > 0.0f;

        // Degenerate case: no radius or too thin. Four vertices; let CCDrawNode
        // paint the border here since there are no arcs to show seams.
        if (radius <= 0.f || std::min(width, height) <= 4.f) {
            cocos2d::CCPoint rect[4] = {
                ccp(0, 0), ccp(width, 0), ccp(width, height), ccp(0, height)
            };
            cocos2d::ccColor4F effectiveBorder = hasVisibleBorder
                ? borderColor
                : (borderWidth > 0.0f && borderWidth <= 0.5f ? fillColor : cocos2d::ccc4f(0, 0, 0, 0));
            node->drawPolygon(rect, 4, fillColor, hasVisibleBorder ? borderWidth : 0.f, effectiveBorder);
            node->setContentSize(cocos2d::CCSize(width, height));
            return node;
        }

        // More segments per corner => smoother outline. 14 points per 90deg
        // looks continuous at typical UI sizes.
        constexpr int kSegments = 14;
        std::vector<cocos2d::CCPoint> outline;
        outline.reserve(4 * kSegments);

        // kSegments points per arc, excluding the endpoint (the next arc's
        // start). Using i <= kSegments would duplicate corner vertices, which
        // CCDrawNode triangulates into degenerate tris and a stray quad.
        auto addArc = [&](float cx, float cy, float startAngle, float r) {
            for (int i = 0; i < kSegments; ++i) {
                float angle = startAngle + (static_cast<float>(M_PI) * 0.5f) *
                    (static_cast<float>(i) / static_cast<float>(kSegments));
                outline.push_back(ccp(cx + cosf(angle) * r, cy + sinf(angle) * r));
            }
        };

        // Full closed outline. Corners: BL, BR, TR, TL
        addArc(radius, radius, static_cast<float>(M_PI), radius);
        addArc(width - radius, radius, static_cast<float>(M_PI) * 1.5f, radius);
        addArc(width - radius, height - radius, 0.f, radius);
        addArc(radius, height - radius, static_cast<float>(M_PI) * 0.5f, radius);

        // Fill as a closed polygon; borderWidth=0 so CCDrawNode adds no stroke
        // (we draw the ring ourselves below).
        node->drawPolygon(outline.data(), static_cast<unsigned int>(outline.size()),
                          fillColor, 0.f, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f));

        if (hasVisibleBorder) {
            // Two concentric outlines: outer (expanded halfW) and inner
            // (contracted halfW), then weave a ring of quads between them for
            // uniform thickness and smooth corners.
            const float halfW = borderWidth * 0.5f;
            const float outerR = radius + halfW;
            const float innerR = std::max(radius - halfW, 0.f);

            std::vector<cocos2d::CCPoint> outer;
            std::vector<cocos2d::CCPoint> inner;
            outer.reserve(4 * kSegments);
            inner.reserve(4 * kSegments);

            // Use i < kSegments (not <=), same as the fill outline, to avoid
            // duplicate arc-join vertices that spawn stray quads.
            auto pushRingArcs = [&](std::vector<cocos2d::CCPoint>& dst, float rUsed) {
                auto fanArc = [&](float cx, float cy, float startAngle) {
                    for (int i = 0; i < kSegments; ++i) {
                        float angle = startAngle + (static_cast<float>(M_PI) * 0.5f) *
                            (static_cast<float>(i) / static_cast<float>(kSegments));
                        dst.push_back(ccp(cx + cosf(angle) * rUsed, cy + sinf(angle) * rUsed));
                    }
                };
                fanArc(radius, radius, static_cast<float>(M_PI));
                fanArc(width - radius, radius, static_cast<float>(M_PI) * 1.5f);
                fanArc(width - radius, height - radius, 0.f);
                fanArc(radius, height - radius, static_cast<float>(M_PI) * 0.5f);
            };

            pushRingArcs(outer, outerR);
            pushRingArcs(inner, innerR);

            const size_t n = outer.size();
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                cocos2d::CCPoint quad[4] = { inner[i], outer[i], outer[j], inner[j] };
                node->drawPolygon(quad, 4, borderColor, 0.f, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f));
            }
        }

        node->setContentSize(cocos2d::CCSize(width, height));
        return node;
    }

    // Rounded-corner stencil (opaque white) for CCClippingNode.
    // Pure geometry; immune to HappyTextures / TextureLdr.
    static cocos2d::CCDrawNode* createRoundedRectOutline(
        float width, float height,
        float radius,
        cocos2d::ccColor4F borderColor,
        float borderWidth = 1.5f
    ) {
        cocos2d::ccColor4F transparentFill = {0, 0, 0, 0};
        return createRoundedRect(width, height, radius, transparentFill, borderColor, borderWidth);
    }

    // Dark panel with opacity; modern replacement for the old
    // CCScale9Sprite("square02_001.png") + setColor({0,0,0}).
    // Routing: tiny/zero-radius panels use CCDrawNode; otherwise try
    // NineSlice(square02b_001.png), then CCScale9Sprite, then a CCDrawNode
    // rounded rect. Returns CCNodeRGBA* (common base of all three) so callers
    // can set color/opacity/size without knowing the concrete primitive.
    static cocos2d::CCNodeRGBA* createDarkPanel(
        float width, float height,
        GLubyte opacity,
        float radius = 4.f
    ) {
        return createColorPanel(width, height, cocos2d::ccColor3B{0, 0, 0}, opacity, radius);
    }

    // Color panel with custom color/opacity; same strategy as createDarkPanel.
    static cocos2d::CCNodeRGBA* createColorPanel(
        float width, float height,
        cocos2d::ccColor3B color,
        GLubyte opacity,
        float radius = 4.f
    ) {
        // 1. Routing: thin lines (separators) and small panels (swatches,
        // badges) keep CCDrawNode; NineSlice corners would dominate the render.
        const bool tooSmall = (width < 30.f || height < 30.f);
        const bool tooThin  = (width < 8.f  || height < 8.f);
        const bool flat     = (radius < 0.5f);

        if (tooSmall || tooThin || flat) {
            cocos2d::ccColor4F fill = {
                color.r / 255.f, color.g / 255.f, color.b / 255.f, opacity / 255.f
            };
            return createRoundedRect(width, height, radius, fill);
        }

        // 2. canonical NineSlice (Geode + TextureLdr)
        if (auto* ns = safeCreateNineSliceFromFile("square02b_001.png")) {
            ns->setContentSize({width, height});
            ns->setAnchorPoint({0.f, 0.f});
            ns->setColor(color);
            ns->setOpacity(opacity);
            return ns;
        }

        // 3. Fallback CCScale9Sprite
        if (auto* s9 = safeCreateScale9("square02b_001.png")) {
            s9->setContentSize({width, height});
            s9->setAnchorPoint({0.f, 0.f});
            s9->setColor(color);
            s9->setOpacity(opacity);
            return s9;
        }

        // 4. last resort: CCDrawNode rounded rect
        cocos2d::ccColor4F fill = {
            color.r / 255.f, color.g / 255.f, color.b / 255.f, opacity / 255.f
        };
        return createRoundedRect(width, height, radius, fill);
    }

    // Rounded-corner stencil (opaque white) for CCClippingNode.
    // Pure geometry; immune to HappyTextures / TextureLdr.
    static cocos2d::CCDrawNode* createRoundedRectStencil(
        float width, float height,
        float radius = 6.f
    ) {
        return createRoundedRect(width, height, radius, {1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.f);
    }
};

} // namespace paimon
