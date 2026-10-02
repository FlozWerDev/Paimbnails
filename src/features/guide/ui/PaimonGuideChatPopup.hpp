#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <chrono>
#include <functional>
#include <string>

#include "AnimatedPaimon.hpp"
#include "AnimatedTextInput.hpp"
#include "../services/GuideIntents.hpp"

// paimon chat popup with an animated character, scrollable history, text input,
// suggestion chips, and an optional action button.

namespace paimon::guide {

class PaimonGuideChatPopup : public PaimonPopup {
public:
    static PaimonGuideChatPopup* create();
    void submitQuery(std::string const& query);

protected:
    bool init() override;
    void onExit() override;
    void keyDown(cocos2d::enumKeyCodes key, double p1) override;

    void onSubmitButton(cocos2d::CCObject* sender);
    void onTakeMeThere(cocos2d::CCObject* sender);
    void onSuggestionChip(cocos2d::CCObject* sender);
    void onRecommendationChip(cocos2d::CCObject* sender);
    void onClearChat(cocos2d::CCObject* sender);
    void onHelpButton(cocos2d::CCObject* sender);
    void onToggleMode(cocos2d::CCObject* sender);

    // async max reply lands here, already on the main thread.
    void onMaxReply(GuideAnswer const& ans);

    void setRecommendationChips(std::vector<GuideRecommendation> const& recs);
    void restoreDefaultChips();
    void updateTopicLabel(std::string const& topicId);

    // enter arrives via both ime and keydown; debounced.
    void trySubmitFromEnter();

    void displayMessage(std::string const& message);
    void appendUserMessage(std::string const& message);
    void onTypewriterTick(float dt);
    void finishTypewriter();
    cocos2d::CCNode* makeBubble(std::string const& wrapped, bool fromUser);
    void relayoutChat();

    AnimatedPaimon* m_paimon = nullptr;
    AnimatedTextInput* m_input = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_responseLabel = nullptr; // newest paimon bubble.
    CCMenuItemSpriteExtra* m_takeMeBtn = nullptr;
    cocos2d::CCMenu* m_takeMeMenu = nullptr;
    cocos2d::CCMenu* m_suggestionsMenu = nullptr;
    cocos2d::CCLabelBMFont* m_lastBubbleLabel = nullptr; // set by makebubble.
    cocos2d::CCLabelBMFont* m_topicLabel = nullptr;      // current topic.

    std::string m_pendingMessage;
    std::size_t m_typewriterIndex = 0;

    std::chrono::steady_clock::time_point m_lastEnterSubmit{};

    std::function<void(PaimonGuideChatPopup*)> m_pendingAction;
    std::vector<GuideRecommendation> m_pendingRecommendations;

    CCMenuItemSpriteExtra* m_modeBtn = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    void refreshModeButton();
};

}
