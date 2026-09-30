#pragma once

#include <Geode/Geode.hpp>
#include <string>

// chainable animations over the paim_paimon.png sprite; one-shots run over idle
// via ccaction tags so idle + blink + talk don't cancel each other.

namespace paimon::guide {

class AnimatedPaimon : public cocos2d::CCNode {
public:
    enum class Animation {
        Idle,
        Blink,
        Talk,
        Surprise,
        Wave,
        Point,
        Sleep,
    };

    static AnimatedPaimon* create(float spriteScale = 1.0f);

    // play an animation. idle/blink loop; the rest are one-shot and return to idle when done.
    void play(Animation anim);

    // point at a target node (computes the angle and rotates the sprite). null target resets to 0.
    void pointAt(cocos2d::CCNode* target, float duration = 0.3f);

    // in "lively" mode paimon does continuous idle+blink and reacts more to chat
    // animations. when false, the sprite stays semi-static (e.g. guide disabled).
    void setLively(bool lively);

    // optional chat bubble ("ask me!") at top-right. empty text hides it.
    void showBubble(std::string const& text, float duration = 3.0f);
    void hideBubble();

    void onExit() override;

protected:
    bool init(float spriteScale);

    void startIdleLoop();
    void scheduleNextBlink();
    void onBlinkTimer(float dt);

    // action tags to keep states from interfering.
    static constexpr int kIdleTag  = 1001;
    static constexpr int kBlinkTag = 1002;
    static constexpr int kStateTag = 1003;

    cocos2d::CCSprite* m_sprite = nullptr;
    geode::WeakRef<cocos2d::CCNode> m_bubble;

    bool m_lively = false;
};

} // namespace paimon::guide
