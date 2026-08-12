#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../services/PhysicsWorkspace.hpp"
#include "../ui/PhysicsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <fmt/format.h>

using namespace geode::prelude;

namespace {

bool enabled() {
    return paimon::modules::isEnabled("paimbnails.physics.editor");
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

void openLab() {
    if (!enabled()) return;
    auto* editor = LevelEditorLayer::get();
    auto* ui = editor ? editor->m_editorUI : nullptr;
    if (!ui) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("physics-lab-popup"_spr)) return;

    auto& workspace = paimon::editorphysics::PhysicsWorkspace::get();
    workspace.bind(ui);
    if (workspace.hasPendingCapture()) {
        auto result = workspace.consumePending(ui);
        if (result.isErr()) {
            PaimonNotify::show(result.unwrapErr(), NotificationIcon::Error, 4.f);
        } else {
            auto const report = result.unwrap();
            PaimonNotify::show(
                fmt::format(
                    "Cuerpo capturado: {} objeto{}{}.",
                    report.objects,
                    report.objects == 1 ? "" : "s",
                    report.group > 0 ? fmt::format(", grupo {}", report.group) : ", grupo automatico"
                ),
                NotificationIcon::Success
            );
        }
    } else if (workspace.empty()) {
        auto result = workspace.capture(ui, paimon::editorphysics::CaptureRole::ReplaceA);
        if (result.isOk()) {
            PaimonNotify::show("La seleccion actual se capturo como cuerpo A.", NotificationIcon::Success);
        }
    }

    if (auto* popup = paimon::editorphysics::PhysicsPopup::create()) popup->show();
}

} // namespace

class $modify(PaimonPhysicsEditorUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!enabled()) return true;

        auto* button = paimon::editor::assets::circleButton(
            "paim_physics.png",
            {"GJ_gravityBtn_001.png", "GJ_moveBtn_001.png", "GJ_optionsBtn_001.png"},
            0.68f,
            CircleBaseColor::Green,
            [] { openLab(); },
            CircleBaseSize::Tiny
        );
        if (!button) return true;
        button->setID("physics-lab-button"_spr);

        if (auto* menu = hostMenu(this)) {
            menu->addChild(button);
            if (menu->getLayout()) menu->updateLayout();
        } else {
            auto* fallback = CCMenu::create();
            fallback->setID("physics-lab-menu"_spr);
            fallback->setPosition({28.f, CCDirector::get()->getWinSize().height - 164.f});
            fallback->addChild(button);
            addChild(fallback, 100);
        }
        return true;
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editor-physics-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || !enabled() || !LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            openLab();
        }
    ).leak();
}
