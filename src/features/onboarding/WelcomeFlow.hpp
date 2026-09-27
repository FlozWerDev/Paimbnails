#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <string>
#include <unordered_map>

namespace paimon::onboarding {

bool isAccepted();

class WelcomePopup : public geode::Popup {
public:
    static WelcomePopup* create();

protected:
    bool init() override;
    void showStep();
    void showTerms();
    void nextStep();
    void applyStep();
    void selectLanguage(std::string const& language);
    void toggleOption(std::string const& key);
    void finish();

    cocos2d::CCNode* m_content = nullptr;
    int m_step = 0;
    int m_termsPage = 0;
    bool m_skippedTutorial = false;
    std::unordered_map<std::string, bool> m_pending;
};

}
