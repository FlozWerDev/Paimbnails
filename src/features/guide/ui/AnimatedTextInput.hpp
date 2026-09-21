#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

// Animated wrapper over geode::TextInput (glow, typing dot, send sweep).
// No native Enter callback: a relay delegate forwards to geode's and fires onSubmit.

namespace paimon::guide {

class AnimatedTextInput : public cocos2d::CCNode {
public:
    static AnimatedTextInput* create(float width, std::string const& placeholder);

    void setCallback(std::function<void(std::string const&)> cb);

    // Fired when the user presses Enter while the input is focused.
    void setOnSubmit(std::function<void()> cb);
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

    static constexpr int kGlowPulseTag = 2001;
    static constexpr int kSweepTag     = 2002;

    // Delegate interposed between the CCTextInputNode and geode's TextInput:
    // forwards everything to the original delegate and reports Enter presses.
    class EnterRelayDelegate : public TextInputDelegate {
    public:
        TextInputDelegate* forward = nullptr;
        std::function<void()> onEnter;

        void textChanged(CCTextInputNode* n) override {
            if (forward) forward->textChanged(n);
        }
        void textInputOpened(CCTextInputNode* n) override {
            if (forward) forward->textInputOpened(n);
        }
        void textInputClosed(CCTextInputNode* n) override {
            if (forward) forward->textInputClosed(n);
        }
        void textInputShouldOffset(CCTextInputNode* n, float offset) override {
            if (forward) forward->textInputShouldOffset(n, offset);
        }
        void textInputReturn(CCTextInputNode* n) override {
            if (forward) forward->textInputReturn(n);
        }
        bool allowTextInput(CCTextInputNode* n) override {
            return forward ? forward->allowTextInput(n) : true;
        }
        void enterPressed(CCTextInputNode* n) override {
            if (forward) forward->enterPressed(n);
            if (onEnter) onEnter();
        }
    };

    geode::TextInput* m_input = nullptr;
    cocos2d::extension::CCScale9Sprite* m_glow = nullptr;
    cocos2d::CCSprite* m_typingDot = nullptr;
    std::function<void(std::string const&)> m_userCallback;
    std::function<void()> m_onSubmit;
    EnterRelayDelegate m_enterRelay;

    float m_width = 0.f;
};

} // namespace paimon::guide
