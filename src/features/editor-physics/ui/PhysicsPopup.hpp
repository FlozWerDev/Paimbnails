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
    void refreshPreviewBounds();
    void drawPreview(float time);
    void setStatus(std::string const& text, cocos2d::ccColor3B color);
    EditorUI* editorUI() const;

    LabConfig m_config;
    std::vector<ResolvedBody> m_resolved;
    SimulationTrace m_trace;
    bool m_playing = false;
    float m_elapsed = 0.f;
    float m_previewMinX = 0.f;
    float m_previewMinY = 0.f;
    float m_previewScale = 1.f;

    cocos2d::CCDrawNode* m_previewDraw = nullptr;
    cocos2d::CCLabelBMFont* m_bodyALabel = nullptr;
    cocos2d::CCLabelBMFont* m_otherBodiesLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    ButtonSprite* m_bodyModeSprite = nullptr;
    std::array<cocos2d::CCLabelBMFont*, 9> m_valueLabels{};
};

} // namespace paimon::editorphysics
