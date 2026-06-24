#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <vector>

namespace paimon::editorfilters {

class MyLevelFilterPopup : public geode::Popup {
protected:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

    void onToggle(cocos2d::CCObject* sender);
    void onTrash(cocos2d::CCObject* sender);

    CCMenuItemToggler* makeToggler(char const* text, int tag, bool on);
    void reloadBrowser();

    std::vector<CCMenuItemToggler*> m_togglers;
    geode::TextInput* m_songInput = nullptr;

public:
    static MyLevelFilterPopup* create();
};

} // namespace paimon::editorfilters
