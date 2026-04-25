#include "RenderTexture.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>

// OpenGL ES 2.0 no tiene GL_DEPTH24_STENCIL8 ni GL_DEPTH_STENCIL_ATTACHMENT
// Usa la extension OES_packed_depth_stencil en su lugar
#ifndef GL_DEPTH24_STENCIL8
  #ifdef GL_DEPTH24_STENCIL8_OES
    #define GL_DEPTH24_STENCIL8 GL_DEPTH24_STENCIL8_OES
  #else
    #define GL_DEPTH24_STENCIL8 0x88F0
  #endif
#endif

#ifndef GL_DEPTH_STENCIL_ATTACHMENT
  #ifdef GL_DEPTH_STENCIL_ATTACHMENT_OES
    #define GL_DEPTH_STENCIL_ATTACHMENT GL_DEPTH_STENCIL_ATTACHMENT_OES
  #else
    // fallback: attach depth and stencil separately
    #define PT_SEPARATE_DEPTH_STENCIL 1
  #endif
#endif

using namespace geode::prelude;

RenderTexture::RenderTexture(uint32_t width, uint32_t height) : m_width(width), m_height(height) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFBO);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    glGenRenderbuffers(1, &m_depthStencil);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencil);
#ifdef PT_SEPARATE_DEPTH_STENCIL
    // OpenGL ES sin packed depth-stencil: adjuntar por separado
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthStencil);
    glGenRenderbuffers(1, &m_stencilBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_stencilBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_stencilBuffer);
#else
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthStencil);
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, m_oldFBO);
}

RenderTexture::~RenderTexture() {
    this->end();
    glDeleteTextures(1, &m_texture);
    if (m_depthStencil) glDeleteRenderbuffers(1, &m_depthStencil);
#ifdef PT_SEPARATE_DEPTH_STENCIL
    if (m_stencilBuffer) glDeleteRenderbuffers(1, &m_stencilBuffer);
#endif
    glDeleteFramebuffers(1, &m_fbo);
}

void RenderTexture::begin() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFBO);

    auto view = cocos2d::CCEGLView::get();
    auto director = cocos2d::CCDirector::get();
    auto winSize = director->getWinSize();

    m_oldScale = cocos2d::CCSize{ view->m_fScaleX, view->m_fScaleY };
    m_oldResolution = view->getDesignResolutionSize();
    m_oldWinSizeInPoints = director->m_obWinSizeInPoints;

    auto displayFactor = geode::utils::getDisplayFactor();
    view->m_fScaleX = static_cast<float>(m_width) / winSize.width / displayFactor;
    view->m_fScaleY = static_cast<float>(m_height) / winSize.height / displayFactor;

    auto aspectRatio = static_cast<float>(m_width) / m_height;
    auto newRes = cocos2d::CCSize{ std::round(320.f * aspectRatio), 320.f };

    director->m_obWinSizeInPoints = newRes;
    m_oldScreenSize = view->m_obScreenSize;
    view->m_obScreenSize = cocos2d::CCSize{ static_cast<float>(m_width), static_cast<float>(m_height) };

    view->setDesignResolutionSize(newRes.width, newRes.height, kResolutionExactFit);

    glViewport(0, 0, m_width, m_height);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    glGetFloatv(GL_COLOR_CLEAR_VALUE, m_oldClearColor.data());
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void RenderTexture::end() {
    glClearColor(m_oldClearColor[0], m_oldClearColor[1], m_oldClearColor[2], m_oldClearColor[3]);

    if (m_oldFBO != -1) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_oldFBO);
        m_oldFBO = -1;
    }

    if (m_oldScale.width != 0 && m_oldScale.height != 0) {
        auto view = cocos2d::CCEGLView::get();
        auto director = cocos2d::CCDirector::get();

        view->m_fScaleX = m_oldScale.width;
        view->m_fScaleY = m_oldScale.height;
        view->m_obScreenSize = m_oldScreenSize;
        view->setDesignResolutionSize(m_oldResolution.width, m_oldResolution.height, kResolutionExactFit);
        director->m_obWinSizeInPoints = m_oldWinSizeInPoints;
        director->setViewport();

        m_oldScale = cocos2d::CCSize{0, 0};
    }
}

std::unique_ptr<uint8_t[]> RenderTexture::getData() const {
    if (!m_texture || !m_fbo) {
        return nullptr;
    }
    auto data = std::make_unique<uint8_t[]>(m_width * m_height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    GLint oldFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, data.get());
    
    glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
    
    return data;
}
