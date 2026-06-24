#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

// Visual wrapper over geode::TextInput with animated feedback: a pulsing glow
// halo while typing, a "typing dot" that reacts to input, and a send sweep that
// crosses the input on submit. TextInput has no native Enter callback, so the
// chat popup handles submit with an external button and calls playSendSweep().

namespace paimon::guide {

class AnimatedTextInput : public cocos2d::CCNode {
public:
    static AnimatedTextInput* create(float width, std::string const& placeholder);

    void setCallback(std::function<void(std::string const&)> cb);
    std::string getString() const;
    void setString(std::string const& s);
    void clear();

    // Visual feedback explicit triggers
    void playSendSweep();
    void playTypingPulse();

    void onExit() override;

    // Access the underlying input for further customization.
    geode::TextInput* getInput() const { return m_input; }

protected:
    bool init(float width, std::string const& placeholder);

    void onTextChanged(std::string const& text);
    void startGlowPulse();
    void stopGlowPulse();

    static constexpr int kGlowPulseTag = 2001;
    static constexpr int kSweepTag     = 2002;

    geode::TextInput* m_input = nullptr;
    cocos2d::extension::CCScale9Sprite* m_glow = nullptr;
    cocos2d::CCSprite* m_typingDot = nullptr;
    std::function<void(std::string const&)> m_userCallback;

    float m_width = 0.f;
};

} // namespace paimon::guide
