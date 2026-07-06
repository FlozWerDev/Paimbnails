#pragma once
#include <Geode/Geode.hpp>
#include "../models/EmoteModels.hpp"

namespace paimon::emotes {

class EmoteAutocomplete : public cocos2d::CCNode {
    CCTextInputNode* m_inputNode = nullptr;
    geode::CopyableFunction<void(std::string const&)> m_setTextFn;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCNodeRGBA* m_bg = nullptr;
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
