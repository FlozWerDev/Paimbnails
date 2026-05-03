#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/utils/cocos.hpp>
#include <cmath>
#include <vector>

using cocos2d::CCDrawNode;
using cocos2d::CCTexture2D;
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
 */
class PaimonDrawNode : public CCDrawNode {
public:
    static CCTexture2D* getWhiteTexture() {
        static CCTexture2D* texture = []() -> CCTexture2D* {
            unsigned char pixel[4] = {255, 255, 255, 255};
            auto* tex = new CCTexture2D();
            if (!tex->initWithData(
                pixel,
                cocos2d::kCCTexture2DPixelFormat_RGBA8888,
                1,
                1,
                {1.f, 1.f}
            )) {
                tex->release();
                return nullptr;
            }
            tex->autorelease();
            tex->retain();
            return tex;
        }();
        return texture;
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

    void draw() override {
        if (m_nBufferCount == 0) return;

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
        // y evitar que hooks de otros mods corrompan el estado
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
    }
};
