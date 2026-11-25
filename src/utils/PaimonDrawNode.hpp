#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/textures/CCTextureCache.h>
#include <Geode/cocos/CCDirector.h>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
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

/* client arrays: other mods corrupt vbo state, so gl_array_buffer stays
 * unbound and the draw buffer is revalidated each draw. */
class PaimonDrawNode : public CCDrawNode {
public:
    // retained 1x1 white texture; reset after gl context reloads.
    static inline CCTexture2D* s_cached = nullptr;

    static CCTexture2D* getWhiteTexture() {
        // fast path reuses the last lookup; context loss clears the cache.
        if (s_cached) return s_cached;

        auto* cache = CCTextureCache::sharedTextureCache();
        if (!cache) return nullptr;
        constexpr char const* kKey = "paimon-draw-node-white";
        // extra retain: removeunusedtextures() must not dangle the cached pointer.
        if (auto* existing = cache->textureForKey(kKey)) {
            existing->retain();
            s_cached = existing;
            return s_cached;
        }
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
        CC_SAFE_RETAIN(s_cached);
        return s_cached;
    }

    static void invalidateWhiteTextureCache() {
        if (auto* cache = CCTextureCache::sharedTextureCache()) {
            cache->removeTextureForKey("paimon-draw-node-white");
        }
        CC_SAFE_RELEASE(s_cached);
        s_cached = nullptr;
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

    // uniform capsule between two points, no gaps or alpha buildup.
    void drawCapsuleSegment(cocos2d::CCPoint p1, cocos2d::CCPoint p2,
                            float thickness, cocos2d::ccColor4F const& color,
                            unsigned int capSegs = 24) {
        const float radius = std::max(thickness * 0.5f, 0.5f);
        if (capSegs < 6) capSegs = 6;

        constexpr float kPi = 3.14159265358979323846f;

        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        float len = std::sqrt(dx * dx + dy * dy);

        // coincident points draw a single dot.
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

        float ux = dx / len;
        float uy = dy / len;
        float nx = -uy;
        float ny = ux;

        std::vector<cocos2d::CCPoint> outline;
        outline.reserve(capSegs * 2 + 2);

        const float baseAngle = std::atan2(ny, nx);

        for (unsigned int i = 0; i < capSegs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(capSegs);
            float a = baseAngle - kPi * t;
            outline.emplace_back(p2.x + cosf(a) * radius,
                                 p2.y + sinf(a) * radius);
        }

        const float baseAngleP1 = baseAngle + kPi;
        for (unsigned int i = 0; i < capSegs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(capSegs);
            float a = baseAngleP1 - kPi * t;
            outline.emplace_back(p1.x + cosf(a) * radius,
                                 p1.y + sinf(a) * radius);
        }

        this->drawPolygon(outline.data(), static_cast<unsigned int>(outline.size()),
                          color, 0.f, cocos2d::ccc4f(0.f, 0.f, 0.f, 0.f));
    }

    // Direct triangle append without CCDrawNode::drawPolygon miter extrusion.
    void drawTriangle(cocos2d::CCPoint p1, cocos2d::CCPoint p2, cocos2d::CCPoint p3,
                      cocos2d::ccColor4F const& color) {
        ensureCapacity(3);
        cocos2d::ccColor4B c4b = cocos2d::ccc4BFromccc4F(color);
        auto* v = m_pBuffer + m_nBufferCount;
        v[0] = {{p1.x, p1.y}, c4b, {0.5f, 0.5f}};
        v[1] = {{p2.x, p2.y}, c4b, {0.5f, 0.5f}};
        v[2] = {{p3.x, p3.y}, c4b, {0.5f, 0.5f}};
        m_nBufferCount += 3;
        m_bDirty = true;
    }

    // Triangulates a simple (convex or concave) polygon via ear-clipping without
    // Cocos2d-x border/AA miter extrusion, avoiding spikes on duplicate or sharp vertices.
    void drawSolidPolygon(std::vector<cocos2d::CCPoint> const& inputVerts,
                          cocos2d::ccColor4F const& color) {
        std::vector<cocos2d::CCPoint> pts;
        pts.reserve(inputVerts.size());
        constexpr float kEpsSq = 1e-6f;
        for (auto const& p : inputVerts) {
            if (!pts.empty()) {
                float dx = p.x - pts.back().x;
                float dy = p.y - pts.back().y;
                if (dx * dx + dy * dy < kEpsSq) continue;
            }
            pts.push_back(p);
        }
        while (pts.size() > 1) {
            float dx = pts.front().x - pts.back().x;
            float dy = pts.front().y - pts.back().y;
            if (dx * dx + dy * dy < kEpsSq) pts.pop_back();
            else break;
        }
        if (pts.size() < 3) return;

        // Ensure counter-clockwise winding.
        double area2 = 0.0;
        for (size_t i = 0; i < pts.size(); ++i) {
            size_t j = (i + 1) % pts.size();
            area2 += static_cast<double>(pts[i].x) * pts[j].y
                   - static_cast<double>(pts[j].x) * pts[i].y;
        }
        if (std::fabs(area2) < 1e-6) return;
        if (area2 < 0.0) std::reverse(pts.begin(), pts.end());

        auto cross2 = [](cocos2d::CCPoint a, cocos2d::CCPoint b, cocos2d::CCPoint c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        auto pointInTri = [&](cocos2d::CCPoint p, cocos2d::CCPoint a,
                              cocos2d::CCPoint b, cocos2d::CCPoint c) {
            float c1 = cross2(a, b, p);
            float c2 = cross2(b, c, p);
            float c3 = cross2(c, a, p);
            return (c1 >= -1e-5f && c2 >= -1e-5f && c3 >= -1e-5f);
        };

        std::vector<int> idx(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) idx[i] = static_cast<int>(i);

        float minCross = 1e-6f;
        while (idx.size() > 3) {
            bool clipped = false;
            size_t m = idx.size();
            for (size_t i = 0; i < m; ++i) {
                int iPrev = idx[(i + m - 1) % m];
                int iCurr = idx[i];
                int iNext = idx[(i + 1) % m];
                auto a = pts[iPrev], b = pts[iCurr], c = pts[iNext];
                if (cross2(a, b, c) <= minCross) continue;

                bool containsOther = false;
                for (size_t j = 0; j < m; ++j) {
                    int k = idx[j];
                    if (k == iPrev || k == iCurr || k == iNext) continue;
                    if (pointInTri(pts[k], a, b, c)) {
                        containsOther = true;
                        break;
                    }
                }
                if (containsOther) continue;

                drawTriangle(a, b, c, color);
                idx.erase(idx.begin() + static_cast<ptrdiff_t>(i));
                clipped = true;
                minCross = 1e-6f;
                break;
            }
            if (!clipped) {
                if (minCross > -1e-4f) {
                    minCross = -1e-4f;
                } else {
                    break;
                }
            }
        }
        if (idx.size() == 3) {
            drawTriangle(pts[idx[0]], pts[idx[1]], pts[idx[2]], color);
        }
    }

    void draw() override {
        if (m_nBufferCount == 0 || !m_pBuffer) return;

        if (m_bDirty) {
            glBindBuffer(GL_ARRAY_BUFFER, m_uVbo);
            glBufferData(GL_ARRAY_BUFFER,
                sizeof(cocos2d::ccV2F_C4B_T2F) * m_uBufferCapacity,
                m_pBuffer, GL_STREAM_DRAW);
            m_bDirty = false;
        }

        CC_NODE_DRAW_SETUP();

        // unbind before client-side arrays to isolate other draw hooks.
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        if (auto* texture = getWhiteTexture()) {
            ccGLBindTexture2D(texture->getName());
        } else {
            ccGLBindTexture2D(0);
        }

        ccGLBlendFunc(m_sBlendFunc.src, m_sBlendFunc.dst);

        ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);

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

// leave the vbo unbound; later nodes bind their own. reading the previous
// binding with glgetintegerv stalls dense uis.
    }
};
