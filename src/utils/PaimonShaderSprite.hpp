#pragma once

#include <Geode/cocos/platform/CCGL.h>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>

// Targeted type imports to avoid namespace pollution in headers
using cocos2d::CCSprite;
using cocos2d::CCTexture2D;
using cocos2d::CCSize;
using cocos2d::CCRect;
using cocos2d::CCPoint;
using cocos2d::ccColor3B;
using cocos2d::ccColor4B;
using cocos2d::ccV3F_C4B_T2F;
using cocos2d::ccGLBlendFunc;
using cocos2d::ccGLBindTexture2D;
using cocos2d::ccGLEnableVertexAttribs;
using cocos2d::kCCVertexAttrib_Position;
using cocos2d::kCCVertexAttrib_TexCoords;
using cocos2d::kCCVertexAttrib_Color;
using cocos2d::kCCVertexAttribFlag_PosColorTex;
using cocos2d::kCCTexture2DPixelFormat_RGBA8888;

#ifndef kQuadSize
#define kQuadSize sizeof(ccV3F_C4B_T2F)
#endif

/**
 * sprite con shader custom pa thumbnails.
 * draw() manual pa saltarse hooks de otros mods (ej: happy textures).
 * reutilizable en LevelCell, GJScoreCell, etc.
 */
class PaimonShaderSprite : public CCSprite {
public:
    float m_intensity = 0.0f;
    float m_time = 0.0f;
    float m_brightness = 1.0f;
    CCSize m_texSize = {0, 0};

    static PaimonShaderSprite* createWithTexture(CCTexture2D* texture) {
        auto sprite = new PaimonShaderSprite();
        if (sprite && sprite->initWithTexture(texture)) {
            sprite->autorelease();
            sprite->setID("paimon-shader-sprite"_spr);
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    void draw() override {
        CC_NODE_DRAW_SETUP();

        GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
        if (intensityLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
        }

        GLint timeLoc = getShaderProgram()->getUniformLocationForName("u_time");
        if (timeLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(timeLoc, m_time);
        }

        GLint brightLoc = getShaderProgram()->getUniformLocationForName("u_brightness");
        if (brightLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(brightLoc, m_brightness);
        }

        GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
        if (sizeLoc != -1) {
            if (m_texSize.width == 0) {
                m_texSize = getTexture()->getContentSizeInPixels();
            }
            getShaderProgram()->setUniformLocationWith2f(sizeLoc, m_texSize.width, m_texSize.height);
        }

        ccGLBlendFunc(m_sBlendFunc.src, m_sBlendFunc.dst);

        if (getTexture()) {
            ccGLBindTexture2D(getTexture()->getName());
        } else {
            ccGLBindTexture2D(0);
        }

        // desvincular vbo activo pa evitar crashes en drivers (ej: atio6axx.dll)
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);

        uintptr_t offset = (uintptr_t)&m_sQuad;

        int diff = offsetof(ccV3F_C4B_T2F, vertices);
        glVertexAttribPointer(kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, kQuadSize, (void*)(offset + diff));

        diff = offsetof(ccV3F_C4B_T2F, texCoords);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, kQuadSize, (void*)(offset + diff));

        diff = offsetof(ccV3F_C4B_T2F, colors);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, kQuadSize, (void*)(offset + diff));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
        CC_INCREMENT_GL_DRAWS(1);
#endif
    }
};

/**
 * gradiente shader custom con colores por vertice.
 * draw() manual pa saltarse hooks de otros mods.
 */
class PaimonShaderGradient : public CCSprite {
public:
    float m_intensity = 0.0f;
    float m_time = 0.0f;
    CCSize m_texSize = {0, 0};
    ccColor3B m_startColor = {255, 255, 255};
    ccColor3B m_endColor = {255, 255, 255};

    static PaimonShaderGradient* create(ccColor4B const& start, ccColor4B const& end) {
        auto sprite = new PaimonShaderGradient();

        unsigned char data[] = {
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255
        };

        auto texture = new CCTexture2D();
        if (texture && texture->initWithData(data, kCCTexture2DPixelFormat_RGBA8888, 2, 2, {2.0f, 2.0f})) {
            if (sprite && sprite->initWithTexture(texture)) {
                texture->release();
                sprite->autorelease();
                sprite->setTextureRect({0, 0, 2, 2});
                sprite->setStartColor({start.r, start.g, start.b});
                sprite->setEndColor({end.r, end.g, end.b});
                sprite->setOpacity(start.a);
                return sprite;
            }
        }

        CC_SAFE_DELETE(texture);
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    void setStartColor(ccColor3B const& color) {
        m_startColor = color;
        updateGradient();
    }

    void setEndColor(ccColor3B const& color) {
        m_endColor = color;
        updateGradient();
    }

    void updateGradient() {
        GLubyte opacity = getOpacity();
        ccColor4B start4 = {m_startColor.r, m_startColor.g, m_startColor.b, opacity};
        ccColor4B end4 = {m_endColor.r, m_endColor.g, m_endColor.b, opacity};

        m_sQuad.bl.colors = start4;
        m_sQuad.tl.colors = start4;
        m_sQuad.br.colors = end4;
        m_sQuad.tr.colors = end4;
    }

    void setOpacity(GLubyte opacity) override {
        CCSprite::setOpacity(opacity);
        updateGradient();
    }

    void setContentSize(CCSize const& size) override {
        CCSprite::setContentSize(size);

        m_sQuad.bl.vertices = {0.0f, 0.0f, 0.0f};
        m_sQuad.br.vertices = {size.width, 0.0f, 0.0f};
        m_sQuad.tl.vertices = {0.0f, size.height, 0.0f};
        m_sQuad.tr.vertices = {size.width, size.height, 0.0f};

        updateGradient();
    }

    void setVector(CCPoint const&) {}

    void draw() override {
        CC_NODE_DRAW_SETUP();

        GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
        if (intensityLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
        }

        GLint timeLoc = getShaderProgram()->getUniformLocationForName("u_time");
        if (timeLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(timeLoc, m_time);
        }

        GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
        if (sizeLoc != -1) {
            getShaderProgram()->setUniformLocationWith2f(sizeLoc, getContentSize().width, getContentSize().height);
        }

        ccGLBlendFunc(m_sBlendFunc.src, m_sBlendFunc.dst);

        if (getTexture()) {
            ccGLBindTexture2D(getTexture()->getName());
        } else {
            ccGLBindTexture2D(0);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);

        uintptr_t offset = (uintptr_t)&m_sQuad;

        int diff = offsetof(ccV3F_C4B_T2F, vertices);
        glVertexAttribPointer(kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, kQuadSize, (void*)(offset + diff));

        diff = offsetof(ccV3F_C4B_T2F, texCoords);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, kQuadSize, (void*)(offset + diff));

        diff = offsetof(ccV3F_C4B_T2F, colors);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, kQuadSize, (void*)(offset + diff));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
        CC_INCREMENT_GL_DRAWS(1);
#endif
    }
};

/**
 * sprite blur pa fondos de celdas con sync a GIF animado.
 * reutilizable en GJScoreCell y otros sitios.
 */
class PaimonBlurSprite : public CCSprite {
public:
    float m_intensity = 0.0f;
    CCSize m_texSize = {0, 0};
    CCSprite* m_syncTarget = nullptr;

    static PaimonBlurSprite* createWithTexture(CCTexture2D* tex) {
        auto ret = new PaimonBlurSprite();
        if (ret && ret->initWithTexture(tex)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void update(float dt) override {
        if (m_syncTarget) {
            // AnimatedGIFSprite cambia textura via setDisplayFrame() cada frame
            // sincronizamos la textura directamente sin tocar el rect/contentSize
            // pa no romper la escala calculada al crear el blur
            auto targetTex = m_syncTarget->getTexture();
            if (targetTex && targetTex != this->getTexture()) {
                // guardar tamano original pa que setTextureRect no lo rompa
                auto savedSize = this->getContentSize();
                auto savedScale = this->getScale();

                this->setTexture(targetTex);
                // actualizar texSize pa que el shader use las dimensiones correctas
                m_texSize = targetTex->getContentSizeInPixels();
                // actualizar el rect pa que coincida con la textura nueva
                auto texSize = targetTex->getContentSize();
                this->setTextureRect(CCRect(0, 0, texSize.width, texSize.height));

                // restaurar tamano y escala originales pa no romper el layout
                this->setContentSize(savedSize);
                this->setScale(savedScale);
            }
        }
        CCSprite::update(dt);
    }

    void draw() override {
        if (getShaderProgram()) {
            getShaderProgram()->use();
            getShaderProgram()->setUniformsForBuiltins();

            GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
            if (intensityLoc != -1) {
                getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
            }

            if (m_texSize.width == 0 && getTexture()) {
                m_texSize = getTexture()->getContentSizeInPixels();
            }
            float w = m_texSize.width > 0 ? m_texSize.width : 1.0f;
            float h = m_texSize.height > 0 ? m_texSize.height : 1.0f;

            GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
            if (sizeLoc != -1) {
                getShaderProgram()->setUniformLocationWith2f(sizeLoc, w, h);
            }
            GLint screenLoc = getShaderProgram()->getUniformLocationForName("u_screenSize");
            if (screenLoc != -1) {
                getShaderProgram()->setUniformLocationWith2f(screenLoc, w, h);
            }
        }
        CCSprite::draw();
    }
};


