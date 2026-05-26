#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/ui/NineSlice.hpp>
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

    // ─────────────────────────────────────────────────────────────────────
    // Wrapper seguro para `geode::NineSlice` — la primitiva 9-slice nativa
    // de Geode. Es la que usan tanto el loader (`Popup`, `ListBorders`,
    // `MDPopup`, `Notification`, etc.) como mods de referencia para texture
    // packs y UI (`geode-sdk/textureldr`, `Alphalaneous/HappyTextures`),
    // asi que es lo MAS compatible cuando un usuario tiene HappyTextures
    // o un texture pack agresivo activo.
    //
    // Devuelve nullptr si:
    //  - el sprite frame no existe en la cache
    //  - el frame es el fallback magenta de Geode 5.4
    //  - la textura subyacente es un placeholder 2x2
    //
    // Asi el llamador puede encadenar fallbacks (NineSlice -> CCScale9Sprite
    // -> CCDrawNode rounded rect) sin que el fallback de Geode rompa la
    // cadena por hacernos creer que el asset existe.
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

    // Variante de `safeCreateNineSlice` que carga desde archivo (no desde
    // sprite frame cache). Para `square02b_001.png` y similares, que son
    // recursos sueltos de GD que NO siempre estan en la cache de frames
    // pero si en la cache de texturas.
    //
    // Es la primitiva que usa `geode-sdk/textureldr` para los fondos de
    // sus listas (`PackSelectPopup`) y la que `Geode::Popup` usa por
    // defecto para los `m_bgSprite` de sus popups.
    static geode::NineSlice* safeCreateNineSliceFromFile(
        const char* file,
        geode::NineSlice::Insets const& insets = {}
    ) {
        // addImage con forceReload=false es cache-friendly: si la textura
        // ya esta cargada, retorna el handle existente; si no, intenta
        // cargarla. Devuelve null solo si el archivo no existe.
        auto* tex = cocos2d::CCTextureCache::sharedTextureCache()->addImage(file, false);
        if (!tex) return nullptr;
        auto px = tex->getContentSizeInPixels();
        if (px.width <= 2.f && px.height <= 2.f) return nullptr;
        return geode::NineSlice::create(file, {}, insets);
    }

    // Crea un rectangulo redondeado con CCDrawNode (geometria pura).
    // No depende de texturas del juego ni de CCScale9Sprite.
    // Inmune a HappyTextures y actualizaciones de GD.
    //
    // Render:
    //  - Fill: un único triangle fan del polígono completo (no produce
    //    artefactos en la unión entre arcos).
    //  - Border: cuando borderWidth > 0 y borderColor.a > 0 dibujamos un
    //    anillo cerrado entre dos contornos concéntricos (uno expandido
    //    `borderWidth/2` hacia fuera y otro contraído lo mismo hacia
    //    dentro). Eso garantiza grosor uniforme en las esquinas y elimina
    //    las uniones quad-a-quad de drawPolygon que daban el efecto de
    //    "diente de sierra" y "líneas más gordas que otras".
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

        const bool hasVisibleBorder = borderColor.a > 0.001f && borderWidth > 0.0f;

        // Caso degenerado: sin radio o forma demasiado fina. Cuatro vertices
        // y se acabó. Aquí sí dejamos que CCDrawNode pinte el borde porque
        // no hay arcos donde se note el problema.
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

        // Mas segmentos por esquina => contorno mas suave. 14 puntos por
        // 90deg da una linea que a tamaños UI tipicos se ve continua.
        constexpr int kSegments = 14;
        std::vector<cocos2d::CCPoint> outline;
        outline.reserve(4 * kSegments);

        // Genera kSegments puntos por arco (sin incluir el endpoint, que
        // es exactamente el startpoint del siguiente arco). Si dejásemos
        // i <= kSegments crearíamos vértices duplicados consecutivos en
        // las esquinas, que la triangulación de CCDrawNode interpreta
        // como triángulos degenerados y a veces emite un quad fantasma
        // detrás de la forma.
        auto addArc = [&](float cx, float cy, float startAngle, float r) {
            for (int i = 0; i < kSegments; ++i) {
                float angle = startAngle + (static_cast<float>(M_PI) * 0.5f) *
                    (static_cast<float>(i) / static_cast<float>(kSegments));
                outline.push_back(ccp(cx + cosf(angle) * r, cy + sinf(angle) * r));
            }
        };

        // Contorno completo cerrado. esquinas: BL, BR, TR, TL
        addArc(radius, radius, static_cast<float>(M_PI), radius);
        addArc(width - radius, radius, static_cast<float>(M_PI) * 1.5f, radius);
        addArc(width - radius, height - radius, 0.f, radius);
        addArc(radius, height - radius, static_cast<float>(M_PI) * 0.5f, radius);

        // Fill como polígono cerrado. Pasamos borderWidth=0 para que
        // CCDrawNode no añada su propio stroke (lo dibujamos nosotros como
        // anillo más abajo).
        node->drawPolygon(outline.data(), static_cast<unsigned int>(outline.size()),
                          fillColor, 0.f, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f));

        if (hasVisibleBorder) {
            // Construir dos contornos concéntricos: outer (expandido
            // halfW hacia fuera) e inner (contraído halfW hacia dentro).
            // Después tejer un anillo de quads entre los dos. Resultado:
            // grosor uniforme y esquinas suaves sin picos ni gaps.
            const float halfW = borderWidth * 0.5f;
            const float outerR = radius + halfW;
            const float innerR = std::max(radius - halfW, 0.f);

            std::vector<cocos2d::CCPoint> outer;
            std::vector<cocos2d::CCPoint> inner;
            outer.reserve(4 * kSegments);
            inner.reserve(4 * kSegments);

            // Importante: usamos i < kSegments (no <=) por la misma razón
            // que en el contorno del fill — evitar vértices duplicados en
            // las uniones de los arcos que generen quads fantasma.
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

    // Panel oscuro con opacidad — el reemplazo "moderno" del antiguo
    // `CCScale9Sprite::create("square02_001.png") + setColor({0,0,0})`.
    //
    // Smart router (v2 — alineado con `geode-sdk/textureldr` y `Geode::Popup`):
    //   1. Si el panel es muy chico (separadores horizontales, swatches
    //      pequeños) o tiene radio cero (esquinas afiladas explicitas),
    //      caemos al CCDrawNode rounded rect: NineSlice añadiría esquinas
    //      redondeadas no deseadas y setColor sobre la textura
    //      `square02b_001.png` no daría un fill plano predecible para
    //      tamaños sub-30px.
    //   2. Si no, intentamos `geode::NineSlice::create("square02b_001.png")`
    //      con tinte. `square02b_001.png` es la cápsula 9-slice tintable
    //      que GD usa internamente y que tanto Geode (`MDTextArea`,
    //      `ModPopup`, `FiltersPopup`) como TextureLdr (`PackSelectPopup`)
    //      usan para sus paneles de fondo.
    //   3. Si NineSlice no carga (TexturePack agresivo), caemos a
    //      `CCScale9Sprite::create("square02b_001.png")`.
    //   4. Último recurso: el rounded rect pintado con CCDrawNode (legacy).
    //
    // Devuelve `cocos2d::CCNodeRGBA*` (base común de las tres
    // implementaciones — `CCDrawNode`, `CCScale9Sprite` y `NineSlice`
    // heredan todos de `CCNodeRGBA`). Eso permite que el caller llame
    // `setColor / setOpacity / setContentSize / setPosition / addChild`
    // sin saber qué primitiva concreta recibió. Los pocos miembros
    // tipados explícitamente como `Ref<CCDrawNode>` se ajustan a
    // `Ref<CCNode>` o `Ref<CCNodeRGBA>` (~2 casos aislados).
    static cocos2d::CCNodeRGBA* createDarkPanel(
        float width, float height,
        GLubyte opacity,
        float radius = 4.f
    ) {
        return createColorPanel(width, height, cocos2d::ccColor3B{0, 0, 0}, opacity, radius);
    }

    // Panel con color y opacidad customizados — misma estrategia que
    // `createDarkPanel`, ver doc de arriba.
    static cocos2d::CCNodeRGBA* createColorPanel(
        float width, float height,
        cocos2d::ccColor3B color,
        GLubyte opacity,
        float radius = 4.f
    ) {
        // ── 1. Smart routing: separadores y swatches mantienen CCDrawNode.
        // Lineas finas (height<6, p.ej. separadores) o paneles muy
        // pequeños (color picker swatches 18x18, badges 22x18) saldrian
        // mal con NineSlice porque sus esquinas dominarian el render.
        // Mismo criterio que `geode::Popup` aplica internamente para
        // decidir entre 9-slice y CCDrawNode.
        const bool tooSmall = (width < 30.f || height < 30.f);
        const bool tooThin  = (width < 8.f  || height < 8.f);
        const bool flat     = (radius < 0.5f);

        if (tooSmall || tooThin || flat) {
            cocos2d::ccColor4F fill = {
                color.r / 255.f, color.g / 255.f, color.b / 255.f, opacity / 255.f
            };
            return createRoundedRect(width, height, radius, fill);
        }

        // ── 2. NineSlice canónico (Geode + TextureLdr) ─────────────────
        if (auto* ns = safeCreateNineSliceFromFile("square02b_001.png")) {
            ns->setContentSize({width, height});
            ns->setAnchorPoint({0.f, 0.f});
            ns->setColor(color);
            ns->setOpacity(opacity);
            return ns;
        }

        // ── 3. Fallback CCScale9Sprite ─────────────────────────────────
        if (auto* s9 = safeCreateScale9("square02b_001.png")) {
            s9->setContentSize({width, height});
            s9->setAnchorPoint({0.f, 0.f});
            s9->setColor(color);
            s9->setOpacity(opacity);
            return s9;
        }

        // ── 4. Último recurso: CCDrawNode rounded rect ─────────────────
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
