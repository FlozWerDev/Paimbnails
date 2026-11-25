#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

// animated wrapper over geode::textinput (glow, typing dot, send sweep).

namespace paimon::guide {

class AnimatedTextInput : public cocos2d::CCNode {
public:
    using cocos2d::CCNode::init;
    static AnimatedTextInput* create(float width, std::string const& placeholder);

    void setCallback(std::function<void(std::string const&)> cb);

    void setOnSubmit(std::function<void()> cb);
    std::string getString() const;
    void setString(std::string const& s);
    void clear();
    void playSendSweep();
    void playTypingPulse();

    void onExit() override;

    geode::TextInput* getInput() const { return m_input; }

protected:
    bool init(float width, std::string const& placeholder);

    void onTextChanged(std::string const& text);
    void startGlowPulse();

    static constexpr int kGlowPulseTag = 2001;
    static constexpr int kSweepTag     = 2002;

    geode::TextInput* m_input = nullptr;
    cocos2d::extension::CCScale9Sprite* m_glow = nullptr;
    cocos2d::CCSprite* m_typingDot = nullptr;
    std::function<void(std::string const&)> m_userCallback;
    std::function<void()> m_onSubmit;

    float m_width = 0.f;
};

} // namespace paimon::guide
