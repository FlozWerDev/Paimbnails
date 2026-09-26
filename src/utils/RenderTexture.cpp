#include "RenderTexture.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <limits>

// GLES2 lacks packed depth-stencil; use OES_packed_depth_stencil.
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
    #define PT_SEPARATE_DEPTH_STENCIL 1
  #endif
#endif

using namespace geode::prelude;

RenderTexture::RenderTexture(uint32_t width, uint32_t height) : m_width(width), m_height(height) {
    if (width == 0 || height == 0
        || static_cast<uint64_t>(width) * static_cast<uint64_t>(height)
            > std::numeric_limits<size_t>::max() / 4) {
        return;
    }

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

    m_valid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE
           && glGetError() == GL_NO_ERROR;
    if (!m_valid) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_oldFBO);
        if (m_texture) glDeleteTextures(1, &m_texture);
        if (m_depthStencil) glDeleteRenderbuffers(1, &m_depthStencil);
#ifdef PT_SEPARATE_DEPTH_STENCIL
        if (m_stencilBuffer) glDeleteRenderbuffers(1, &m_stencilBuffer);
#endif
        if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
        m_texture = 0;
        m_depthStencil = 0;
        m_stencilBuffer = 0;
        m_fbo = 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_oldFBO);
}

RenderTexture::~RenderTexture() {
    // destructor may run after GL teardown (atexit, hot-reload): handles invalid,
    // glDelete* can crash some drivers.
    auto* director = cocos2d::CCDirector::get();
    bool glAlive = director && director->getOpenGLView();
    if (!glAlive) {
        // context dead: leak the handles, the OS frees them. better than crashing.
        return;
    }

    this->end();
    if (m_texture) glDeleteTextures(1, &m_texture);
    if (m_depthStencil) glDeleteRenderbuffers(1, &m_depthStencil);
#ifdef PT_SEPARATE_DEPTH_STENCIL
    if (m_stencilBuffer) glDeleteRenderbuffers(1, &m_stencilBuffer);
#endif
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
}

bool RenderTexture::begin() {
    if (!m_valid || m_begun) return false;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFBO);

    auto director = cocos2d::CCDirector::get();
    auto* view = director ? director->getOpenGLView() : nullptr;
    if (!view) return false;
    auto winSize = director->getWinSize();
    auto displayFactor = geode::utils::getDisplayFactor();
    if (winSize.width <= 0.f || winSize.height <= 0.f || displayFactor <= 0.f) {
        return false;
    }

    m_oldScale = cocos2d::CCSize{ view->m_fScaleX, view->m_fScaleY };
    m_oldResolution = view->getDesignResolutionSize();
    m_oldWinSizeInPoints = director->m_obWinSizeInPoints;

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
    m_begun = true;
    return true;
}

void RenderTexture::bind() {
    if (m_valid && m_begun) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    }
}

void RenderTexture::end() {
    if (!m_begun) return;

    glClearColor(m_oldClearColor[0], m_oldClearColor[1], m_oldClearColor[2], m_oldClearColor[3]);

    if (m_oldFBO != -1) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_oldFBO);
        m_oldFBO = -1;
    }

    if (m_oldScale.width != 0 && m_oldScale.height != 0) {
        auto director = cocos2d::CCDirector::get();
        auto* view = director ? director->getOpenGLView() : nullptr;
        if (!view) {
            m_begun = false;
            m_oldScale = cocos2d::CCSize{0, 0};
            return;
        }

        view->m_fScaleX = m_oldScale.width;
        view->m_fScaleY = m_oldScale.height;
        view->m_obScreenSize = m_oldScreenSize;
        view->setDesignResolutionSize(m_oldResolution.width, m_oldResolution.height, kResolutionExactFit);
        director->m_obWinSizeInPoints = m_oldWinSizeInPoints;
        director->setViewport();

        m_oldScale = cocos2d::CCSize{0, 0};
    }
    m_begun = false;
}

std::unique_ptr<uint8_t[]> RenderTexture::getData() const {
    if (!m_valid || !m_texture || !m_fbo) {
        return nullptr;
    }
    auto data = std::make_unique<uint8_t[]>(
        static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4);

    GLint oldPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &oldPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    GLint oldFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, data.get());
    GLenum const error = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, oldPackAlignment);
    glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);

    if (error != GL_NO_ERROR) return nullptr;
    return data;
}
