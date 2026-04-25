#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

class PaimonLoadingOverlay : public cocos2d::CCLayerColor {
protected:
    geode::LoadingSpinner* m_spinner = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_funFactLabel = nullptr;
    bool m_dismissed = false;

    bool init(std::string const& statusText, float spinnerSize);
    void startPulse();
    void showAt(cocos2d::CCNode* parent, cocos2d::CCPoint const& position, cocos2d::CCSize const& size, int zOrder);

public:
    static PaimonLoadingOverlay* create(
        std::string const& statusText = "Loading...",
        float spinnerSize = 40.f
    );

    void show(cocos2d::CCNode* parent, int zOrder = 200);
    void showLocal(cocos2d::CCNode* parent, int zOrder = 200);
    void dismiss();
    void updateText(std::string const& text);
};
