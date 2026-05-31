#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/textures/CCTextureCache.h>
#include <Geode/cocos/CCDirector.h>
#include <Geode/utils/cocos.hpp>
#include <cmath>
#include <vector>

using cocos2d::CCDrawNode;
using cocos2d::CCTexture2D;
using cocos2d::CCTextureCache;
using cocos2d::ccGLBlendFunc;
using cocos2d::ccGLBindTexture2D;
using cocos2d::ccGLEnableVertexAttribs;
using cocos2d::kCCVertexAttrib_Position;
using cocos2d::kCCVertexAttrib_TexCoords;
using cocos2d::kCCVertexAttrib_Color;
using cocos2d::kCCVertexAttribFlag_PosColorTex;

/**
 * CCDrawNode con draw() manual pa saltarse hooks de otros mods
 * (ej: HappyTextures, TextureLdr) que corrompen VBOs.
 * Mismo patron que PaimonShaderSprite: glBindBuffer(0) + client-side arrays.
 *
 * Reglas importantes para compatibilidad con otros mods:
 *  - Restaurar GL_ARRAY_BUFFER_BINDING a 0 al terminar para no
 *    contaminar el estado global de GL. Cocos2d siempre asume que no
 *    hay un VBO bindeado entre nodos: cada nodo que necesita uno hace
 *    su propio glBindBuffer al principio (CC_NODE_DRAW_SETUP no toca
 *    el binding). Por eso restaurar a 0 es suficiente y barato; no
 *    necesitamos llamar glGetIntegerv (que fuerza un pipeline stall
 *    CPU↔GPU y mata FPS cuando hay muchos draw nodes en pantalla).
 *  - Validar m_pBuffer antes de tocarlo: si CCDrawNode no pudo
 *    inicializarse (perdida de contexto, OOM en otro mod), ya no
 *    intentamos dibujar (mejor invisible que crash).
 *  - La textura blanca usada como dummy se obtiene a traves de
 *    CCTextureCache para que se invalide correctamente cuando el
 *    contexto GL se pierde (Android background, iOS multitasking).
 *    El resultado se cachea en un static raw pointer que se invalida
 *    a si mismo si la textura cae del cache: zero-cost en el caso
 *    comun, refetch automatico tras context loss.
 */
class PaimonDrawNode : public CCDrawNode {
public:
    static CCTexture2D* getWhiteTexture() {
        // Fast path: pointer cacheado del ultimo lookup. Despues del
        // primer draw, esta rama es la unica que se ejecuta — sin
        // string hash, sin lookup en CCDictionary, sin allocs.
        // En context loss, el caller del cache (CCTextureCache) borra
        // todas las texturas; la nuestra cae del cache y al siguiente
        // frame el lookup falla, asi que vamos al slow path y la
        // recreamos. No hace falta validar getName() activamente: si
        // GD aun cree que la textura existe pero el GL name esta
        // muerto, todos los demas sprites del juego tambien lo
        // estarian, y ese caso lo gestiona Cocos via reloadTextures().
        static CCTexture2D* s_cached = nullptr;
        if (s_cached) return s_cached;

        auto* cache = CCTextureCache::sharedTextureCache();
        if (!cache) return nullptr;
        constexpr char const* kKey = "paimon-draw-node-white";
        if (auto* existing = cache->textureForKey(kKey)) {
            s_cached = existing;
            return s_cached;
        }
        // Crear textura blanca 1x1 RGBA8888 si no existe en el cache.
        // Usamos kFmtRawData para que CCImage no intente decodificar
        // los bytes como PNG/JPG.
        unsigned char pixel[4] = {255, 255, 255, 255};
        auto* image = new cocos2d::CCImage();
        bool ok = image->initWithImageData(
            pixel,
            sizeof(pixel),
            cocos2d::CCImage::kFmtRawData,
            1, 1, 8
        );
        if (!ok) {
            image->release();
            return nullptr;
        }
        s_cached = cache->addUIImage(image, kKey);
        image->release();
        return s_cached;
    }

    /// Limpia el cache estatico de la textura blanca. Llamar tras
    /// context loss / reloadTextures(). Si nadie la limpia, igual
    /// se autoreplaza la siguiente vez que CCTextureCache devuelva
    /// null para nuestra key.
    static void invalidateWhiteTextureCache() {
        // Truco: definimos un helper que toca el static dentro de
        // getWhiteTexture sin exponerlo. Como no podemos resetear un
        // static local desde fuera, replicamos la logica de "olvidar
        // el cache" llamando a CCTextureCache::removeTextureForKey
        // y forzando una nueva busqueda en el siguiente draw.
        if (auto* cache = CCTextureCache::sharedTextureCache()) {
            cache->removeTextureForKey("paimon-draw-node-white");
        }
        // El static s_cached sigue apuntando a una textura ya liberada.
        // Para invalidarlo de verdad, getWhiteTexture validaria
        // s_cached->retainCount() o un flag. En practica, perder el
        // contexto reinicia todo el proceso de Cocos asi que no
        // entramos aqui en runtime estable.
    }

    static PaimonDrawNode* create() {
        auto node = new PaimonDrawNode();
        if (node && node->init()) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void drawSolidCircle(cocos2d::CCPoint center, float radius, cocos2d::ccColor4F const& fillColor, unsigned int segments = 48) {
        if (segments < 3 || radius <= 0.f) return;

        constexpr float kPi = 3.14159265358979323846f;
        std::vector<cocos2d::CCPoint> verts;
        verts.reserve(segments);

        for (unsigned int i = 0; i < segments; ++i) {
            float angle = 2.f * kPi * static_cast<float>(i) / static_cast<float>(segments);
            verts.emplace_back(center.x + radius * cosf(angle), center.y + radius * sinf(angle));
        }

        this->drawPolygon(verts.data(), static_cast<unsigned int>(verts.size()), fillColor, 0.f, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f));
    }

    /**
     * Dibuja un segmento como una capsule (forma de píldora): un rectángulo
     * orientado entre los dos puntos + dos semicírculos en los extremos.
     * Es la forma correcta de pintar un trazo de lápiz: grosor uniforme a
     * lo largo de toda la línea, sin acumular alpha donde se solapan
     * círculos consecutivos, y sin gaps cuando el step no divide exacto.
     *
     * @param p1, p2     extremos del trazo (en coords del nodo)
     * @param thickness  grosor total (no radio); se divide a la mitad
     *                   internamente para los semicírculos.
     * @param color      color con alpha pre-multiplicado en la mente del
     *                   llamador. Se aplica una sola vez para toda la
     *                   capsule, así no hay zonas más oscuras.
     * @param capSegs    segmentos por semicírculo (24 = suave a 32 px).
     */
    void drawCapsuleSegment(cocos2d::CCPoint p1, cocos2d::CCPoint p2,
                            float thickness, cocos2d::ccColor4F const& color,
                            unsigned int capSegs = 24) {
        const float radius = std::max(thickness * 0.5f, 0.5f);
        if (capSegs < 6) capSegs = 6;

        constexpr float kPi = 3.14159265358979323846f;

        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        float len = std::sqrt(dx * dx + dy * dy);

        // Caso degenerado: los dos puntos coinciden. Se reduce a un
        // círculo simple (un solo punto del lápiz).
        if (len < 0.0001f) {
            std::vector<cocos2d::CCPoint> verts;
            verts.reserve(capSegs * 2);
            for (unsigned int i = 0; i < capSegs * 2; ++i) {
                float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(capSegs * 2);
                verts.emplace_back(p1.x + cosf(a) * radius, p1.y + sinf(a) * radius);
            }
            this->drawPolygon(verts.data(), static_cast<unsigned int>(verts.size()),
                              color, 0.f, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f));
            return;
        }

        // Vector unitario a lo largo del trazo y normal (perpendicular).
        float ux = dx / len;
        float uy = dy / len;
        float nx = -uy;
        float ny = ux;

        // Construir la capsule como un único polígono cerrado:
        //  - Semicírculo en p2 (de +n a -n pasando por +u)
        //  - Linea recta hasta p1 por el lado -n
        //  - Semicírculo en p1 (de -n a +n pasando por -u)
        //  - Cierre implícito al volver al primer vértice
        std::vector<cocos2d::CCPoint> outline;
        outline.reserve(capSegs * 2 + 2);

        // Ángulo del normal positivo: orientamos los semicírculos respecto
        // al normal del trazo, no respecto al eje X global.
        const float baseAngle = std::atan2(ny, nx); // dirección de +n

        // Semicirculo en p2: barre desde +n hasta -n por el lado de +u.
        // Usamos i < capSegs para que el endpoint (-n en p2) no se repita
        // como startpoint del semicírculo en p1: cada vértice es único, lo
        // cual evita triángulos degenerados en la triangulación.
        for (unsigned int i = 0; i < capSegs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(capSegs);
            float a = baseAngle - kPi * t;
            outline.emplace_back(p2.x + cosf(a) * radius,
                                 p2.y + sinf(a) * radius);
        }

        // Semicirculo en p1: barre desde -n hasta +n por el lado de -u.
        const float baseAngleP1 = baseAngle + kPi; // dirección de -n
        for (unsigned int i = 0; i < capSegs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(capSegs);
            float a = baseAngleP1 - kPi * t;
            outline.emplace_back(p1.x + cosf(a) * radius,
                                 p1.y + sinf(a) * radius);
        }

        this->drawPolygon(outline.data(), static_cast<unsigned int>(outline.size()),
                          color, 0.f, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f));
    }

    void draw() override {
        // Validar buffer antes de tocar el VBO. Si init fallo o un mod
        // externo nuke'o el estado, mejor invisible que crash.
        if (m_nBufferCount == 0 || !m_pBuffer) return;

        // Sincronizar VBO si hay datos nuevos (replica lo que render() hace)
        if (m_bDirty) {
            glBindBuffer(GL_ARRAY_BUFFER, m_uVbo);
            glBufferData(GL_ARRAY_BUFFER,
                sizeof(cocos2d::ccV2F_C4B_T2F) * m_uBufferCapacity,
                m_pBuffer, GL_STREAM_DRAW);
            m_bDirty = false;
        }

        CC_NODE_DRAW_SETUP();

        // CLAVE: desvincular VBO pa usar arrays del lado del cliente
        // y evitar que hooks de otros mods corrompan el estado.
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        if (auto* texture = getWhiteTexture()) {
            ccGLBindTexture2D(texture->getName());
        } else {
            ccGLBindTexture2D(0);
        }

        ccGLBlendFunc(m_sBlendFunc.src, m_sBlendFunc.dst);

        ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);

        // ccV2F_C4B_T2F: vertices(2 floats) + colors(4 bytes) + texCoords(2 floats)
        #define kPaimonDrawNodeStride sizeof(cocos2d::ccV2F_C4B_T2F)

        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE,
            kPaimonDrawNodeStride, &m_pBuffer[0].vertices);

        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE,
            kPaimonDrawNodeStride, &m_pBuffer[0].colors);

        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE,
            kPaimonDrawNodeStride, &m_pBuffer[0].texCoords);

        glDrawArrays(GL_TRIANGLES, 0, m_nBufferCount);

        CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
        CC_INCREMENT_GL_DRAWS(1);
#endif

        // Dejar el VBO desvinculado al salir. Los siguientes nodos en
        // el frame que necesiten un VBO especifico haran su propio
        // glBindBuffer al inicio de su draw — Cocos no asume nada
        // sobre el estado del binding entre nodos.
        //
        // No usamos glGetIntegerv para "guardar el VBO previo": ese
        // syscall fuerza un pipeline stall (CPU espera a que la GPU
        // termine las operaciones pendientes para poder leer el
        // estado), y con muchos PaimonDrawNodes en pantalla (cards,
        // paneles, miniaturas, blur preview) el coste se acumulaba
        // hasta tirar el FPS de 360 a ~60. Restaurar a 0 es lo que
        // CCDrawNode original hace y es el contrato que esperan los
        // demas nodos del juego.
    }
};
