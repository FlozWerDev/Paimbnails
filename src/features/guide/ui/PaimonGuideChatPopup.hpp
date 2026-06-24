#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

#include "AnimatedPaimon.hpp"
#include "AnimatedTextInput.hpp"
#include "../services/GuideIntents.hpp"

// Paimon chat popup: AnimatedPaimon on the left (waves on open, talks on reply),
// a typewriter message panel on top, AnimatedTextInput with an "Ask" button below,
// and a "Take me there" button shown when the answer has an action.

namespace paimon::guide {

class PaimonGuideChatPopup : public geode::Popup {
public:
    static PaimonGuideChatPopup* create();

    // Inject a question as if the user typed it.
    void submitQuery(std::string const& query);

protected:
    bool init() override;
    void onExit() override;

    void onSubmitButton(cocos2d::CCObject* sender);
    void onTakeMeThere(cocos2d::CCObject* sender);
    void onSuggestionChip(cocos2d::CCObject* sender);

    // Replace the current message with a new one (typewriter effect).
    void displayMessage(std::string const& message);
    void onTypewriterTick(float dt);

    AnimatedPaimon* m_paimon = nullptr;
    AnimatedTextInput* m_input = nullptr;
    cocos2d::extension::CCScale9Sprite* m_responseBg = nullptr;
    cocos2d::CCLabelBMFont* m_responseLabel = nullptr;
    CCMenuItemSpriteExtra* m_takeMeBtn = nullptr;
    cocos2d::CCMenu* m_takeMeMenu = nullptr;
    cocos2d::CCMenu* m_suggestionsMenu = nullptr;

    // Typewriter state
    std::string m_pendingMessage;
    std::size_t m_typewriterIndex = 0;

    // Pending action from the last intent (for "Take me there")
    std::function<void(PaimonGuideChatPopup*)> m_pendingAction;
};

} // namespace paimon::guide
