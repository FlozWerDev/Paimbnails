#include "../EditorMcpServer.hpp"
#include "../ui/EditorMcpPopup.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorUI.hpp>

using namespace geode::prelude;

namespace {

bool featureEnabled() {
    return paimon::modules::isEnabled("paimbnails.mcp.editor");
}

void openMcpPopup() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("editor-mcp-popup"_spr)) return;
    if (auto* popup = paimon::editormcp::EditorMcpPopup::create()) {
        popup->show();
    }
}

} // namespace

class $modify(PaimonEditorMcpUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!featureEnabled()) return true;

        auto* button = paimon::editor::assets::squareButton(
            "paim_gif_modeRender.png",
            {"GJ_optionsBtn_001.png", "GJ_infoIcon_001.png"},
            0.9f,
            EditorBaseColor::Cyan,
            [] { openMcpPopup(); },
            paimon::editor::toolbarToggleSize(this)
        );
        if (!button) return true;
        button->setID("editor-mcp-button"_spr);

        if (auto* menu = paimon::editor::hostToolbarMenu(this)) {
            menu->addChild(button);
            if (menu->getLayout()) menu->updateLayout();
        } else {
            auto* fallback = CCMenu::create();
            fallback->setID("editor-mcp-menu"_spr);
            fallback->setPosition({28.f, CCDirector::get()->getWinSize().height - 270.f});
            fallback->addChild(button);
            addChild(fallback, 100);
        }
        return true;
    }
};

$on_mod(Loaded) {
    listenForSettingChanges<bool>("editor-mcp-enable", +[](bool) {
        paimon::editormcp::EditorMcpServer::get().restart();
    });
    listenForSettingChanges<int64_t>("editor-mcp-port", +[](int64_t) {
        paimon::editormcp::EditorMcpServer::get().restart();
    });
}

$on_game(Loaded) {
    paimon::editormcp::EditorMcpServer::get().init();
}

$on_game(Exiting) {
    if (paimon::editormcp::EditorMcpServer::isInitialized()) {
        paimon::editormcp::EditorMcpServer::get().shutdown();
    }
}

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editor-mcp-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || !LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            if (!featureEnabled() && !paimon::editormcp::EditorMcpServer::get().isRunning()) {
                // Allow opening the popup with keybind so the user can re-enable it easily.
            }
            openMcpPopup();
        }
    ).leak();
}
