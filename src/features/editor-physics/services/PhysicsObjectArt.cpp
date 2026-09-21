#include "PhysicsObjectArt.hpp"

#include <Geode/binding/GameObject.hpp>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

// Text and counter objects need a font texture handed to them, so they never go
// through the create-by-key path.
constexpr int kTextObjectID = 914;
constexpr int kCounterObjectID = 1615;

void applyTransform(GameObject* object, BodyVisual const& visual) {
    object->setRScaleX(visual.scaleX);
    object->setRScaleY(visual.scaleY);
    object->setRRotation(visual.rotation);
    object->setFlipX(visual.flipX);
    object->setFlipY(visual.flipY);
    object->setColor(visual.baseColor);
    object->setOpacity(visual.baseOpacity);
    if (auto* detail = object->m_colorSprite) {
        detail->setColor(visual.detailColor);
        detail->setOpacity(visual.detailOpacity);
    }
}

// The real art via the editor's own create path; detail and glow sprites belong
// to batch layers that only exist inside a level.
CCNode* spawnObject(BodyVisual const& visual) {
    if (visual.objectID <= 0 || visual.objectID == kTextObjectID ||
        visual.objectID == kCounterObjectID) {
        return nullptr;
    }
    auto* clone = GameObject::createWithKey(visual.objectID);
    if (!clone) return nullptr;

    if (auto* detail = clone->m_colorSprite; detail && !detail->getParent()) {
        clone->addColorSpriteToSelf();
    }
    if (auto* glow = clone->m_glowSprite; glow && !glow->getParent() && !clone->m_hasNoGlow) {
        clone->addChild(glow, -1);
    }
    applyTransform(clone, visual);
    return clone;
}

// Whatever the object draws on screen, sprite by sprite: GD keeps base, detail
// and glow sprites in sibling batch layers sharing the object's space.
CCNode* mirrorLiveArt(BodyVisual const& visual) {
    auto* object = visual.object;
    if (!object) return nullptr;

    auto* group = CCNode::create();
    auto const origin = object->getPosition();
    auto copySprite = [&](CCSprite* source, int order) {
        if (!source || !source->isVisible()) return;
        auto* frame = source->displayFrame();
        auto* copy = frame ? CCSprite::createWithSpriteFrame(frame) : nullptr;
        if (!copy) return;
        auto const position = source == object ? origin : source->getPosition();
        copy->setPosition(position - origin);
        copy->setScaleX(source->getScaleX());
        copy->setScaleY(source->getScaleY());
        copy->setRotation(source->getRotation());
        copy->setFlipX(source->isFlipX());
        copy->setFlipY(source->isFlipY());
        copy->setColor(source->getColor());
        copy->setOpacity(source->getOpacity());
        copy->setBlendFunc(source->getBlendFunc());
        group->addChild(copy, order);
    };

    copySprite(object->m_glowSprite, -1);
    copySprite(object, 0);
    copySprite(object->m_colorSprite, 1);
    return group->getChildrenCount() > 0 ? group : nullptr;
}

// Last resort: the main frame stretched over the hitbox.
CCNode* stretchedArt(BodyVisual const& visual) {
    if (!visual.object) return nullptr;
    auto* frame = visual.object->displayFrame();
    if (!frame) return nullptr;
    auto* sprite = CCSprite::createWithSpriteFrame(frame);
    if (!sprite) return nullptr;
    auto const content = sprite->getContentSize();
    if (content.width <= 0.f || content.height <= 0.f) return nullptr;
    sprite->setScaleX(visual.size.x / content.width);
    sprite->setScaleY(visual.size.y / content.height);
    sprite->setRotation(visual.rotation);
    sprite->setFlipX(visual.flipX);
    sprite->setFlipY(visual.flipY);
    sprite->setColor(visual.baseColor);
    sprite->setOpacity(visual.baseOpacity);
    return sprite;
}

} // namespace

CCNode* buildObjectArt(BodyVisual const& visual) {
    if (auto* art = spawnObject(visual)) return art;
    if (auto* art = mirrorLiveArt(visual)) return art;
    return stretchedArt(visual);
}

} // namespace paimon::editorphysics
