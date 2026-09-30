#pragma once
#include <cstdint>
#include <memory>
#include <Geode/Geode.hpp>

class RenderTexture {
public:
    RenderTexture(uint32_t width, uint32_t height);
    ~RenderTexture();
    [[nodiscard]] bool isValid() const { return m_valid; }
    [[nodiscard]] bool begin();
    void bind();
    void end();
    [[nodiscard]] std::unique_ptr<uint8_t[]> getData() const;

private:
    uint32_t m_width, m_height;
    int32_t m_oldFBO = -1;
    uint32_t m_fbo = 0;
    uint32_t m_texture = 0;
    uint32_t m_depthStencil = 0;
    uint32_t m_stencilBuffer = 0; // used only on gl es without packed depth-stencil
    bool m_valid = false;
    bool m_begun = false;
    cocos2d::CCSize m_oldScale{};
    cocos2d::CCSize m_oldResolution{};
    cocos2d::CCSize m_oldWinSizeInPoints{};
    cocos2d::CCSize m_oldScreenSize{};
    std::array<float, 4> m_oldClearColor{};
};
