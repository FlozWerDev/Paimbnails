#pragma once

#include <Geode/Geode.hpp>

namespace paimon::transitions::dynamic {

// Holds the panel's animation until the native show finishes positioning it.
class PanelOpenScope {
public:
    PanelOpenScope(cocos2d::CCNode* panel, cocos2d::CCNode* parent);
    ~PanelOpenScope();
    PanelOpenScope(PanelOpenScope const&) = delete;
    PanelOpenScope& operator=(PanelOpenScope const&) = delete;
    bool active() const { return m_active; }
private:
    cocos2d::CCNode* m_panel;
    bool m_active;
};

class PanelShowGuard {
public:
    explicit PanelShowGuard(FLAlertLayer* panel);
    ~PanelShowGuard();
    PanelShowGuard(PanelShowGuard const&) = delete;
    PanelShowGuard& operator=(PanelShowGuard const&) = delete;
private:
    FLAlertLayer* m_panel;
    bool m_elasticity, m_action;
    PanelOpenScope m_scope;
};

bool panelWillChange(cocos2d::CCNode* panel, cocos2d::CCNode* parent, bool opening);
void panelDidOpen(cocos2d::CCNode* panel);
void finishPanelAnimation();
void clearPanelHistory();

}
