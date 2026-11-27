#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../ui/OptimizerPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorUI.hpp>

using namespace geode::prelude;

namespace {

bool enabled() {
    return paimon::modules::isEnabled("paimbnails.optimizer.editor");
}

void openOptimizer() {
    if (!enabled() || !LevelEditorLayer::get()) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("structure-optimizer-popup"_spr)) return;
    if (auto* popup = paimon::optimizer::OptimizerPopup::create()) {
        popup->show();
    }
}

} // namespace

class $modify(PaimonOptimizerEditorUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!enabled()) return true;

        auto* button = paimon::editor::assets::squareButton(
            "paim_editor_optimizer.png",
            {"GJ_hammerIcon_001.png", "GJ_optionsBtn_001.png"},
            0.9f,
            EditorBaseColor::Green,
            [] { openOptimizer(); },
            paimon::editor::toolbarToggleSize(this)
        );
        if (!button) return true;
        button->setID("structure-optimizer-button"_spr);

        paimon::editor::addToolbarButton(
            this,
            button,
            "structure-optimizer-menu"_spr,
            {28.f, CCDirector::get()->getWinSize().height - 236.f}
        );
        return true;
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editor-optimizer-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || !enabled() || !LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            openOptimizer();
        }
    ).leak();
}
