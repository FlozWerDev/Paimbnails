#pragma once

#include <Geode/ui/Popup.hpp>

namespace paimon::gameplayperf {

class GameplayPerformancePopup : public geode::Popup {
protected:
    bool init() override;
    void onToggle(cocos2d::CCObject* sender);

public:
    static GameplayPerformancePopup* create();
};

} // namespace paimon::gameplayperf
