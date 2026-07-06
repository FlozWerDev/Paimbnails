// Right-click drag rotates the editor canvas using GD's native camera angle.
// The touch hooks rotate incoming touches around the screen center so building,
// selecting and dragging keep working while the canvas is turned.

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/utils/cocos.hpp>
#include <cmath>

#include "../services/EditorRotateManager.hpp"
#include "../../../utils/EditorContext.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace paimon::editorrotate;

namespace {
    bool featureEnabled() {
        return Mod::get()->getSettingValue<bool>("editor-rotate-enable");
    }

    CCPoint rotateAroundPivot(CCPoint p, CCPoint pivot, float deg) {
        float r = CC_DEGREES_TO_RADIANS(deg);
        float s = std::sin(r), c = std::cos(r);
        p = p - pivot;
        return {p.x * c - p.y * s + pivot.x, p.x * s + p.y * c + pivot.y};
    }
}

class $modify(PaimonRotateEditorUI, EditorUI) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("EditorUI::ccTouchBegan", Priority::EarlyPre);
        (void)self.setHookPriorityPre("EditorUI::ccTouchMoved", Priority::EarlyPre);
        (void)self.setHookPriorityPre("EditorUI::ccTouchEnded", Priority::EarlyPre);
        (void)self.setHookPriorityPre("EditorUI::ccTouchCancelled", Priority::EarlyPre);
    }

    void transformTouch(CCTouch* touch) {
        if (!touch) return;
        auto& mgr = EditorRotateManager::get();
        if (!featureEnabled() || !mgr.isRotated()) return;
        if (m_editorLayer && m_editorLayer->m_playbackMode != PlaybackMode::Not) return;

        auto win = CCDirector::get()->getWinSize();
        CCPoint np = rotateAroundPivot(touch->getLocation(), win / 2.f, mgr.angle());
        touch->setTouchInfo(touch->getID(), np.x, win.height - np.y);
    }

    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        transformTouch(touch);
        return EditorUI::ccTouchBegan(touch, event);
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        transformTouch(touch);
        EditorUI::ccTouchMoved(touch, event);
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        transformTouch(touch);
        EditorUI::ccTouchEnded(touch, event);
    }

    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        transformTouch(touch);
        EditorUI::ccTouchCancelled(touch, event);
    }

    $override
    void playtestStopped() {
        EditorUI::playtestStopped();
        if (featureEnabled()) EditorRotateManager::get().reapply();
    }
};

class $modify(PaimonRotateLevelEditor, LevelEditorLayer) {
    $override
    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1)) return false;
        // Each editor session starts upright.
        EditorRotateManager::get().resetRotation();
        return true;
    }
};

$execute {
    // Right button owns canvas rotation inside the editor. Consuming the event
    // keeps GD's own right-drag and the capture menu out of the way.
    MouseInputEvent().listen([](MouseInputData& data) -> bool {
        if (data.button != MouseInputData::Button::Right) return ListenerResult::Propagate;
        if (!featureEnabled() || !paimon::isEditorScene()) return ListenerResult::Propagate;

        auto& mgr = EditorRotateManager::get();
        if (data.action == MouseInputData::Action::Press) {
            // Alt is required so plain right-click stays free for captures/menus.
            if (!(data.modifiers & KeyboardModifier::Alt)) return ListenerResult::Propagate;
            mgr.beginDrag(cocos::getMousePos());
            return ListenerResult::Stop;
        }
        if (mgr.isDragging()) {
            mgr.endDrag();
            return ListenerResult::Stop;
        }
        return ListenerResult::Propagate;
    }, 100).leak();

    MouseMoveEvent().listen([](int32_t, int32_t) -> bool {
        auto& mgr = EditorRotateManager::get();
        if (!mgr.isDragging()) return ListenerResult::Propagate;
        mgr.updateDrag(cocos::getMousePos());
        return ListenerResult::Propagate;
    }).leak();

    KeybindSettingPressedEventV3(Mod::get(), "editor-rotate-reset-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!featureEnabled() || !paimon::isEditorScene()) return;
            if (paimon::isRuntimeShuttingDown()) return;
            EditorRotateManager::get().resetRotation();
        }
    ).leak();
}
