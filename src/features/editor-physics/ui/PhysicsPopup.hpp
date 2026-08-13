#pragma once

#include "../PhysicsConfig.hpp"
#include "../services/PhysicsWorkspace.hpp"

#include <Geode/Geode.hpp>

#include <array>
#include <string>
#include <vector>

class EditorUI;
class ButtonSprite;

namespace paimon::editorphysics {

class PhysicsPopup : public geode::Popup {
public:
    static PhysicsPopup* create();

private:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

    void beginCapture(CaptureRole role);
    void toggleBMotion();
    void clearBodies();
    void preview();
    void bake();
    void removeLast();
    void adjust(int field, int direction);
    void tick(float dt);

    bool runSimulation();
    void refreshValues();
    void refreshBodies();
    void buildPreviewScenery(cocos2d::CCNode* clip, float width, float height);
    void rebuildPreview();
    void addWorldGround();
    void refreshCameraScale();
    float previewScale() const;
    void adjustZoom(float factor);
    void resetView();
    void refreshOverlays();
    void drawBodyOutline(std::size_t index);
    void drawTrajectory();
    void cycleFocus(int direction);
    void updateFocusLabel();
    float playbackTime() const;
    void drawPreview(float time, float dt);
    void setStatus(std::string const& text, cocos2d::ccColor3B color);
    EditorUI* editorUI() const;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void scrollWheel(float vertical, float horizontal) override;

    LabConfig m_config;
    std::vector<ResolvedBody> m_resolved;
    SimulationTrace m_trace;
    bool m_playing = false;
    float m_elapsed = 0.f;
    int m_focusIndex = -1;
    Vec2 m_camera{};
    float m_camScale = 1.f;
    float m_zoom = 1.f;
    bool m_manualCamera = false;
    bool m_panning = false;
    cocos2d::ccColor3B m_groundColor{30, 30, 45};

    cocos2d::CCNode* m_previewWorld = nullptr;
    cocos2d::CCLayerColor* m_groundLine = nullptr;
    cocos2d::CCDrawNode* m_pathDraw = nullptr;
    cocos2d::CCLabelBMFont* m_focusLabel = nullptr;
    std::vector<cocos2d::CCNode*> m_bodyContainers;
    std::vector<cocos2d::CCDrawNode*> m_outlineNodes;
    cocos2d::CCLabelBMFont* m_bodyALabel = nullptr;
    cocos2d::CCLabelBMFont* m_otherBodiesLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    ButtonSprite* m_bodyModeSprite = nullptr;
    std::array<cocos2d::CCLabelBMFont*, 9> m_valueLabels{};
};

} // namespace paimon::editorphysics
