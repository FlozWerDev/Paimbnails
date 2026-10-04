#include "ProfilePicRenderer.hpp"
#include "ProfilePicCustomizer.hpp"
#include "ProfileImageCache.hpp"
#include "ProfileShapes.hpp"
#include "HoverShaderSprite.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/loader/Mod.hpp>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::profile_pic {

namespace {
    bool resolveFileSource(std::string const& path, ResolvedProfilePhoto& out) {
        if (path.empty()) return false;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) return false;
        out.kind = ImageLoadHelper::isAnimatedImage(std::filesystem::path(path))
            ? ResolvedProfilePhoto::Kind::GifFile
            : ResolvedProfilePhoto::Kind::StaticFile;
        out.path = path;
        return true;
    }
}

ResolvedProfilePhoto resolveProfilePhoto(ProfilePicConfig const& cfg) {
    ResolvedProfilePhoto out;

    // the profile button has its own configuration and must not borrow the
    // profile popup's backdrop (profileimg / profile-bg-path).
    if (cfg.photoSource != "none" && !cfg.photoPath.empty() && resolveFileSource(cfg.photoPath, out)) {
        out.source = ResolvedProfilePhoto::Source::Custom;
        return out;
    }

    return out;
}

CCNode* createResolvedPhotoNode(ResolvedProfilePhoto const& photo) {
    using Kind = ResolvedProfilePhoto::Kind;

    auto loadStatic = [](std::string const& path) -> CCNode* {
        auto loaded = ImageLoadHelper::loadStaticImage(std::filesystem::path(path), 16);
        if (!loaded.success || !loaded.texture) return nullptr;
        auto* sprite = CCSprite::createWithTexture(loaded.texture);
        loaded.texture->release();
        return sprite;
    };

    switch (photo.kind) {
        case Kind::GifCacheKey:
            return AnimatedGIFSprite::createFromCache(photo.gifKey);
        case Kind::Texture:
            return photo.texture ? CCSprite::createWithTexture(photo.texture) : nullptr;
        case Kind::GifFile:
            if (auto* gif = AnimatedGIFSprite::create(photo.path)) return gif;
            // corrupt/unsupported animation: fall back to a static decode
            return loadStatic(photo.path);
        case Kind::StaticFile:
            return loadStatic(photo.path);
        default:
            return nullptr;
    }
}

void applyIconAnimation(SimplePlayer* player, int animType, float speed, float amount, float baseScale) {
    if (!player || animType == 0) return;

    player->stopAllActions();

    float duration = 1.f / speed;
    float scaleFactor = 1.f + 0.15f * amount;

    if (animType == 1) {
        auto scaleUp = CCScaleTo::create(duration * 0.5f, baseScale * scaleFactor);
        auto scaleDown = CCScaleTo::create(duration * 0.5f, baseScale / scaleFactor);
        auto bounce = CCRepeatForever::create(CCSequence::create(scaleUp, scaleDown, nullptr));
        player->runAction(bounce);
    }
    else if (animType == 2) {
        auto rotate = CCRotateBy::create(duration, 360.f * amount);
        auto spin = CCRepeatForever::create(rotate);
        player->runAction(spin);
    }
    else if (animType == 3) {
        auto scaleUp = CCScaleTo::create(duration * 0.5f, baseScale * scaleFactor);
        auto scaleDown = CCScaleTo::create(duration * 0.5f, baseScale / scaleFactor);
        auto rotate = CCRotateBy::create(duration, 360.f * amount);

        auto scaleSeq = CCRepeatForever::create(CCSequence::create(scaleUp, scaleDown, nullptr));
        auto spin = CCRepeatForever::create(rotate);

        player->runAction(scaleSeq);
        player->runAction(spin);
    }
    else if (animType == 4) {
        float flipDur = duration * 0.5f;
        auto toZero  = CCScaleTo::create(flipDur, 0.f, baseScale);
        auto toNeg   = CCScaleTo::create(flipDur, -baseScale, baseScale);
        auto toZero2 = CCScaleTo::create(flipDur, 0.f, baseScale);
        auto toPos   = CCScaleTo::create(flipDur, baseScale, baseScale);
        auto seq = CCRepeatForever::create(CCSequence::create(toZero, toNeg, toZero2, toPos, nullptr));
        player->runAction(seq);
    }
    else if (animType == 5) {
        float flipDur = duration * 0.5f;
        auto toZero  = CCScaleTo::create(flipDur, baseScale, 0.f);
        auto toNeg   = CCScaleTo::create(flipDur, baseScale, -baseScale);
        auto toZero2 = CCScaleTo::create(flipDur, baseScale, 0.f);
        auto toPos   = CCScaleTo::create(flipDur, baseScale, baseScale);
        auto seq = CCRepeatForever::create(CCSequence::create(toZero, toNeg, toZero2, toPos, nullptr));
        player->runAction(seq);
    }
    else if (animType == 6) {
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

    auto container = CCNode::create();
    container->setContentSize({targetSize, targetSize});
    container->setAnchorPoint({0.5f, 0.5f});
    container->ignoreAnchorPointForPosition(false);
    container->setID("paimon-profile-container"_spr);

    if (cfg.onlyIconMode) {
        auto* gm = GameManager::sharedState();
        if (!gm) return container;

        if (cfg.iconConfig.iconImageEnabled && !cfg.iconConfig.iconImagePath.empty()) {
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

        int iconId = cfg.iconConfig.iconId;
        int iconType = cfg.iconConfig.iconType;

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
            if (iconType > 0) {
                player->updatePlayerFrame(iconId, static_cast<IconType>(iconType));
            }

            if (cfg.iconConfig.colorSource == IconColorSource::Player) {
                player->setColor(gm->colorForIdx(gm->m_playerColor));
                player->setSecondColor(gm->colorForIdx(gm->m_playerColor2));
            } else {
                player->setColor(cfg.iconConfig.color1);
                player->setSecondColor(cfg.iconConfig.color2);
            }

            player->updateColors();

            if (cfg.iconConfig.glowEnabled) {
                if (cfg.iconConfig.glowColorSource == IconColorSource::Player) {
                    player->setGlowOutline(gm->colorForIdx(gm->m_playerColor));
                } else {
                    player->setGlowOutline(cfg.iconConfig.glowColor);
                }
            } else {
                player->disableGlowOutline();
            }

            float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
            float baseScale = 1.f;
            if (maxDim > 0) {
                baseScale = targetSize * 0.7f / maxDim;
            }
            float finalScale = baseScale * cfg.iconConfig.scale;
            player->setScale(finalScale);
            player->setPosition({targetSize / 2.f, targetSize / 2.f});
            container->addChild(player);

            applyIconAnimation(
                player,
                cfg.iconConfig.animationType,
                cfg.iconConfig.animationSpeed,
                cfg.iconConfig.animationAmount,
                finalScale
            );
        }

        if (cfg.frameEnabled) {
            auto border = paimon::profile_shapes::createOutline(
                paimon::profile_shapes::canonicalId(shapeName), targetSize,
                std::max(cfg.frame.thickness, 1.f), cfg.frame.color,
                static_cast<GLubyte>(std::clamp(cfg.frame.opacity, 0.f, 255.f))
            );
            if (border) {
                border->setID("paimon-profile-border"_spr);
                border->setAnchorPoint({0.5f, 0.5f});
                border->setPosition({targetSize / 2.f, targetSize / 2.f});
                container->addChild(border, -1);
            }
        }

        float sizeMul = std::clamp(cfg.size, 60.f, 200.f) / 120.f;
        float sx = std::clamp(cfg.scaleX, 0.2f, 3.0f);
        float sy = std::clamp(cfg.scaleY, 0.2f, 3.0f);
        container->setScaleX(sx * sizeMul);
        container->setScaleY(sy * sizeMul);
        container->setRotation(cfg.rotation);

        return container;
    }


    std::string canonShape = paimon::profile_shapes::canonicalId(shapeName);

    auto stencil = paimon::profile_shapes::createFill(canonShape, targetSize);
    if (!stencil) stencil = paimon::profile_shapes::createFill("circle", targetSize);
    if (!stencil) return nullptr;
    stencil->setPosition({0, 0});

    auto clipper = CCClippingNode::create();
    clipper->setStencil(stencil);
    clipper->setAlphaThreshold(0.5f);
    clipper->setContentSize({targetSize, targetSize});
    clipper->setID("paimon-profile-clipper"_spr);

    // single transform shared by stencil, image and border: scaling the
    // container moves all three together, so they never drift apart.
    auto shapeNode = CCNode::create();
    shapeNode->setContentSize({targetSize, targetSize});
    shapeNode->setAnchorPoint({0.5f, 0.5f});
    shapeNode->ignoreAnchorPointForPosition(false);
    shapeNode->setPosition({targetSize / 2.f, targetSize / 2.f});
    shapeNode->setID("paimon-profile-shape"_spr);

    CCNode* finalImage = imageNode;
    if (imageNode && cfg.hoverShader != "none" && !cfg.hoverShader.empty()) {
        if (auto* spr = typeinfo_cast<CCSprite*>(imageNode)) {
            if (auto* tex = spr->getTexture()) {
                if (auto* hover = HoverShaderSprite::createWithTexture(
                        tex, cfg.hoverShader, std::clamp(cfg.hoverIntensity, 0.f, 2.f))) {
                    hover->setTextureRect(spr->getTextureRect());
                    hover->setHoverSource(shapeNode);
                    finalImage = hover;
                }
            }
        }
    }

    if (finalImage) {
        float iw = std::max(finalImage->getContentWidth(), 1.f);
        float ih = std::max(finalImage->getContentHeight(), 1.f);
        float baseScale = std::max(targetSize / iw, targetSize / ih);

        float zoom = std::clamp(cfg.imageZoom, 0.5f, 3.0f);
        float imgScale = baseScale * zoom;
        finalImage->setScaleX(imgScale * (cfg.imageFlipX ? -1.f : 1.f));
        finalImage->setScaleY(imgScale * (cfg.imageFlipY ? -1.f : 1.f));
        finalImage->setRotation(cfg.imageRotation);
        if (auto* rgba = typeinfo_cast<CCSprite*>(finalImage)) {
            rgba->setOpacity(static_cast<GLubyte>(std::clamp(cfg.imageOpacity, 0.f, 255.f)));
        }
        finalImage->setAnchorPoint({0.5f, 0.5f});
        finalImage->ignoreAnchorPointForPosition(false);
        finalImage->setPosition({
            targetSize / 2.f + cfg.imageOffsetX,
            targetSize / 2.f + cfg.imageOffsetY
        });
        clipper->addChild(finalImage);
    } else {
        auto placeholder = paimon::SpriteHelper::createColorPanel(targetSize, targetSize, {40, 40, 40}, 220, 0.f);
        clipper->addChild(placeholder);
    }

    shapeNode->addChild(clipper);

    if (cfg.frameEnabled) {
        // same box and silhouette as the stencil: the stroke rides the clip
        // edge instead of a separate larger outline.
        auto border = paimon::profile_shapes::createOutline(
            canonShape, targetSize,
            std::max(cfg.frame.thickness, 1.f), cfg.frame.color,
            static_cast<GLubyte>(std::clamp(cfg.frame.opacity, 0.f, 255.f))
        );
        if (border) {
            border->setID("paimon-profile-border"_spr);
            border->setAnchorPoint({0.5f, 0.5f});
            border->setPosition({targetSize / 2.f, targetSize / 2.f});
            shapeNode->addChild(border, 1);
        }
    }

    container->addChild(shapeNode);

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

        float dx = targetSize / 2.f + deco.posX * (targetSize / 2.f);
        float dy = targetSize / 2.f + deco.posY * (targetSize / 2.f);
        decoSpr->setPosition({dx, dy});
        decoSpr->setZOrder(deco.zOrder + 10);
        container->addChild(decoSpr, deco.zOrder + 10);
    }

    float sizeMul = std::clamp(cfg.size, 60.f, 200.f) / 120.f;
    float sx = std::clamp(cfg.scaleX, 0.2f, 3.0f);
    float sy = std::clamp(cfg.scaleY, 0.2f, 3.0f);
    container->setScaleX(sx * sizeMul);
    container->setScaleY(sy * sizeMul);
    container->setRotation(cfg.rotation);

    return container;
}

}
