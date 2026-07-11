// Relocate pause-menu build tools onto the EDIT button bar (Tinker-aligned).
// Tinker: steals pause actions into an EDIT tab with 40×40 GJ_button_04 + icons.
// Paimon: injects the same four actions into m_editButtonBar with edit-bar plates.

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../api/FakePause.hpp"

#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Notification.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {
bool on() { return moduleEnabled("editor-mod-relocate-build-tools"); }

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

void pushTool(
    EditorUI* ui,
    char const* preferred,
    std::initializer_list<char const*> fallbacks,
    char const* id,
    std::function<void()> action
) {
    if (!ui || !ui->m_editButtonBar || !ui->m_editButtonBar->m_buttonArray) return;
    // 40×40 GJ_button_04 plate + icon (Tinker RelocateBuildTools look).
    auto* btn = editBarToolButton(preferred, fallbacks, std::move(action));
    if (!btn) return;
    btn->setID(id);
    ui->m_editButtonBar->m_buttonArray->addObject(btn);
}
} // namespace

class $modify(PaimonRelocateToolsUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!on()) return true;
        if (!m_editButtonBar || !m_editButtonBar->m_buttonArray) return true;

        // Build Helper
        pushTool(
            this, files::toolBuildHelper,
            { "GJ_hammerIcon_001.png", "edit_eStartPosBtn_001.png" },
            "paimbnails/tool-bh",
            [this] {
                if (!m_editorLayer) return;
                if (runBuildHelper(m_editorLayer)) {
                    Notification::create("Build Helper", NotificationIcon::Success)->show();
                }
            }
        );
        // Align X
        pushTool(
            this, files::toolAlignX,
            { "edit_leftBtn_001.png", "edit_rightBtn_001.png", "GJ_arrow_01_001.png" },
            "paimbnails/tool-ax",
            [this] {
                if (m_editorLayer) runAlignX(m_editorLayer);
            }
        );
        // Align Y
        pushTool(
            this, files::toolAlignY,
            { "edit_upBtn_001.png", "edit_downBtn_001.png" },
            "paimbnails/tool-ay",
            [this] {
                if (m_editorLayer) runAlignY(m_editorLayer);
            }
        );
        // Create Loop
        pushTool(
            this, files::toolLoop,
            { "edit_eStartPosBtn_001.png", "GJ_playBtn2_001.png" },
            "paimbnails/tool-loop",
            [this] {
                if (m_editorLayer) runCreateLoop(m_editorLayer);
            }
        );

        reloadEditBar(this);
        return true;
    }
};
