// Camera tools: center camera on selection / move selection to camera.
// Tinker EditTools places these on the EDIT button bar with dedicated sprites.
// We do the same (edit bar + keybinds N/M).

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"
#include "../EditorAssets.hpp"

#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "../../../framework/HookConventions.hpp"
#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {
void reloadEditBar(EditorUI* ui) {
    if (!ui || !ui->m_editButtonBar) return;
    int cols = GameManager::get()->getIntGameVariable("0049");
    int rows = GameManager::get()->getIntGameVariable("0050");
    if (cols < 1) cols = 6;
    if (rows < 1) rows = 2;
    cols = std::clamp(cols, 1, 12);
    rows = std::clamp(rows, 1, 6);
    ui->m_editButtonBar->reloadItems(cols, rows);
}
} // namespace

class $modify(PaimonCameraToolsUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void onCamToObj(CCObject*) {
        if (!moduleEnabled("editor-mod-camera-tools")) return;
        focusCameraOnSelection(this);
    }

    void onObjToCam(CCObject*) {
        if (!moduleEnabled("editor-mod-camera-tools")) return;
        moveSelectionToCamera(this);
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-camera-tools")) return true;
        if (!m_editButtonBar || !m_editButtonBar->m_buttonArray) return true;

        // Tinker: center-camera.png / center-object.png on the edit bar.
        // Custom: paim_cam-to-object / paim_object-to-cam; else find / freemove.
        auto* toObj = editBarToolButton(
            files::camToObject,
            { "edit_findBtn_001.png", "GJ_zoomInBtn_001.png" },
            [this] { this->onCamToObj(nullptr); }
        );
        auto* toCam = editBarToolButton(
            files::objectToCam,
            { "edit_freeMoveBtn_001.png", "GJ_zoomOutBtn_001.png" },
            [this] { this->onObjToCam(nullptr); }
        );
        if (toObj) {
            toObj->setID("paimbnails/camera-to-object");
            m_editButtonBar->m_buttonArray->addObject(toObj);
        }
        if (toCam) {
            toCam->setID("paimbnails/object-to-camera");
            m_editButtonBar->m_buttonArray->addObject(toCam);
        }
        reloadEditBar(this);
        return true;
    }

    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        if (moduleEnabled("editor-mod-camera-tools")
            && paimon::isEditorScene()
            && !focusedTextInput()) {
            auto* kd = CCKeyboardDispatcher::get();
            bool mod = kd && (kd->getControlKeyPressed() || kd->getShiftKeyPressed() || kd->getAltKeyPressed());
            if (!mod) {
                if (key == KEY_N) {
                    focusCameraOnSelection(this);
                    return;
                }
                if (key == KEY_M) {
                    moveSelectionToCamera(this);
                    return;
                }
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};
