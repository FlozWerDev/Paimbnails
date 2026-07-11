// Scroll through create-tab object pages with the mouse wheel (Tinker-style).
// When the cursor is over the build bar, wheel changes pages instead of panning.

#include "../EditorModule.hpp"

#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/cocos.hpp>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

bool on() { return moduleEnabled("editor-mod-scrollable-objects"); }

EditButtonBar* activeCreateBar(EditorUI* ui) {
    if (!ui || !ui->m_createButtonBars) return nullptr;
    // Prefer the currently visible bar.
    for (auto* bar : CCArrayExt<EditButtonBar*>(ui->m_createButtonBars)) {
        if (bar && bar->isVisible()) return bar;
    }
    return typeinfo_cast<EditButtonBar*>(ui->m_createButtonBar);
}

bool pointOverNode(CCNode* node, CCPoint world) {
    if (!node || !node->isVisible()) return false;
    auto bb = node->boundingBox();
    auto* parent = node->getParent();
    if (!parent) return false;
    auto bl = parent->convertToWorldSpace({bb.getMinX(), bb.getMinY()});
    auto tr = parent->convertToWorldSpace({bb.getMaxX(), bb.getMaxY()});
    CCRect worldRect{bl.x, bl.y, tr.x - bl.x, tr.y - bl.y};
    return worldRect.containsPoint(world);
}

} // namespace

class $modify(PaimonScrollableObjectsUI, EditorUI) {
    $override
    void scrollWheel(float y, float x) {
        if (!on() || !m_editorLayer
            || m_editorLayer->m_playbackMode == PlaybackMode::Playing
            || m_selectedMode != 2) {
            return EditorUI::scrollWheel(y, x);
        }

        auto* keys = CCKeyboardDispatcher::get();
        // Leave Ctrl/Shift to scroll-zoom module.
        if (keys && (keys->getControlKeyPressed() || keys->getShiftKeyPressed())) {
            return EditorUI::scrollWheel(y, x);
        }

        auto* bar = activeCreateBar(this);
        auto mouse = getMousePos();
        if (!bar || !pointOverNode(bar, mouse)) {
            return EditorUI::scrollWheel(y, x);
        }

        // EditButtonBar pages via onLeft / onRight (or similar).
        // Prefer public API if present.
        float delta = (std::abs(y) >= std::abs(x)) ? y : x;
        if (delta > 0.f) {
            // Scroll up → previous page
            bar->onLeft(nullptr);
        } else if (delta < 0.f) {
            bar->onRight(nullptr);
        }
        // Consume: do not pan the canvas while over the object bar.
    }
};
