#include "ButtonSkinRender.hpp"

#include "../../../core/RuntimeLifecycle.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace paimon::buttonskin {

namespace {

// long side of the snapshot in points (x4 pixels on high textures): buttons stay sharp
// while big invisible hit areas don't eat vram.
constexpr float kMaxShapeSide = 160.f;
// slider drags bake a new texture per step; old ones only live while something draws them.
constexpr size_t kMaxBakes = 24;

struct BakeEntry {
    Ref<CCTexture2D> shape;  // pins the key's texture address while the entry lives
    Ref<CCTexture2D> baked;
};

std::unordered_map<std::string, BakeEntry>& bakeCache() {
    // never destroyed: releasing gl textures after the context is gone crashes on exit.
    static auto* cache = new std::unordered_map<std::string, BakeEntry>();
    return *cache;
}

std::string fillKey(ButtonSkin const& s) {
    return fmt::format("{}|{}|{}|{}|{:.4f}|{:.3f}|{:.4f}|{:.4f}|{}{}|{},{},{}|{},{},{}",
        static_cast<int>(s.fill), s.imagePath, static_cast<int>(s.fit), s.maskToShape,
        s.fillScale, s.fillRotation, s.fillOffsetX, s.fillOffsetY, s.fillFlipX, s.fillFlipY,
        s.fillColor.r, s.fillColor.g, s.fillColor.b, s.fillColorB.r, s.fillColorB.g, s.fillColorB.b);
}

CCTexture2D* loadImage(std::string const& path) {
    if (path.empty()) return nullptr;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return nullptr;
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(path.c_str(), false);
    if (!tex) return nullptr;
    auto px = tex->getContentSizeInPixels();
    return px.width > 2.f || px.height > 2.f ? tex : nullptr;
}

// a 1x256 ramp sprite instead of cclayercolor/gradient: texture studio repaints brown
// and orange layer colors, which would change the color the player picked.
CCSprite* makeRampFill(ccColor3B a, ccColor3B b, float degrees, CCSize box) {
    constexpr int kSteps = 256;
    std::vector<GLubyte> pixels(static_cast<size_t>(kSteps) * 4);
    auto mix = [](GLubyte from, GLubyte to, float t) {
        return static_cast<GLubyte>(std::lround(from + (static_cast<float>(to) - from) * t));
    };
    for (int i = 0; i < kSteps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSteps - 1);
        auto* px = &pixels[static_cast<size_t>(i) * 4];
        px[0] = mix(a.r, b.r, t);
        px[1] = mix(a.g, b.g, t);
        px[2] = mix(a.b, b.b, t);
        px[3] = 255;
    }

    auto* texture = new CCTexture2D();
    if (!texture->initWithData(pixels.data(), kCCTexture2DPixelFormat_RGBA8888, 1, kSteps,
                               CCSize(1.f, static_cast<float>(kSteps)))) {
        texture->release();
        return nullptr;
    }
    texture->autorelease();
    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);

    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return nullptr;
    auto size = sprite->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return nullptr;
    // row 0 (color A) shows on top; the diagonal keeps the box covered at any angle.
    float cover = std::sqrt(box.width * box.width + box.height * box.height) + 2.f;
    sprite->setScaleX(cover / size.width);
    sprite->setScaleY(cover / size.height);
    // positive angles turn A towards the left, like a counter-clockwise dial.
    sprite->setRotation(-degrees);
    sprite->setPosition({box.width * 0.5f, box.height * 0.5f});
    return sprite;
}

// paint laid out in a w x h box (texture space, y up).
CCNode* makeFillNode(ButtonSkin const& s, CCSize box) {
    switch (s.fill) {
        case SkinFill::Image: {
            auto* tex = loadImage(s.imagePath);
            auto* sprite = tex ? CCSprite::createWithTexture(tex) : nullptr;
            if (!sprite) return nullptr;
            auto size = sprite->getContentSize();
            if (size.width <= 0.f || size.height <= 0.f) return nullptr;
            float sx = box.width / size.width;
            float sy = box.height / size.height;
            if (s.fit == SkinFit::Cover) sx = sy = std::max(sx, sy);
            else if (s.fit == SkinFit::Contain) sx = sy = std::min(sx, sy);
            sprite->setScaleX(sx * s.fillScale);
            sprite->setScaleY(sy * s.fillScale);
            sprite->setRotation(s.fillRotation);
            sprite->setFlipX(s.fillFlipX);
            sprite->setFlipY(s.fillFlipY);
            sprite->setPosition({box.width * (0.5f + s.fillOffsetX),
                                 box.height * (0.5f + s.fillOffsetY)});
            return sprite;
        }
        case SkinFill::Color:
            return makeRampFill(s.fillColor, s.fillColor, 0.f, box);
        case SkinFill::Gradient:
            return makeRampFill(s.fillColor, s.fillColorB, s.fillRotation, box);
        default:
            return nullptr;
    }
}

} // namespace

AssetShape snapshotAsset(CCNode* node) {
    AssetShape out;
    if (!node || paimon::isRuntimeShuttingDown()) return out;

    auto size = node->getContentSize();
    if (!(size.width >= 1.f && size.height >= 1.f) || size.width > 4096.f || size.height > 4096.f) {
        return out;
    }
    float res = std::min(1.f, kMaxShapeSide / std::max(size.width, size.height));
    int w = std::max(1, static_cast<int>(std::ceil(size.width * res)));
    int h = std::max(1, static_cast<int>(std::ceil(size.height * res)));

    // stencil kept: assets built with clipping nodes still render right.
    Ref<CCRenderTexture> rt = CCRenderTexture::create(
        w, h, kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
    if (!rt || !rt->getSprite() || !rt->getSprite()->getTexture()) return out;

    // pose the node so its content box fills the texture, then put everything back.
    auto const position = node->getPosition();
    float const scaleX = node->getScaleX();
    float const scaleY = node->getScaleY();
    float const rotationX = node->getRotationX();
    float const rotationY = node->getRotationY();
    float const skewX = node->getSkewX();
    float const skewY = node->getSkewY();
    bool const visible = node->isVisible();

    auto anchor = node->getAnchorPoint();
    node->setScaleX(static_cast<float>(w) / size.width);
    node->setScaleY(static_cast<float>(h) / size.height);
    node->setRotationX(0.f);
    node->setRotationY(0.f);
    node->setSkewX(0.f);
    node->setSkewY(0.f);
    node->setPosition(node->isIgnoreAnchorPointForPosition()
        ? CCPointZero : ccp(anchor.x * static_cast<float>(w), anchor.y * static_cast<float>(h)));
    node->setVisible(true);

    // a fading asset (or parent) must not leave the shape half transparent or empty.
    auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node);
    GLubyte parentOpacity = 255;
    GLubyte const ownOpacity = rgba ? rgba->getOpacity() : 255;
    if (rgba) {
        auto* parentRgba = typeinfo_cast<CCRGBAProtocol*>(node->getParent());
        if (parentRgba && parentRgba->isCascadeOpacityEnabled()) {
            parentOpacity = parentRgba->getDisplayedOpacity();
        }
        rgba->setOpacity(255);
        rgba->updateDisplayedOpacity(255);
    }

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f, 1.f, 0);
    node->visit();
    rt->end();

    if (rgba) {
        rgba->setOpacity(ownOpacity);
        rgba->updateDisplayedOpacity(parentOpacity);
    }
    node->setPosition(position);
    node->setScaleX(scaleX);
    node->setScaleY(scaleY);
    node->setRotationX(rotationX);
    node->setRotationY(rotationY);
    node->setSkewX(skewX);
    node->setSkewY(skewY);
    node->setVisible(visible);

    out.texture = rt->getSprite()->getTexture();
    out.assetSize = size;
    out.textureSize = CCSize(static_cast<float>(w), static_cast<float>(h));
    return out;
}

CCTexture2D* bakeFill(ButtonSkin const& skin, AssetShape const& shape) {
    if (!shape || !hasFill(skin) || paimon::isRuntimeShuttingDown()) return nullptr;

    auto& cache = bakeCache();
    auto key = fmt::format("{}|{}", fillKey(skin), static_cast<void*>(shape.texture.data()));
    if (auto found = cache.find(key); found != cache.end()) return found->second.baked;

    int w = std::max(1, static_cast<int>(std::lround(shape.textureSize.width)));
    int h = std::max(1, static_cast<int>(std::lround(shape.textureSize.height)));
    auto* fill = makeFillNode(skin, CCSize(static_cast<float>(w), static_cast<float>(h)));
    if (!fill) return nullptr;

    Ref<CCRenderTexture> rt = CCRenderTexture::create(w, h, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt || !rt->getSprite() || !rt->getSprite()->getTexture()) return nullptr;

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
    fill->visit();
    if (skin.maskToShape) {
        // dst *= asset alpha: soft edges survive, unlike a stencil alpha test.
        if (auto* mask = makeShapeSprite(shape.texture)) {
            mask->setAnchorPoint({0.f, 0.f});
            mask->setPosition({0.f, 0.f});
            auto maskSize = mask->getContentSize();
            if (maskSize.width > 0.f && maskSize.height > 0.f) {
                mask->setScaleX(static_cast<float>(w) / maskSize.width);
                mask->setScaleY(static_cast<float>(h) / maskSize.height);
            }
            mask->setBlendFunc({GL_ZERO, GL_SRC_ALPHA});
            mask->visit();
        }
    }
    rt->end();

    Ref<CCTexture2D> baked = rt->getSprite()->getTexture();
    if (cache.size() >= kMaxBakes) cache.clear();
    cache[key] = {shape.texture, baked};
    return baked;
}

ccBlendFunc overlayBlend(SkinBlend blend) {
    switch (blend) {
        case SkinBlend::Multiply: return {GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA};
        case SkinBlend::Add:      return {GL_ONE, GL_ONE};
        case SkinBlend::Screen:   return {GL_ONE, GL_ONE_MINUS_SRC_COLOR};
        default:                  return {GL_ONE, GL_ONE_MINUS_SRC_ALPHA};
    }
}

CCSprite* makeShapeSprite(CCTexture2D* texture) {
    if (!texture) return nullptr;
    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return nullptr;
    sprite->setFlipY(true);
    sprite->setBlendFunc({GL_ONE, GL_ONE_MINUS_SRC_ALPHA});
    sprite->setOpacityModifyRGB(true);
    return sprite;
}

CCNode* buildSkinPreview(ButtonSkin const& skin, AssetShape const& shape, float box,
                         float pointScale, bool withTransform) {
    auto* root = CCNode::create();
    if (!shape) return root;

    auto tex = shape.textureSize;
    float longest = std::max({tex.width, tex.height, 1.f});
    // room for growing up to ~1.6x before the frame clips it.
    float fit = (withTransform ? 0.6f : 0.9f) * box / longest;

    auto* holder = CCNode::create();
    holder->setScale(fit * (withTransform ? skin.scale : 1.f));
    if (withTransform) {
        holder->setRotation(skin.rotation);
        float assetToTexture = tex.width / std::max(shape.assetSize.width, 1.f);
        float unit = fit * assetToTexture / std::max(pointScale, 0.01f);
        holder->setPosition({skin.offsetX * unit, skin.offsetY * unit});
    }
    root->addChild(holder);

    if (auto* base = makeShapeSprite(shape.texture)) {
        if (skin.tintEnabled) base->setColor(skin.tint);
        float opacity = skin.hideOriginal ? 0.f : skin.baseOpacity;
        base->setOpacity(static_cast<GLubyte>(std::lround(std::clamp(opacity, 0.f, 1.f) * 255.f)));
        holder->addChild(base, 0);
    }

    if (auto* baked = bakeFill(skin, shape)) {
        if (auto* overlay = makeShapeSprite(baked)) {
            overlay->setBlendFunc(overlayBlend(skin.blend));
            overlay->setOpacity(
                static_cast<GLubyte>(std::lround(std::clamp(skin.fillOpacity, 0.f, 1.f) * 255.f)));
            holder->addChild(overlay, 1);
        }
    }
    return root;
}

void clearRenderCaches() {
    bakeCache().clear();
}

} // namespace paimon::buttonskin
