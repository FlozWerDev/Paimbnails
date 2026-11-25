#pragma once
#include "../services/TransitionEvaluation.hpp"
#include <Geode/Geode.hpp>

namespace paimon::transitions {
inline cocos2d::CCSprite* createSolidTransitionSprite() {
    // A sprite, rather than a layer, gives shapes the same alpha/rotate/scale
    // behavior as imported media without relying on a texture pack's artwork.
    constexpr unsigned char white[] = {255, 255, 255, 255};
    auto texture = geode::Ref<cocos2d::CCTexture2D>(new cocos2d::CCTexture2D());
    texture->release(); // Ref now owns the constructor's reference.
    if (!texture->initWithData(white, cocos2d::kCCTexture2DPixelFormat_RGBA8888, 1, 1, {1, 1})) return nullptr;
    cocos2d::ccTexParams parameters{GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT};
    texture->setTexParameters(&parameters);
    return cocos2d::CCSprite::createWithTexture(texture.data());
}
inline void applyTransitionVisual(cocos2d::CCNode* node, TransitionVisual const& visual, float scale = 1.f) {
    node->setPosition({visual.position.x, visual.position.y});
    node->setVisible(visual.visible);
    node->setScale(scale * visual.scale);
    node->setRotation(visual.rotation);
    node->setSkewX(visual.skew);
    cocos2d::CCRGBAProtocol* rgba = nullptr;
    if (auto* sprite = geode::cast::typeinfo_cast<cocos2d::CCSprite*>(node)) rgba = sprite;
    else if (auto* layer = geode::cast::typeinfo_cast<cocos2d::CCLayerRGBA*>(node)) rgba = layer;
    if (rgba) {
        rgba->setOpacity(static_cast<GLubyte>(std::clamp(visual.opacity, 0.f, 255.f)));
        rgba->setColor(cocos2d::ccColor3B{static_cast<GLubyte>(std::clamp(visual.r, 0, 255)),
            static_cast<GLubyte>(std::clamp(visual.g, 0, 255)), static_cast<GLubyte>(std::clamp(visual.b, 0, 255))});
    }
}
} // namespace paimon::transitions
