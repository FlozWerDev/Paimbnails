#pragma once

#include <Geode/Geode.hpp>
#include <string>

// Visual wrapper over the paim_Paimon.png sprite with chainable animations:
// Idle (bobbing loop), Blink, Talk, Surprise, Wave, Point, Sleep. One-shot
// animations run over Idle via CCAction tags (kIdleTag/kBlinkTag/kStateTag),
// so Idle + Blink + Talk can run at once without cancelling each other.

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

    // Play an animation. Idle/Blink loop; the rest are one-shot and return to Idle when done.
    void play(Animation anim);

    // Point at a target node (computes the angle and rotates the sprite). Null target resets to 0.
    void pointAt(cocos2d::CCNode* target, float duration = 0.3f);

    // In "lively" mode Paimon does continuous Idle+Blink and reacts more to chat
    // animations. When false, the sprite stays semi-static (e.g. guide disabled).
    void setLively(bool lively);
    bool isLively() const { return m_lively; }

    // Access the inner sprite for opacity/color/flipX tweaks.
    cocos2d::CCSprite* getSprite() const { return m_sprite; }

    // Optional chat bubble ("Ask me!") at top-right. Empty text hides it.
    void showBubble(std::string const& text, float duration = 3.0f);
    void hideBubble();
    bool hasBubble() const { return static_cast<bool>(m_bubble.lock()); }

    void onExit() override;

protected:
    bool init(float spriteScale);

    void startIdleLoop();
    void scheduleNextBlink();
    void onBlinkTimer(float dt);

    // Action tags to keep states from interfering.
    static constexpr int kIdleTag  = 1001;
    static constexpr int kBlinkTag = 1002;
    static constexpr int kStateTag = 1003;

    cocos2d::CCSprite* m_sprite = nullptr;
    geode::WeakRef<cocos2d::CCNode> m_bubble;
    cocos2d::CCLabelBMFont* m_bubbleText = nullptr;

    bool m_lively = false;
    Animation m_currentState = Animation::Idle;
};

} // namespace paimon::guide
