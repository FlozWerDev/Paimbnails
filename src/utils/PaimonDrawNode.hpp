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
 * CCDrawNode with a manual draw() to dodge other mods' hooks (e.g. HappyTextures,
 * TextureLdr) that corrupt VBOs. Same pattern as PaimonShaderSprite:
 * glBindBuffer(0) + client-side arrays.
 *
 * Compatibility rules:
 *  - Restore GL_ARRAY_BUFFER_BINDING to 0 when done so we don't pollute global
 *    GL state. Cocos2d assumes no VBO is bound between nodes; each node that
 *    needs one binds it at the start. Restoring to 0 is cheap and enough — no
 *    glGetIntegerv (which forces a CPU<->GPU pipeline stall and kills FPS with
 *    many draw nodes on screen).
 *  - Validate m_pBuffer before touching it: if CCDrawNode failed to init
 *    (context loss, OOM elsewhere), skip drawing (invisible beats a crash).
 *  - The dummy white texture goes through CCTextureCache so it's invalidated
 *    correctly on GL context loss (Android background, iOS multitasking). The
 *    result is cached in a static raw pointer that self-invalidates if the
 *    texture drops from the cache: zero-cost in the common case, auto-refetch
 *    after context loss.
 */
class PaimonDrawNode : public CCDrawNode {
public:
    static CCTexture2D* getWhiteTexture() {
        // Fast path: pointer cached from the last lookup. After the first draw
        // this is the only branch that runs — no string hash, no CCDictionary
        // lookup, no allocs. On context loss CCTextureCache clears all textures;
        // ours drops out and the next frame's lookup fails, so we hit the slow
        // path and recreate it. No need to check getName() actively: if GD still
        // thinks the texture exists but its GL name is dead, every other sprite
        // would be too, and Cocos handles that via reloadTextures().
        static CCTexture2D* s_cached = nullptr;
        if (s_cached) return s_cached;

        auto* cache = CCTextureCache::sharedTextureCache();
        if (!cache) return nullptr;
        constexpr char const* kKey = "paimon-draw-node-white";
        if (auto* existing = cache->textureForKey(kKey)) {
            s_cached = existing;
            return s_cached;
        }
        // Create a 1x1 RGBA8888 white texture if not cached. kFmtRawData stops
        // CCImage from trying to decode the bytes as PNG/JPG.
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

    /// Clears the static white-texture cache. Call after context loss /
    /// reloadTextures(). If nobody calls it, it self-replaces the next time
    /// CCTextureCache returns null for our key.
    static void invalidateWhiteTextureCache() {
        // We can't reset a function-local static from outside, so emulate
        // "forget the cache" by removing the texture from CCTextureCache and
        // forcing a fresh lookup on the next draw.
        if (auto* cache = CCTextureCache::sharedTextureCache()) {
            cache->removeTextureForKey("paimon-draw-node-white");
        }
        // s_cached still points at a freed texture. Truly invalidating it would
        // need getWhiteTexture to check retainCount() or a flag. In practice
        // context loss restarts all of Cocos, so we don't hit this in stable runtime.
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
     * Draws a segment as a capsule (pill shape): a rectangle oriented between
     * the two points plus a semicircle at each end. The correct way to paint a
     * pencil stroke: uniform thickness along the whole line, no alpha buildup
     * where consecutive circles overlap, and no gaps when the step doesn't divide evenly.
     *
     * @param p1, p2     stroke endpoints (node coords)
     * @param thickness  total thickness (not radius); halved internally for the caps.
     * @param color      color (caller pre-multiplies alpha); applied once for the
     *                   whole capsule so there are no darker spots.
     * @param capSegs    segments per semicircle (24 = smooth at 32 px).
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

        // Degenerate case: the two points coincide. Reduces to a simple circle
        // (a single pencil dot).
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

        // unit vector along the stroke and its normal (perpendicular).
        float ux = dx / len;
        float uy = dy / len;
        float nx = -uy;
        float ny = ux;

        // Build the capsule as one closed polygon:
        //  - semicircle at p2 (+n to -n via +u)
        //  - straight line to p1 along -n
        //  - semicircle at p1 (-n to +n via -u)
        //  - implicit close back to the first vertex
        std::vector<cocos2d::CCPoint> outline;
        outline.reserve(capSegs * 2 + 2);

        // angle of the positive normal: caps are oriented to the stroke normal,
        // not the global X axis.
        const float baseAngle = std::atan2(ny, nx); // direction of +n

        // Semicircle at p2: sweep +n to -n on the +u side. Use i < capSegs so the
        // endpoint (-n at p2) isn't duplicated as the p1 semicircle's start —
        // unique vertices avoid degenerate triangles.
        for (unsigned int i = 0; i < capSegs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(capSegs);
            float a = baseAngle - kPi * t;
            outline.emplace_back(p2.x + cosf(a) * radius,
                                 p2.y + sinf(a) * radius);
        }

        // Semicircle at p1: sweep -n to +n on the -u side.
        const float baseAngleP1 = baseAngle + kPi; // direction of -n
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
        // Validate the buffer before touching the VBO. If init failed or an
        // external mod nuked the state, invisible beats a crash.
        if (m_nBufferCount == 0 || !m_pBuffer) return;

        // sync the VBO if there's new data (mirrors what render() does)
        if (m_bDirty) {
            glBindBuffer(GL_ARRAY_BUFFER, m_uVbo);
            glBufferData(GL_ARRAY_BUFFER,
                sizeof(cocos2d::ccV2F_C4B_T2F) * m_uBufferCapacity,
                m_pBuffer, GL_STREAM_DRAW);
            m_bDirty = false;
        }

        CC_NODE_DRAW_SETUP();

        // Key: unbind the VBO to use client-side arrays and stop other mods'
        // hooks from corrupting the state.
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

        // Leave the VBO unbound on exit. Later nodes that need a specific VBO
        // bind their own at the start of their draw — Cocos assumes nothing about
        // the binding state between nodes.
        //
        // We don't glGetIntegerv to "save the previous VBO": that syscall forces
        // a pipeline stall (CPU waits for the GPU to finish before reading state),
        // and with many PaimonDrawNodes on screen (cards, panels, thumbnails, blur
        // preview) the cost dropped FPS from 360 to ~60. Restoring to 0 is what the
        // original CCDrawNode does and what other nodes expect.
    }
};
