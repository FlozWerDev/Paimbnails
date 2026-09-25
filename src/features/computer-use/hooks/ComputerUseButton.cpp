// Editor entry point for Computer Use: a button in the editor toolbar plus
// the Ctrl+B keybind. Gated by the computeruse module.

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/Keyboard.hpp>

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../ui/ComputerUsePopup.hpp"

using namespace geode::prelude;

namespace {

bool computerUseEnabled() {
    return paimon::modules::isEnabled("paimbnails.computeruse.editor");
}

void openPanel() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("computeruse-popup"_spr)) return;
    if (auto* popup = paimon::computeruse::ComputerUsePopup::create()) popup->show();
}

// The toolbar menus differ between node-ids versions, so fall back to the menu
// that already holds the swipe/undo buttons before giving up on a layout.
CCMenu* hostMenu(EditorUI* ui) {
    for (auto const* id : {"toolbar-toggles-menu", "editor-buttons-menu", "undo-menu"}) {
        if (auto* menu = typeinfo_cast<CCMenu*>(ui->getChildByID(id))) return menu;
    }
    if (ui->m_swipeBtn) {
        if (auto* menu = typeinfo_cast<CCMenu*>(ui->m_swipeBtn->getParent())) return menu;
    }
    if (ui->m_undoBtn) {
        if (auto* menu = typeinfo_cast<CCMenu*>(ui->m_undoBtn->getParent())) return menu;
    }
    return nullptr;
}

} // namespace

class $modify(PaimonComputerUseEditorUI, EditorUI) {
    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!computerUseEnabled()) return true;

        auto* button = paimon::editor::assets::circleButton(
            "paim_computeruse.png",
            {"GJ_paintBtn_001.png", "GJ_optionsBtn_001.png"},
            0.7f,
            CircleBaseColor::Cyan,
            [] { openPanel(); },
            CircleBaseSize::Tiny
        );
        if (!button) return true;
        button->setID("computeruse-button"_spr);

        if (auto* toolbar = hostMenu(this)) {
            toolbar->addChild(button);
            if (toolbar->getLayout()) toolbar->updateLayout();
        } else {
            auto winSize = CCDirector::get()->getWinSize();
            auto* fallback = CCMenu::create();
            fallback->setID("computeruse-menu"_spr);
            fallback->setPosition({28.f, winSize.height - 90.f});
            fallback->addChild(button);
            this->addChild(fallback, 100);
        }
        return true;
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "computeruse-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!computerUseEnabled()) return;
            if (!LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            openPanel();
        }
    ).leak();
}
