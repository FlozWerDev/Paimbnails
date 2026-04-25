#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include "PaimonDrawNode.hpp"

namespace paimon {

// Utilidad para validar sprites y mantener fallbacks deterministas.
// Con Geode 5.4, createWithSpriteFrameName / create(file) pueden devolver
// un fallback sprite en vez de nullptr cuando el asset falta.
struct SpriteHelper {

    // Crea un CCDrawNode rectangular para usar como stencil en CCClippingNode.
    // Evita usar CCScale9Sprite (hookeado por HappyTextures) o CCLayerColor
    // (problemas con anchorPoint en CCLayer). CCDrawNode es geometria pura.
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

    // Stencil con borde izquierdo diagonal (paralelogramo).
    // skewOffset: desplazamiento horizontal del borde superior-izquierdo.
    // Forma: (0,0)→(w,0)→(w,h)→(skew,h)  ← corte diagonal en la izquierda.
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

    // Verifica si un sprite es utilizable para el mod.
    static bool isValidSprite(cocos2d::CCSprite* spr) {
        if (!spr) return false;
        if (spr->isUsingFallback()) return false;
        auto tex = spr->getTexture();
        if (!tex) return false;
        auto size = tex->getContentSizeInPixels();
        // la textura placeholder de cocos2d es 2x2 magenta
        if (size.width <= 2.f && size.height <= 2.f) return false;
        return true;
    }

    // Wrapper seguro de createWithSpriteFrameName que retorna null si el frame
    // no existe o si Geode devolvio un fallback sprite/frame.
    static cocos2d::CCSprite* safeCreateWithFrameName(const char* frameName) {
        auto frame = cocos2d::CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName);
        if (!frame || frame->isUsingFallback()) return nullptr;
        auto spr = cocos2d::CCSprite::createWithSpriteFrame(frame);
        if (!isValidSprite(spr)) return nullptr;
        return spr;
    }

    // Wrapper seguro de create(file) que trata el fallback integrado de Geode
    // como un fallo real para poder encadenar fallbacks propios.
    static cocos2d::CCSprite* safeCreate(const char* file) {
        auto spr = cocos2d::CCSprite::create(file);
        if (!isValidSprite(spr)) return nullptr;
        return spr;
    }

    // Wrapper seguro de CCScale9Sprite::create que retorna null si la textura
    // no existe. CCScale9Sprite::create crashea internamente en vez de
    // retornar nullptr cuando el sprite no se encuentra.
    static cocos2d::extension::CCScale9Sprite* safeCreateScale9(const char* file) {
        auto* tex = cocos2d::CCTextureCache::sharedTextureCache()->addImage(file, false);
        if (!tex) return nullptr;
        return cocos2d::extension::CCScale9Sprite::create(file);
    }

    // Variante segura para sprite frames, evitando que el fallback de Geode 5.4
    // corte cadenas de fallback del mod.
    static cocos2d::extension::CCScale9Sprite* safeCreateScale9WithFrameName(const char* frameName) {
        auto frame = cocos2d::CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName);
        if (!frame || frame->isUsingFallback()) return nullptr;
        return cocos2d::extension::CCScale9Sprite::createWithSpriteFrame(frame);
    }

    // Crea un rectangulo redondeado con CCDrawNode (geometria pura).
    // No depende de texturas del juego ni de CCScale9Sprite.
    // Inmune a HappyTextures y actualizaciones de GD.
    static cocos2d::CCDrawNode* createRoundedRect(
        float width, float height,
        float radius,
        cocos2d::ccColor4F fillColor,
        cocos2d::ccColor4F borderColor = {0, 0, 0, 0},
        float borderWidth = 0.5f
    ) {
        auto node = PaimonDrawNode::create();
        if (!node) return nullptr;

        // clampear radio pa que no exceda la mitad del lado mas corto
        float maxR = std::min(width, height) * 0.5f;
        if (radius > maxR) radius = maxR;
        if (radius < 0.f) radius = 0.f;

        // Si borderColor es totalmente transparente y no se paso explicitamente (pero width > 0),
        // usar fillColor como borde (comportamiento original). Si width es 0, respetamos transparente.
        cocos2d::ccColor4F effectiveBorder = borderColor;
        if (effectiveBorder.a <= 0.f && borderWidth > 0.0f && borderWidth <= 0.5f) {
            effectiveBorder = fillColor;
        }

        // Rectangulo simple con 4 vertices cuando el radio es 0 o la forma
        // es demasiado fina para arcos (evita vertices degenerados que causan
        // artefactos de triangulacion en separadores de 1-2px)
        if (radius <= 0.f || std::min(width, height) <= 4.f) {
            cocos2d::CCPoint rect[4] = {
                ccp(0, 0), ccp(width, 0), ccp(width, height), ccp(0, height)
            };
            node->drawPolygon(rect, 4, fillColor, borderWidth, effectiveBorder);
            node->setContentSize(cocos2d::CCSize(width, height));
            return node;
        }

        constexpr int kSegments = 8; // puntos por esquina
        std::vector<cocos2d::CCPoint> pts;
        pts.reserve(4 * kSegments);

        auto addArc = [&](float cx, float cy, float startAngle) {
            for (int i = 0; i < kSegments; ++i) {
                float angle = startAngle + (static_cast<float>(M_PI) * 0.5f) *
                    (static_cast<float>(i) / static_cast<float>(kSegments));
                pts.push_back(ccp(cx + cosf(angle) * radius, cy + sinf(angle) * radius));
            }
        };

        // esquinas: bottom-left, bottom-right, top-right, top-left
        addArc(radius, radius, static_cast<float>(M_PI));                    // BL (180..270)
        addArc(width - radius, radius, static_cast<float>(M_PI) * 1.5f);    // BR (270..360)
        addArc(width - radius, height - radius, 0.f);                        // TR (0..90)
        addArc(radius, height - radius, static_cast<float>(M_PI) * 0.5f);   // TL (90..180)

        node->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()),
                          fillColor, borderWidth, effectiveBorder);

        node->setContentSize(cocos2d::CCSize(width, height));
        return node;
    }

    // Stencil con esquinas redondeadas (blanco opaco) para CCClippingNode.
    // Usa geometria pura — inmune a HappyTextures / TextureLdr.
    static cocos2d::CCDrawNode* createRoundedRectOutline(
        float width, float height,
        float radius,
        cocos2d::ccColor4F borderColor,
        float borderWidth = 1.5f
    ) {
        cocos2d::ccColor4F transparentFill = {0, 0, 0, 0};
        return createRoundedRect(width, height, radius, transparentFill, borderColor, borderWidth);
    }

    // Shortcut: panel oscuro con opacidad (reemplazo directo de
    // CCScale9Sprite::create("square02_001.png") + setColor({0,0,0}) + setOpacity).
    static cocos2d::CCDrawNode* createDarkPanel(
        float width, float height,
        GLubyte opacity,
        float radius = 4.f
    ) {
        cocos2d::ccColor4F fill = {0.f, 0.f, 0.f, opacity / 255.f};
        return createRoundedRect(width, height, radius, fill);
    }

    // Panel con color y opacidad customizados.
    static cocos2d::CCDrawNode* createColorPanel(
        float width, float height,
        cocos2d::ccColor3B color,
        GLubyte opacity,
        float radius = 4.f
    ) {
        cocos2d::ccColor4F fill = {
            color.r / 255.f, color.g / 255.f, color.b / 255.f, opacity / 255.f
        };
        return createRoundedRect(width, height, radius, fill);
    }

    // Stencil con esquinas redondeadas (blanco opaco) para CCClippingNode.
    // Usa geometria pura — inmune a HappyTextures / TextureLdr.
    static cocos2d::CCDrawNode* createRoundedRectStencil(
        float width, float height,
        float radius = 6.f
    ) {
        return createRoundedRect(width, height, radius, {1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.f);
    }
};

} // namespace paimon
