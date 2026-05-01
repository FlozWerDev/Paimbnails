#pragma once
#include <Geode/Geode.hpp>
#include <functional>

class BetaUploadWarningPopup : public geode::Popup {
protected:
    std::function<void()> m_onProceed;

    bool init(std::function<void()> onProceed);
    void onAccept(cocos2d::CCObject*);
    void onDismissForever(cocos2d::CCObject*);

public:
    static BetaUploadWarningPopup* create(std::function<void()> onProceed);
};

namespace paimon {
    void showBetaUploadWarningIfNeeded(std::function<void()> onProceed);
}
