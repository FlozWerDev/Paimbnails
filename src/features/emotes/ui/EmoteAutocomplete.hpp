#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include "../models/EmoteModels.hpp"

namespace paimon::emotes {

/// Autocomplete popup that appears above a text input when the
/// user types `:XX` (colon + 2+ chars).  Shows up to 8 matching
/// emote names that can be clicked to complete the emote code.
class EmoteAutocomplete : public cocos2d::CCNode {
    CCTextInputNode* m_inputNode = nullptr;
    geode::CopyableFunction<void(std::string const&)> m_setTextFn;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::extension::CCScale9Sprite* m_bg = nullptr;
    std::string m_lastText;
    size_t m_colonPos = std::string::npos;

    bool init(CCTextInputNode* input,
              geode::CopyableFunction<void(std::string const&)> setTextFn);
    void update(float dt) override;
    void rebuildSuggestions(std::vector<EmoteInfo> const& matches,
                           std::string const& partial, size_t colonPos);
    void clearSuggestions();
    void onSuggestionClicked(cocos2d::CCObject* sender);

public:
    static EmoteAutocomplete* create(
        CCTextInputNode* input,
        geode::CopyableFunction<void(std::string const&)> setTextFn);
};

} // namespace paimon::emotes
