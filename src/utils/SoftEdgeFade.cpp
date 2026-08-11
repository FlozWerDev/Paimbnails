#include "SoftEdgeFade.hpp"

#include <Geode/cocos/platform/CCGL.h>

#include <algorithm>
#include <array>
#include <cmath>

using namespace cocos2d;

namespace paimon {

namespace {
ccTex2F lerpTexCoord(ccTex2F const& a, ccTex2F const& b, float t) {
    return {
        a.u + (b.u - a.u) * t,
        a.v + (b.v - a.v) * t
    };
}

ccColor4B fadeColor(ccColor4B color, float opacity, bool premultiplied) {
    opacity = std::clamp(opacity, 0.f, 1.f);
    color.a = static_cast<GLubyte>(std::round(color.a * opacity));
    if (premultiplied) {
        color.r = static_cast<GLubyte>(std::round(color.r * opacity));
        color.g = static_cast<GLubyte>(std::round(color.g * opacity));
        color.b = static_cast<GLubyte>(std::round(color.b * opacity));
    }
    return color;
}
}

bool drawSoftEdgeFade(CCSprite* sprite, SoftEdgeFade const& fade) {
    if (!sprite || fade.amount <= 0.f || !sprite->getTexture() || !sprite->getShaderProgram()) {
        return false;
    }

    auto* parent = sprite->getParent();
    if (!parent) return false;

    auto const size = parent->getContentSize();
    float const skew = std::clamp(fade.skew, 0.f, size.width);
    float const width = std::clamp(fade.amount, 0.f, 1.f) * size.width * 0.75f;
    if (width <= 0.f || size.height <= 0.f) return false;

    auto toSpriteSpace = [parent, sprite](CCPoint const& point) {
        return sprite->convertToNodeSpace(parent->convertToWorldSpace(point));
    };

    std::array<CCPoint, 6> const points = {
        toSpriteSpace({skew, size.height}),
        toSpriteSpace({0.f, 0.f}),
        toSpriteSpace({skew + width, size.height}),
        toSpriteSpace({width, 0.f}),
        toSpriteSpace({size.width, size.height}),
        toSpriteSpace({size.width, 0.f})
    };
    std::array<float, 6> const opacities = {0.f, 0.f, 1.f, 1.f, 1.f, 1.f};

    auto const quad = sprite->getQuad();
    float const quadWidth = quad.br.vertices.x - quad.bl.vertices.x;
    float const quadHeight = quad.tl.vertices.y - quad.bl.vertices.y;
    if (std::abs(quadWidth) < 0.001f || std::abs(quadHeight) < 0.001f) return false;

    auto sampleTexture = [&](CCPoint const& point) {
        float const x = (point.x - quad.bl.vertices.x) / quadWidth;
        float const y = (point.y - quad.bl.vertices.y) / quadHeight;
        auto const bottom = lerpTexCoord(quad.bl.texCoords, quad.br.texCoords, x);
        auto const top = lerpTexCoord(quad.tl.texCoords, quad.tr.texCoords, x);
        return lerpTexCoord(bottom, top, y);
    };

    std::array<ccV3F_C4B_T2F, 6> vertices;
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].vertices = {points[i].x, points[i].y, quad.bl.vertices.z};
        vertices[i].texCoords = sampleTexture(points[i]);
        vertices[i].colors = fadeColor(quad.bl.colors, opacities[i], sprite->m_bOpacityModifyRGB);
    }

    auto* program = sprite->getShaderProgram();
    program->use();
    program->setUniformsForBuiltins();
    ccGLBlendFunc(sprite->m_sBlendFunc.src, sprite->m_sBlendFunc.dst);
    ccGLBindTexture2D(sprite->getTexture()->getName());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);

    GLsizei const stride = sizeof(ccV3F_C4B_T2F);
    glVertexAttribPointer(
        kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, stride,
        &vertices[0].vertices
    );
    glVertexAttribPointer(
        kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, stride,
        &vertices[0].texCoords
    );
    glVertexAttribPointer(
        kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
        &vertices[0].colors
    );

    glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(vertices.size()));
    CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
    CC_INCREMENT_GL_DRAWS(1);
#endif
    return true;
}

} // namespace paimon
