#pragma once

#include <Geode/Geode.hpp>
#include <chrono>
#include <functional>
#include <string>

#include "AnimatedPaimon.hpp"
#include "AnimatedTextInput.hpp"
#include "../services/GuideIntents.hpp"

// Paimon chat popup, redesigned: AnimatedPaimon on the left column (with a
// feature-count badge and clear/help utility buttons), a scrollable chat
// history with user/Paimon bubbles on the right, an AnimatedTextInput with an
// "Ask" button below (Enter submits too), suggestion chips at the bottom, and
// a "Take me there" button when the answer has an action.

namespace paimon::guide {

class PaimonGuideChatPopup : public geode::Popup {
public:
    static PaimonGuideChatPopup* create();

    // Inject a question as if the user typed it.
    void submitQuery(std::string const& query);

protected:
    bool init() override;
    void onExit() override;

    // Enter submits the current query (keyboard shortcut).
    void keyDown(cocos2d::enumKeyCodes key, double p1) override;

    void onSubmitButton(cocos2d::CCObject* sender);
    void onTakeMeThere(cocos2d::CCObject* sender);
    void onSuggestionChip(cocos2d::CCObject* sender);
    void onRecommendationChip(cocos2d::CCObject* sender);
    void onClearChat(cocos2d::CCObject* sender);
    void onHelpButton(cocos2d::CCObject* sender);

    // Rebuild bottom chips: recommendations from last answer, or default suggestions.
    void setRecommendationChips(std::vector<GuideRecommendation> const& recs);
    void restoreDefaultChips();

    // Enter can arrive from both the IME delegate (input focused) and
    // keyDown (input unfocused); debounce so one press = one submit.
    void trySubmitFromEnter();

    // Append a Paimon bubble with typewriter effect / a user bubble.
    void displayMessage(std::string const& message);
    void appendUserMessage(std::string const& message);
    void onTypewriterTick(float dt);
    void finishTypewriter();

    // Build a chat bubble row; stores the label in m_lastBubbleLabel.
    cocos2d::CCNode* makeBubble(std::string const& wrapped, bool fromUser);

    // Re-stack all bubbles top-to-bottom and scroll to the newest one.
    void relayoutChat();

    AnimatedPaimon* m_paimon = nullptr;
    AnimatedTextInput* m_input = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_responseLabel = nullptr; // newest Paimon bubble label
    CCMenuItemSpriteExtra* m_takeMeBtn = nullptr;
    cocos2d::CCMenu* m_takeMeMenu = nullptr;
    cocos2d::CCMenu* m_suggestionsMenu = nullptr;
    cocos2d::CCLabelBMFont* m_lastBubbleLabel = nullptr; // set by makeBubble

    // Typewriter state
    std::string m_pendingMessage;
    std::size_t m_typewriterIndex = 0;

    // Enter debounce
    std::chrono::steady_clock::time_point m_lastEnterSubmit{};

    // Pending action from the last intent (for "Take me there")
    std::function<void(PaimonGuideChatPopup*)> m_pendingAction;

    // Pending recommendation actions (chip tag index -> action)
    std::vector<GuideRecommendation> m_pendingRecommendations;
};

} // namespace paimon::guide
