// Center the camera on the level content / selection (BetterEdit BackToContent idea).

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"
#include "../EditorAssets.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {
bool on() { return moduleEnabled("editor-mod-back-to-content"); }

void focusContent(EditorUI* ui) {
    if (!ui || !ui->m_editorLayer || !ui->m_editorLayer->m_objectLayer) return;
    auto* editor = ui->m_editorLayer;
    auto const selected = paimon::editor::getSelectedObjects(ui);
    if (!selected.empty()) {
        focusCameraOnSelection(ui);
        return;
    }
    if (!editor->m_objects || editor->m_objects->count() == 0) {
        focusCameraOnPoint(editor, {0.f, 0.f});
        return;
    }

    float sumX = 0.f;
    float sumY = 0.f;
    unsigned count = 0;
    unsigned const objectCount = editor->m_objects->count();
    unsigned const step = objectCount > 200u ? objectCount / 200u : 1u;
    for (unsigned i = 0; i < objectCount; i += step) {
        if (auto* object = typeinfo_cast<GameObject*>(editor->m_objects->objectAtIndex(i))) {
            auto const position = object->getPosition();
            sumX += position.x;
            sumY += position.y;
            ++count;
        }
    }
    if (count == 0) return;
    focusCameraOnPoint(editor, {sumX / count, sumY / count});
    ui->updateSlider();
}
}

class $modify(PaimonBackToContentPause, EditorPauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorPauseLayer::init");
    }

    void onBackToContent(CCObject*) {
        if (on() && m_editorLayer) focusContent(m_editorLayer->m_editorUI);
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorPauseLayer::init(lel)) return false;
        if (!on()) return true;

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("small-actions-menu"));
        if (!menu) return true;

        if (auto* btn = iconOrTextButton(
                files::backToContent,
                { "GJ_arrow_01_001.png", "GJ_playBtn2_001.png" },
                "Reset\nView", "GJ_button_04.png", 0.35f,
                CircleBaseColor::Cyan,
                [this] { this->onBackToContent(nullptr); }
            )) {
            btn->setID("paimbnails/back-to-content");
            menu->insertBefore(btn, nullptr);
            menu->updateLayout();
        }
        return true;
    }
};
