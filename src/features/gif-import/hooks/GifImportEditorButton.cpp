#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../ui/GifImportPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorUI.hpp>

using namespace geode::prelude;

namespace {

bool enabled() {
    return paimon::modules::isEnabled("paimbnails.gifimport.editor");
}

void openImporter() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("gif-import-popup"_spr)) return;
    if (auto* popup = paimon::gifimport::GifImportPopup::create()) popup->show();
}

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

class $modify(PaimonGifImportEditorUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!enabled()) return true;

        auto* button = paimon::editor::assets::circleButton(
            "paim_gif_import.png",
            {"GJ_downloadBtn_001.png", "GJ_artBtn_001.png", "GJ_optionsBtn_001.png"},
            0.68f,
            CircleBaseColor::Green,
            [] { openImporter(); },
            CircleBaseSize::Tiny
        );
        if (!button) return true;
        button->setID("gif-import-button"_spr);

        if (auto* menu = hostMenu(this)) {
            menu->addChild(button);
            if (menu->getLayout()) menu->updateLayout();
        } else {
            auto* fallback = CCMenu::create();
            fallback->setID("gif-import-menu"_spr);
            fallback->setPosition({28.f, CCDirector::get()->getWinSize().height - 128.f});
            fallback->addChild(button);
            addChild(fallback, 100);
        }
        return true;
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "gif-import-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || !enabled() || !LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            openImporter();
        }
    ).leak();
}
