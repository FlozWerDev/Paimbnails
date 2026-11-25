#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include "../../editor-suite/EditorPopupKit.hpp"
#include "../EditorObjectClassifier.hpp"
#include "../StructureOptimizerCore.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <string>

class EditorUI;
class LevelEditorLayer;

namespace paimon::optimizer {

class OptimizerPopup : public PaimonPopup {
public:
    static OptimizerPopup* create();

private:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

    void buildPreviewPanel(cocos2d::CCMenu* menu);
    void buildStatsPanel();
    void buildOptionsPanel(cocos2d::CCMenu* menu);

    void analyze(bool refitCamera);
    void recomputePlan();
    void applyOptimization();
    void toggleScope();
    void toggleViewMode();
    void adjustStepper(int field, int direction);

    void rebuildPreview();
    void refreshCameraTransform();
    void fitCameraToScene();
    void adjustZoom(float factor);
    void updatePreviewHeader();
    void refreshStats();
    void refreshOptionsUI();
    void setStatus(std::string const& text, cocos2d::ccColor3B color);

    LevelEditorLayer* editorLayer() const;
    EditorUI* editorUI() const;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    bool scrollWheel(float vertical, float horizontal) override;

    OptimizerOptions m_options;
    CapturedScene m_scene;
    OptimizationPlan m_plan;

    bool m_selectedOnly = true;
    bool m_showAfter = true;
    bool m_panning = false;
    cocos2d::CCPoint m_camCenter{0.f, 0.f};
    float m_baseScale = 1.f;
    float m_zoom = 1.f;

    cocos2d::CCNode* m_previewWorld = nullptr;
    cocos2d::CCDrawNode* m_drawNode = nullptr;
    cocos2d::CCLabelBMFont* m_viewLabel = nullptr;

    std::array<cocos2d::CCLabelBMFont*, 3> m_statLabels{};
    std::array<cocos2d::CCLabelBMFont*, 2> m_stepperLabels{};
    editor::kit::Pill m_scopePill;
    editor::kit::Pill m_viewPill;
    editor::kit::Pill m_applyPill;
    CCMenuItemToggler* m_viewToggle = nullptr;

    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCSprite* m_statusIcon = nullptr;
};

} // namespace paimon::optimizer
