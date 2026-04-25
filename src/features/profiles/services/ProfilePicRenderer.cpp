#include "ProfilePicRenderer.hpp"
#include "ProfilePicCustomizer.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::profile_pic {

CCNode* composeProfilePicture(CCNode* imageNode, float targetSize, ProfilePicConfig const& cfg) {
    if (targetSize <= 0.f) targetSize = 48.f;

    std::string shapeName = cfg.stencilSprite;
    if (shapeName.empty()) shapeName = "circle";

    // Forma del recorte
    auto stencil = createShapeStencil(shapeName, targetSize);
    if (!stencil) stencil = createShapeStencil("circle", targetSize);
    if (!stencil) return nullptr;
    stencil->setPosition({0, 0});

    auto clipper = CCClippingNode::create();
    clipper->setStencil(stencil);
    clipper->setAlphaThreshold(-1.0f);
    clipper->setContentSize({targetSize, targetSize});
    clipper->setID("paimon-profile-clipper"_spr);

    // Imagen dentro del recorte
    if (imageNode) {
        float iw = std::max(imageNode->getContentWidth(), 1.f);
        float ih = std::max(imageNode->getContentHeight(), 1.f);
        float baseScale = std::max(targetSize / iw, targetSize / ih);

        float zoom = std::clamp(cfg.imageZoom, 0.5f, 3.0f);
        imageNode->setScale(baseScale * zoom);
        imageNode->setRotation(cfg.imageRotation);
        imageNode->setAnchorPoint({0.5f, 0.5f});
        imageNode->ignoreAnchorPointForPosition(false);
        imageNode->setPosition({
            targetSize / 2.f + cfg.imageOffsetX,
            targetSize / 2.f + cfg.imageOffsetY
        });
        clipper->addChild(imageNode);
    } else {
        // Placeholder oscuro
        auto placeholder = paimon::SpriteHelper::createColorPanel(targetSize, targetSize, {40, 40, 40}, 220, 0.f);
        clipper->addChild(placeholder);
    }

    // Contenedor final
    auto container = CCNode::create();
    container->setContentSize({targetSize, targetSize});
    container->setAnchorPoint({0.5f, 0.5f});
    container->ignoreAnchorPointForPosition(false);
    container->setID("paimon-profile-container"_spr);
    container->addChild(clipper);

    // Borde con la misma forma
    if (cfg.frameEnabled) {
        float borderSize = targetSize + cfg.frame.thickness * 2.f;
        auto border = createShapeBorder(
            shapeName, borderSize,
            cfg.frame.thickness, cfg.frame.color,
            static_cast<GLubyte>(std::clamp(cfg.frame.opacity, 0.f, 255.f))
        );
        if (border) {
            border->setID("paimon-profile-border"_spr);
            border->setAnchorPoint({0.5f, 0.5f});
            border->setPosition({targetSize / 2.f, targetSize / 2.f});
            container->addChild(border, -1);
        }
    }

    // Decoraciones ordenadas por zOrder
    for (auto const& deco : cfg.decorations) {
        if (deco.spriteName.empty()) continue;
        CCSprite* decoSpr = paimon::SpriteHelper::safeCreateWithFrameName(deco.spriteName.c_str());
        if (!decoSpr) decoSpr = paimon::SpriteHelper::safeCreate(deco.spriteName.c_str());
        if (!decoSpr) continue;

        decoSpr->setAnchorPoint({0.5f, 0.5f});
        decoSpr->setScale(std::clamp(deco.scale, 0.1f, 3.0f));
        decoSpr->setRotation(deco.rotation);
        decoSpr->setColor(deco.color);
        decoSpr->setOpacity(static_cast<GLubyte>(std::clamp(deco.opacity, 0.f, 255.f)));
        decoSpr->setFlipX(deco.flipX);
        decoSpr->setFlipY(deco.flipY);

        // Posicion relativa: 0 = centro, 1 = borde
        float dx = targetSize / 2.f + deco.posX * (targetSize / 2.f);
        float dy = targetSize / 2.f + deco.posY * (targetSize / 2.f);
        decoSpr->setPosition({dx, dy});
        decoSpr->setZOrder(deco.zOrder + 10);
        container->addChild(decoSpr, deco.zOrder + 10);
    }

    // Aplica escala y rotacion
    float sx = std::clamp(cfg.scaleX, 0.2f, 3.0f);
    float sy = std::clamp(cfg.scaleY, 0.2f, 3.0f);
    container->setScaleX(sx);
    container->setScaleY(sy);
    container->setRotation(cfg.rotation);

    return container;
}

} // namespace paimon::profile_pic
