// Fractional move keybinds + extra editor action keybinds.
// Pause-menu actions go through FakePause (Build Helper, Align, Save).

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"
#include "../api/FakePause.hpp"

#include <Geode/Enums.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/ObjectToolbox.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Notification.hpp>

#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

float gridSize() {
    float g = Mod::get()->getSavedValue<float>("paim-editor-grid-size", 30.f);
    if (g < 0.9f) g = 30.f;
    return g;
}

void moveSelection(EditorUI* ui, float dx, float dy) {
    if (!ui) return;
    auto objs = getSelectedObjects(ui);
    for (auto* o : objs) {
        ui->moveObject(o, {dx, dy});
    }
    ui->updateButtons();
    ui->updateObjectInfoLabel();
}

} // namespace

class $modify(PaimonEditorKeybindsUI, EditorUI) {
    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        if (!paimon::isEditorScene() || focusedTextInput()) {
            return EditorUI::keyDown(key, timestamp);
        }
        auto* kd = CCKeyboardDispatcher::get();
        bool ctrl = kd && kd->getControlKeyPressed();
        bool shift = kd && kd->getShiftKeyPressed();
        bool alt = kd && kd->getAltKeyPressed();

        // --- Move fractions ---
        if (moduleEnabled("editor-mod-move-keybinds") && alt) {
            float g = gridSize();
            float step = ctrl ? g * 0.5f : g * 0.25f; // Ctrl+Alt = half, Alt = quarter
            bool handled = true;
            if (key == KEY_A || key == KEY_Left) moveSelection(this, -step, 0.f);
            else if (key == KEY_D || key == KEY_Right) moveSelection(this, step, 0.f);
            else if (key == KEY_W || key == KEY_Up) moveSelection(this, 0.f, step);
            else if (key == KEY_S || key == KEY_Down) moveSelection(this, 0.f, -step);
            else handled = false;
            if (handled) return;
        }

        // Also support vanilla half via Ctrl+Alt alone using EditCommand
        if (moduleEnabled("editor-mod-move-keybinds") && ctrl && alt) {
            // already handled above with half step
        }

        // --- Extra keybinds ---
        if (moduleEnabled("editor-mod-extra-keybinds")) {
            // R = restart playtest
            if (key == KEY_R && !ctrl && !shift && !alt) {
                if (m_editorLayer && m_editorLayer->m_playbackMode != PlaybackMode::Not) {
                    this->onStopPlaytest(nullptr);
                    WeakRef<EditorUI> self(this);
                    Loader::get()->queueInMainThread([self] {
                        auto ui = self.lock();
                        if (!ui || !ui->m_editorLayer || !paimon::isEditorScene()) return;
                        if (ui->m_editorLayer->m_playbackMode == PlaybackMode::Not) {
                            ui->onPlaytest(nullptr);
                        }
                    });
                    return;
                }
            }
            // Ctrl+Shift+R = rotate 45 CW
            if (key == KEY_R && ctrl && shift) {
                this->transformObjectCall(EditCommand::RotateCW45);
                return;
            }
            // Ctrl+Shift+E = rotate 45 CCW
            if (key == KEY_E && ctrl && shift) {
                this->transformObjectCall(EditCommand::RotateCCW45);
                return;
            }
            // Ctrl+E = edit object
            if (key == KEY_E && ctrl && !shift && !alt) {
                this->editObject(nullptr);
                return;
            }
            // Ctrl+Shift+G = edit group (Ctrl+G may be color picker)
            if (key == KEY_G && ctrl && shift) {
                this->editGroup(nullptr);
                return;
            }
            // Ctrl+Shift+C = copy state
            if (key == KEY_C && ctrl && shift) {
                this->onCopyState(nullptr);
                return;
            }
            // Ctrl+Shift+V = paste state
            if (key == KEY_V && ctrl && shift) {
                this->onPasteState(nullptr);
                return;
            }
            // Ctrl+Shift+B = paste color
            if (key == KEY_B && ctrl && shift) {
                this->onPasteColor(nullptr);
                return;
            }
            // Ctrl+Shift+H = Build Helper (fake pause)
            if (key == KEY_H && ctrl && shift && m_editorLayer) {
                if (runBuildHelper(m_editorLayer)) {
                    Notification::create("Build Helper", NotificationIcon::Success)->show();
                }
                return;
            }
            // Ctrl+Alt+X / Ctrl+Alt+Y = Align
            if (key == KEY_X && ctrl && alt && m_editorLayer) {
                runAlignX(m_editorLayer);
                return;
            }
            if (key == KEY_Y && ctrl && alt && m_editorLayer) {
                runAlignY(m_editorLayer);
                return;
            }
            // Ctrl+Shift+S = quick save via pause saveLevel
            if (key == KEY_S && ctrl && shift && m_editorLayer) {
                if (runSaveLevel(m_editorLayer)) {
                    Notification::create("Level saved", NotificationIcon::Success)->show();
                }
                return;
            }
            // Ctrl+Shift+L = Create Loop
            if (key == KEY_L && ctrl && shift && m_editorLayer) {
                runCreateLoop(m_editorLayer);
                return;
            }
        }

        EditorUI::keyDown(key, timestamp);
    }
};
