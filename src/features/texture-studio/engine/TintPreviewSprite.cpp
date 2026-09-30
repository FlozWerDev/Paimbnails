#include "TintPreviewSprite.hpp"

#include "../../../utils/GLSLLoader.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace paimon::texture_studio {

TintPreviewSprite* TintPreviewSprite::create(cocos2d::CCTexture2D* base,
                                             cocos2d::CCTexture2D* mask) {
    auto ret = new TintPreviewSprite();
    if (ret && ret->init(base, mask)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TintPreviewSprite::init(cocos2d::CCTexture2D* base,
                             cocos2d::CCTexture2D* mask) {
    if (!CCSprite::initWithTexture(base)) return false;
    setMaskTexture(mask);
    if (auto* program = paimon::shaders::getTintPreviewShader()) {
        setShaderProgram(program);
        // sampler units fixed; only colors/grades move per draw.
        program->use();
        GLint loc = program->getUniformLocationForName("u_texture");
        if (loc != -1) program->setUniformLocationWith1i(loc, 0);
        loc = program->getUniformLocationForName("u_roleMask");
        if (loc != -1) program->setUniformLocationWith1i(loc, 1);
        m_usesTintProgram = true;
    }
    return true;
}

TintPreviewSprite::~TintPreviewSprite() {
    if (m_maskTex) m_maskTex->release();
}

void TintPreviewSprite::setMaskTexture(cocos2d::CCTexture2D* mask) {
    if (m_maskTex == mask) return;
    if (mask) mask->retain();
    if (m_maskTex) m_maskTex->release();
    m_maskTex = mask;
    if (m_maskTex) {
        // weights must survive sampling bit-exact; npot clamps on gles2.
        cocos2d::ccTexParams params{GL_NEAREST, GL_NEAREST,
                                    GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        m_maskTex->setTexParameters(&params);
    }
}

void TintPreviewSprite::setTint(TintColors const& colors, int brightness,
                                float saturation, float contrast,
                                bool glowReplace, bool applyDetail,
                                int darkThreshold) {
    m_colors = colors;
    // same clamps as precomputedtint::make: out-of-range ui grades identically on both paths.
    m_brightness = std::clamp(static_cast<float>(brightness), 1.f, 1000.f);
    m_saturation = std::clamp(saturation, 0.f, 3.f);
    m_contrast = std::clamp(contrast, -1.f, 1.f);
    m_glowReplace = glowReplace;
    m_applyDetail = applyDetail;
    m_darkThreshold = static_cast<float>(darkThreshold);
}

void TintPreviewSprite::pushUniforms(cocos2d::CCGLProgram* program) {
    GLint loc = program->getUniformLocationForName("u_color1");
    if (loc != -1) program->setUniformLocationWith3f(loc,
        m_colors.color1.r, m_colors.color1.g, m_colors.color1.b);
    loc = program->getUniformLocationForName("u_color2");
    if (loc != -1) program->setUniformLocationWith3f(loc,
        m_colors.color2.r, m_colors.color2.g, m_colors.color2.b);
    loc = program->getUniformLocationForName("u_detailColor");
    if (loc != -1) program->setUniformLocationWith3f(loc,
        m_colors.detail.r, m_colors.detail.g, m_colors.detail.b);
    loc = program->getUniformLocationForName("u_glowColor");
    if (loc != -1) program->setUniformLocationWith3f(loc,
        m_colors.glow.r, m_colors.glow.g, m_colors.glow.b);
    loc = program->getUniformLocationForName("u_brightness");
    if (loc != -1) program->setUniformLocationWith1f(loc, m_brightness);
    loc = program->getUniformLocationForName("u_saturation");
    if (loc != -1) program->setUniformLocationWith1f(loc, m_saturation);
    loc = program->getUniformLocationForName("u_contrast");
    if (loc != -1) program->setUniformLocationWith1f(loc, m_contrast);
    loc = program->getUniformLocationForName("u_darkThreshold");
    if (loc != -1) program->setUniformLocationWith1f(loc, m_darkThreshold);
    loc = program->getUniformLocationForName("u_glowReplace");
    if (loc != -1) program->setUniformLocationWith1f(loc, m_glowReplace ? 1.f : 0.f);
    loc = program->getUniformLocationForName("u_applyDetail");
    if (loc != -1) program->setUniformLocationWith1f(loc, m_applyDetail ? 1.f : 0.f);
}

void TintPreviewSprite::draw() {
    if (m_usesTintProgram && m_maskTex) {
        auto* program = getShaderProgram();
        program->use();
        program->setUniformsForBuiltins();
        ccGLBindTexture2DN(1, m_maskTex->getName());
        pushUniforms(program);
    }
    CCSprite::draw();
}

}  // namespace paimon::texture_studio
