#include "ProfilePicRenderer.hpp"
#include "ProfilePicCustomizer.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::profile_pic {

// Helper function to create animated icon using CCActions (more optimized than custom update)
void applyIconAnimation(SimplePlayer* player, int animType, float speed, float amount, float baseScale) {
    if (!player || animType == 0) return;

    player->stopAllActions();

    float duration = 1.f / speed;
    float scaleFactor = 1.f + 0.15f * amount;

    if (animType == 1) {
        // Zoom only - use CCScaleTo with ping-pong
        auto scaleUp = CCScaleTo::create(duration * 0.5f, baseScale * scaleFactor);
        auto scaleDown = CCScaleTo::create(duration * 0.5f, baseScale / scaleFactor);
        auto bounce = CCRepeatForever::create(CCSequence::create(scaleUp, scaleDown, nullptr));
        player->runAction(bounce);
    }
    else if (animType == 2) {
        // Rotation only - continuous 360 rotation
        auto rotate = CCRotateBy::create(duration, 360.f * amount);
        auto spin = CCRepeatForever::create(rotate);
        player->runAction(spin);
    }
    else if (animType == 3) {
        // Both - combine scale and rotation
        auto scaleUp = CCScaleTo::create(duration * 0.5f, baseScale * scaleFactor);
        auto scaleDown = CCScaleTo::create(duration * 0.5f, baseScale / scaleFactor);
        auto rotate = CCRotateBy::create(duration, 360.f * amount);

        auto scaleSeq = CCRepeatForever::create(CCSequence::create(scaleUp, scaleDown, nullptr));
        auto spin = CCRepeatForever::create(rotate);

        player->runAction(scaleSeq);
        player->runAction(spin);
    }
    else if (animType == 4) {
        // Flip X - flip horizontally back and forth
        float flipDur = duration * 0.5f;
        auto toZero  = CCScaleTo::create(flipDur, 0.f, baseScale);
        auto toNeg   = CCScaleTo::create(flipDur, -baseScale, baseScale);
        auto toZero2 = CCScaleTo::create(flipDur, 0.f, baseScale);
        auto toPos   = CCScaleTo::create(flipDur, baseScale, baseScale);
        auto seq = CCRepeatForever::create(CCSequence::create(toZero, toNeg, toZero2, toPos, nullptr));
        player->runAction(seq);
    }
    else if (animType == 5) {
        // Flip Y - flip vertically back and forth
        float flipDur = duration * 0.5f;
        auto toZero  = CCScaleTo::create(flipDur, baseScale, 0.f);
        auto toNeg   = CCScaleTo::create(flipDur, baseScale, -baseScale);
        auto toZero2 = CCScaleTo::create(flipDur, baseScale, 0.f);
        auto toPos   = CCScaleTo::create(flipDur, baseScale, baseScale);
        auto seq = CCRepeatForever::create(CCSequence::create(toZero, toNeg, toZero2, toPos, nullptr));
        player->runAction(seq);
    }
    else if (animType == 6) {
        // Shake - rapid left-right rotation oscillation
        float shakeDeg = 15.f * amount;
        float shakeDur = duration * 0.15f;
        auto r1 = CCRotateTo::create(shakeDur,  shakeDeg);
        auto r2 = CCRotateTo::create(shakeDur, -shakeDeg);
        auto r3 = CCRotateTo::create(shakeDur,  shakeDeg * 0.5f);
        auto r4 = CCRotateTo::create(shakeDur, -shakeDeg * 0.5f);
        auto r5 = CCRotateTo::create(shakeDur, 0.f);
        auto pause = CCDelayTime::create(duration * 0.25f);
        auto seq = CCRepeatForever::create(CCSequence::create(r1, r2, r1, r2, r3, r4, r5, pause, nullptr));
        player->runAction(seq);
    }
    else if (animType == 7) {
        // Bounce - drop and bounce up
        float bounceAmt = 8.f * amount;
        float t = duration * 0.25f;
        auto down  = CCMoveBy::create(t,        {0.f, -bounceAmt});
        auto up1   = CCMoveBy::create(t * 0.5f, {0.f,  bounceAmt * 1.4f});
        auto down2 = CCMoveBy::create(t * 0.5f, {0.f, -bounceAmt * 0.4f});
        auto pause = CCDelayTime::create(t);
        auto seq = CCRepeatForever::create(CCSequence::create(down, up1, down2, pause, nullptr));
        player->runAction(seq);
    }
}

CCNode* composeProfilePicture(CCNode* imageNode, float targetSize, ProfilePicConfig const& cfg) {
    if (targetSize <= 0.f) targetSize = 48.f;

    std::string shapeName = cfg.stencilSprite;
    if (shapeName.empty()) shapeName = "circle";

    // Contenedor final
    auto container = CCNode::create();
    container->setContentSize({targetSize, targetSize});
    container->setAnchorPoint({0.5f, 0.5f});
    container->ignoreAnchorPointForPosition(false);
    container->setID("paimon-profile-container"_spr);

    // Only Icon Mode - mostrar icono del juego en lugar de imagen
    if (cfg.onlyIconMode) {
        auto* gm = GameManager::sharedState();
        if (!gm) return container;

        // Icon Image mode - mostrar icono sobre una imagen/GIF
        if (cfg.iconConfig.iconImageEnabled && !cfg.iconConfig.iconImagePath.empty()) {
            // Cargar imagen de fondo
            CCSprite* bgImage = CCSprite::create(cfg.iconConfig.iconImagePath.c_str());
            if (bgImage) {
                float maxDim = std::max(bgImage->getContentSize().width, bgImage->getContentSize().height);
                if (maxDim > 0) {
                    bgImage->setScale(targetSize / maxDim);
                }
                bgImage->setPosition({targetSize / 2.f, targetSize / 2.f});
                container->addChild(bgImage);
            }
        }

        // Obtener el ID del icono segun el tipo
        int iconId = cfg.iconConfig.iconId;
        int iconType = cfg.iconConfig.iconType;

        // Si no hay icono seleccionado, usar el del jugador actual
        if (iconId == 0) {
            switch (iconType) {
                case 1: iconId = gm->m_playerShip; break;
                case 2: iconId = gm->m_playerBall; break;
                case 3: iconId = gm->m_playerBird; break;
                case 4: iconId = gm->m_playerDart; break;
                case 5: iconId = gm->m_playerRobot; break;
                case 6: iconId = gm->m_playerSpider; break;
                case 7: iconId = gm->m_playerSwing; break;
                default: iconId = gm->m_playerFrame; break;
            }
        }

        auto* player = SimplePlayer::create(iconId);
        if (player) {
            // Aplicar tipo de icono
            if (iconType > 0) {
                player->updatePlayerFrame(iconId, static_cast<IconType>(iconType));
            }

            // Aplicar colores correctamente
            // Usar colores del jugador si esta seleccionado, o los colores custom
            if (cfg.iconConfig.colorSource == IconColorSource::Player) {
                player->setColor(gm->colorForIdx(gm->m_playerColor));
                player->setSecondColor(gm->colorForIdx(gm->m_playerColor2));
            } else {
                player->setColor(cfg.iconConfig.color1);
                player->setSecondColor(cfg.iconConfig.color2);
            }

            // Forzar actualizacion de colores
            player->updateColors();

            // Aplicar glow
            if (cfg.iconConfig.glowEnabled) {
                if (cfg.iconConfig.glowColorSource == IconColorSource::Player) {
                    player->setGlowOutline(gm->colorForIdx(gm->m_playerColor));
                } else {
                    player->setGlowOutline(cfg.iconConfig.glowColor);
                }
            } else {
                player->disableGlowOutline();
            }

            // Escalar al tamano objetivo
            float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
            float baseScale = 1.f;
            if (maxDim > 0) {
                baseScale = targetSize * 0.7f / maxDim;
            }
            float finalScale = baseScale * cfg.iconConfig.scale;
            player->setScale(finalScale);
            player->setPosition({targetSize / 2.f, targetSize / 2.f});
            container->addChild(player);

            // Apply CCAction-based animation (more optimized than custom update loop)
            applyIconAnimation(
                player,
                cfg.iconConfig.animationType,
                cfg.iconConfig.animationSpeed,
                cfg.iconConfig.animationAmount,
                finalScale
            );
        }

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

        // Aplica escala y rotacion
        float sx = std::clamp(cfg.scaleX, 0.2f, 3.0f);
        float sy = std::clamp(cfg.scaleY, 0.2f, 3.0f);
        container->setScaleX(sx);
        container->setScaleY(sy);
        container->setRotation(cfg.rotation);

        return container;
    }

    // Modo normal - mostrar imagen de perfil

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
