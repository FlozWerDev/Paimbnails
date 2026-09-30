#pragma once

// paimon rtx config popup.
#include <Geode/Geode.hpp>

namespace paimon::rtx {

// sliders write live config; disk flush stays separate so drags don't rewrite json.
class RTXConfigLayer : public geode::Popup {
public:
    static RTXConfigLayer* create();

protected:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

    void rebuild();
    void scheduleRebuild();
    void touched(bool leavesPreset);
    void flush(float dt);
    void refreshStats(float dt);

private:
    int m_tab = 0;
    bool m_dirty = false;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;
};

} // namespace paimon::rtx
