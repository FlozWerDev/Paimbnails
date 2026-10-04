#pragma once
#include <Geode/Geode.hpp>
#include <Geode/cocos/shaders/CCGLProgram.h>
#include <string>

// u_time only advances while hovered or after a tap, so idle pictures cost nothing.
namespace paimon::profile_pic {

class HoverShaderSprite : public cocos2d::CCSprite {
public:
    static HoverShaderSprite* createWithTexture(cocos2d::CCTexture2D* tex,
                                                std::string const& shaderId,
                                                float intensity);

    void draw() override;
    void update(float dt) override;

    // owning container reports whether the pointer is inside the picture.
    void setHoverSource(cocos2d::CCNode* hitNode) { m_hitNode = hitNode; }

    // android fallback: pulse the effect for a few seconds.
    void triggerBurst();

protected:
    bool initEffect(cocos2d::CCTexture2D* tex, std::string const& shaderId, float intensity);
    bool pointerInside() const;

    cocos2d::CCGLProgram* m_program = nullptr;
    cocos2d::CCNode* m_hitNode = nullptr;
    std::string m_shaderId;
    float m_intensity = 1.f;
    float m_time = 0.f;
    float m_hover = 0.f;
    float m_burst = 0.f;
    float m_cursorX = -1.f;
    float m_cursorY = -1.f;
};

}
