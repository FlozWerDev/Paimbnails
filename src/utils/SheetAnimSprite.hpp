#pragma once

#include <Geode/Geode.hpp>
#include <vector>

// Grid spritesheet animation on a single CCTexture2D (UV rect swap only).
class SheetAnimSprite : public cocos2d::CCSprite {
public:
    static SheetAnimSprite* create(
        char const* path,
        int frameW,
        int frameH,
        int cols,
        int frameCount,
        std::vector<float> delaysSec
    );

    // Bundled Paimon loading mascot sheet (resources/paim_PaimonSheet.png).
    static SheetAnimSprite* createPaimonMascot();

    void play();
    void pauseAnim();
    void setLoop(bool loop) { m_loop = loop; }

    void onEnter() override;
    void onExit() override;
    void update(float dt) override;

protected:
    bool initSheet(
        char const* path,
        int frameW,
        int frameH,
        int cols,
        int frameCount,
        std::vector<float> delaysSec
    );
    void applyFrame(int index);

    float m_frameW = 0.f;
    float m_frameH = 0.f;
    int m_cols = 0;
    int m_count = 0;
    int m_index = 0;
    float m_timer = 0.f;
    bool m_playing = true;
    bool m_loop = true;
    std::vector<float> m_delays;
};
