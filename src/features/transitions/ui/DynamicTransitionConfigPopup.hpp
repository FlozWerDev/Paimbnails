#pragma once

#include <Geode/Geode.hpp>
#include "../services/DynamicTransitionManager.hpp"

namespace paimon::transitions::dynamic {

class DynamicTransitionConfigPopup : public geode::Popup {
public:
    static DynamicTransitionConfigPopup* create();

protected:
    bool init() override;

private:
    void rebuild();
    void scheduleRebuild();
    void persist();
    void applyPreset(int preset);
    Config m_config;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_presetLabel = nullptr;
    int m_tab = 0;
};

}
