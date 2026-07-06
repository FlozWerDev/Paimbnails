#include "EditorRotateManager.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <cmath>

using namespace geode::prelude;

namespace paimon::editorrotate {

namespace {
    bool rotateSlider() {
        return Mod::get()->getSettingValue<bool>("editor-rotate-slider-thumb");
    }
}

EditorRotateManager& EditorRotateManager::get() {
    static EditorRotateManager instance;
    return instance;
}

void EditorRotateManager::beginDrag(CCPoint const& mouse) {
    auto* editor = LevelEditorLayer::get();
    if (!editor || editor->m_playbackMode != PlaybackMode::Not) return;
    m_dragging = true;
    m_lastPos = mouse;
}

void EditorRotateManager::updateDrag(CCPoint const& mouse) {
    if (!m_dragging) return;

    auto center = CCDirector::get()->getWinSize() / 2.f;
    CCPoint v1 = m_lastPos - center;
    CCPoint v2 = mouse - center;

    float deltaAngle = CC_RADIANS_TO_DEGREES(std::atan2(v2.y, v2.x) - std::atan2(v1.y, v1.x));
    if (deltaAngle > 180.f) deltaAngle -= 360.f;
    if (deltaAngle < -180.f) deltaAngle += 360.f;

    updateCanvasRotation(deltaAngle);
    m_lastPos = mouse;
}

void EditorRotateManager::endDrag() {
    if (!m_dragging) return;
    m_dragging = false;
    if (m_isSnapped) {
        m_smoothed = std::round(m_rotation);
        m_rotation = m_smoothed;
        applyToEditor();
    }
}

void EditorRotateManager::resetRotation() {
    m_smoothed = 0.f;
    m_unsnapped = 0.f;
    m_rotation = 0.f;
    m_isSnapped = true;
    applyToEditor();
}

void EditorRotateManager::reapply() {
    applyToEditor();
}

// Snapping + smoothing lifted from the same idea used by Tinker's canvas rotate.
void EditorRotateManager::updateCanvasRotation(float deltaAngle) {
    auto* editor = LevelEditorLayer::get();
    if (!editor || editor->m_playbackMode != PlaybackMode::Not) return;

    constexpr float snapIncrement = 45.f;
    constexpr float snapThreshold = 2.f;
    constexpr float unsnapThreshold = 5.f;
    constexpr float smoothingFactor = 0.2f;

    m_unsnapped = std::fmod(m_unsnapped - deltaAngle, 360.f);
    if (m_unsnapped < 0.f) m_unsnapped += 360.f;

    float nearest = std::round(m_unsnapped / snapIncrement) * snapIncrement;
    float diff = std::fabs(std::fmod(m_unsnapped - nearest + 180.f, 360.f) - 180.f);

    float target;
    if (!m_isSnapped && diff < snapThreshold) {
        target = nearest;
        m_isSnapped = true;
    } else if (m_isSnapped && diff < unsnapThreshold) {
        target = nearest;
    } else {
        target = m_unsnapped;
        m_isSnapped = false;
    }

    float shortest = std::fmod(target - m_smoothed + 540.f, 360.f) - 180.f;
    m_smoothed = std::fmod(m_smoothed + shortest * smoothingFactor + 360.f, 360.f);
    m_rotation = m_smoothed;
    applyToEditor();
}

void EditorRotateManager::applyToEditor() {
    auto* editor = LevelEditorLayer::get();
    if (!editor) return;

    editor->m_gameState.m_cameraAngle = m_smoothed;

    if (auto* ui = editor->m_editorUI; ui && ui->m_positionSlider) {
        if (auto* thumb = ui->m_positionSlider->getThumb()) {
            thumb->setRotation(rotateSlider() ? m_smoothed : 0.f);
        }
    }
}

} // namespace paimon::editorrotate
