#pragma once

#include <Geode/Geode.hpp>
#include <chrono>
#include <functional>
#include <string>

#include "AnimatedPaimon.hpp"
#include "AnimatedTextInput.hpp"
#include "../services/GuideIntents.hpp"

// Paimon chat popup with an animated character, scrollable history, text input,
// suggestion chips, and an optional action button.

namespace paimon::guide {

class PaimonGuideChatPopup : public geode::Popup {
public:
    static PaimonGuideChatPopup* create();

    // Inject a question into the input.
    void submitQuery(std::string const& query);

protected:
    bool init() override;
    void onExit() override;

    // Submit the current query from Enter.
    void keyDown(cocos2d::enumKeyCodes key, double p1) override;

    void onSubmitButton(cocos2d::CCObject* sender);
    void onTakeMeThere(cocos2d::CCObject* sender);
    void onSuggestionChip(cocos2d::CCObject* sender);
    void onRecommendationChip(cocos2d::CCObject* sender);
    void onClearChat(cocos2d::CCObject* sender);
    void onHelpButton(cocos2d::CCObject* sender);
    void onToggleMode(cocos2d::CCObject* sender);

    // Handle an async Max reply on the main thread.
    void onMaxReply(GuideAnswer const& ans);

    // Rebuild chips from the last answer or default suggestions.
    void setRecommendationChips(std::vector<GuideRecommendation> const& recs);
    void restoreDefaultChips();

    // Update the current-topic label.
    void updateTopicLabel(std::string const& topicId);

    // Debounce Enter from both IME and keyDown paths.
    void trySubmitFromEnter();

    // Append a Paimon typewriter bubble or a user bubble.
    void displayMessage(std::string const& message);
    void appendUserMessage(std::string const& message);
    void onTypewriterTick(float dt);
    void finishTypewriter();

    // Build a bubble row and store its label.
    cocos2d::CCNode* makeBubble(std::string const& wrapped, bool fromUser);

    // Restack bubbles and scroll to the newest.
    void relayoutChat();

    AnimatedPaimon* m_paimon = nullptr;
    AnimatedTextInput* m_input = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_responseLabel = nullptr; // Newest Paimon bubble.
    CCMenuItemSpriteExtra* m_takeMeBtn = nullptr;
    cocos2d::CCMenu* m_takeMeMenu = nullptr;
    cocos2d::CCMenu* m_suggestionsMenu = nullptr;
    cocos2d::CCLabelBMFont* m_lastBubbleLabel = nullptr; // Set by makeBubble.
    cocos2d::CCLabelBMFont* m_topicLabel = nullptr;      // Current topic.

    std::string m_pendingMessage;
    std::size_t m_typewriterIndex = 0;

    std::chrono::steady_clock::time_point m_lastEnterSubmit{};

    // Pending action from the last intent.
    std::function<void(PaimonGuideChatPopup*)> m_pendingAction;

    // Pending chip actions.
    std::vector<GuideRecommendation> m_pendingRecommendations;

    // Mode toggle and label.
    CCMenuItemSpriteExtra* m_modeBtn = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    void refreshModeButton();
};

}
