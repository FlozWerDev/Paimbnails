#pragma once

#include <Geode/Geode.hpp>

namespace paimon::transitions::dynamic {

class PanelShowGuard {
public:
    explicit PanelShowGuard(FLAlertLayer* panel);
    ~PanelShowGuard();
    PanelShowGuard(PanelShowGuard const&) = delete;
    PanelShowGuard& operator=(PanelShowGuard const&) = delete;
private:
    FLAlertLayer* m_panel;
    bool m_elasticity, m_action, m_active;
};

bool panelWillChange(cocos2d::CCNode* panel, cocos2d::CCNode* parent, bool opening);
void finishPanelAnimation();
void clearPanelHistory();

}
