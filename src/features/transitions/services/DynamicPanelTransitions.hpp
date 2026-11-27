#pragma once

#include <Geode/Geode.hpp>

namespace paimon::transitions::dynamic {

// holds the panel's animation until the native show finishes positioning it.
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

class FastBypassGuard {
public:
    FastBypassGuard();
    ~FastBypassGuard();
    FastBypassGuard(FastBypassGuard const&) = delete;
    FastBypassGuard& operator=(FastBypassGuard const&) = delete;
};

bool isInFastBypass();
bool isPanelTransitionsEnabled();
bool panelWillChange(cocos2d::CCNode* panel, cocos2d::CCNode* parent, bool opening);
void panelDidOpen(cocos2d::CCNode* panel);
void finishPanelAnimation();
void clearPanelHistory();

}
