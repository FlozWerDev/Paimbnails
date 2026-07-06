#pragma once

#include <Geode/Geode.hpp>

class LevelEditorLayer;

namespace paimon::editorrotate {

// Rotates the editor canvas using GD's native camera angle
// (GJGameState::m_cameraAngle), so the whole view (objects, grid, background)
// turns natively instead of us transforming a layer by hand. Right-click drag
// spins the canvas around the screen center with snapping to 45-degree steps,
// and the position slider thumb is rotated to match so you can read the angle.
class EditorRotateManager {
public:
    static EditorRotateManager& get();

    void beginDrag(cocos2d::CCPoint const& mouse);
    void updateDrag(cocos2d::CCPoint const& mouse);
    void endDrag();
    bool isDragging() const { return m_dragging; }

    // Current canvas angle in degrees; 0 means upright.
    float angle() const { return m_smoothed; }
    bool isRotated() const { return std::fabs(m_smoothed) > 0.001f; }

    void resetRotation();
    // Re-push the stored angle onto the editor (after entering / leaving playtest).
    void reapply();

private:
    void updateCanvasRotation(float deltaAngle);
    void applyToEditor();

    bool m_dragging = false;
    cocos2d::CCPoint m_lastPos = {0.f, 0.f};
    float m_rotation = 0.f;
    float m_unsnapped = 0.f;
    float m_smoothed = 0.f;
    bool m_isSnapped = true;
};

} // namespace paimon::editorrotate
