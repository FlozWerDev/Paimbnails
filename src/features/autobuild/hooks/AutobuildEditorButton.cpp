// Editor entry point for Autobuild: a button in the editor toolbar plus the
// Ctrl+B keybind. Gated by the autobuild module.

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/Keyboard.hpp>

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../services/Builder.hpp"
#include "../ui/AutobuildPopup.hpp"

using namespace geode::prelude;

namespace {

bool autobuildEnabled() {
    return paimon::modules::isEnabled("paimbnails.autobuild.editor");
}

void openPanel() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("autobuild-popup"_spr)) return;
    if (auto* popup = paimon::autobuild::AutobuildPopup::create()) popup->show();
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

class $modify(PaimonAutobuildEditorUI, EditorUI) {
    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        paimon::autobuild::forgetSession();
        if (!autobuildEnabled()) return true;

        auto* button = paimon::editor::assets::circleButton(
            "paim_autobuild.png",
            {"GJ_paintBtn_001.png", "GJ_optionsBtn_001.png"},
            0.7f,
            CircleBaseColor::Cyan,
            [] { openPanel(); },
            CircleBaseSize::Tiny
        );
        if (!button) return true;
        button->setID("autobuild-button"_spr);

        if (auto* toolbar = hostMenu(this)) {
            toolbar->addChild(button);
            if (toolbar->getLayout()) toolbar->updateLayout();
        } else {
            auto winSize = CCDirector::get()->getWinSize();
            auto* fallback = CCMenu::create();
            fallback->setID("autobuild-menu"_spr);
            fallback->setPosition({28.f, winSize.height - 90.f});
            fallback->addChild(button);
            this->addChild(fallback, 100);
        }
        return true;
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "autobuild-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!autobuildEnabled()) return;
            if (!LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            openPanel();
        }
    ).leak();
}
