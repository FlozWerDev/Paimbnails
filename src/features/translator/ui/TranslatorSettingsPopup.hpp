#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::translate {

// rebuilds the per-cell translate button after the setting changes.
void refreshCommentCells();

class TranslatorSettingsPopup : public PaimonPopup {
public:
    static TranslatorSettingsPopup* create();

protected:
    bool init() override;
    void buildSwitches();
    void buildLanguages();
    void buildEndpoint();
    void syncFromSettings();

    CCMenuItemToggler* m_enabledToggle = nullptr;
    CCMenuItemToggler* m_autoToggle = nullptr;
    cocos2d::CCLabelBMFont* m_hint = nullptr;
    float m_hintMaxWidth = 0.f;
    geode::TextInput* m_endpointInput = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_languageButtons;
    std::vector<std::string> m_languageCodes;
};

}
