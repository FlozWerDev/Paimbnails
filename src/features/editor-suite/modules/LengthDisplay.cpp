// Shows approximate level length (seconds) and object count in the editor HUD.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

// Rough estimate: last object X / (speed-dependent units per second).
// Vanilla uses ~311.58 units/sec at 1x for normal speed roughly via timewarp;
// we use a simple 311.f heuristic matching common editor length tools.
float estimateSeconds(LevelEditorLayer* lel) {
    if (!lel || !lel->m_objects) return 0.f;
    float maxX = 0.f;
    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        maxX = std::max(maxX, obj->getPositionX());
    }
    constexpr float kUnitsPerSec = 311.58f;
    return maxX / kUnitsPerSec;
}

std::string formatTime(float sec) {
    if (sec < 0.f) sec = 0.f;
    int total = static_cast<int>(std::round(sec));
    int m = total / 60;
    int s = total % 60;
    return fmt::format("{}:{:02d}", m, s);
}

} // namespace

class $modify(PaimonLengthUI, EditorUI) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void refreshLengthLabel() {
        if (!moduleEnabled("editor-mod-length-display") || !m_fields->label) return;
        auto* lel = m_editorLayer;
        if (!lel) return;
        float sec = estimateSeconds(lel);
        int objs = lel->m_objects ? lel->m_objects->count() : 0;
        m_fields->label->setString(
            fmt::format("Len {} | {} objs", formatTime(sec), objs).c_str()
        );
    }

    void lengthTick(float) {
        refreshLengthLabel();
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-length-display")) return true;

        auto* label = CCLabelBMFont::create("Len 0:00", "bigFont.fnt");
        label->setScale(0.28f);
        label->setAnchorPoint({0.f, 1.f});
        label->setID("paimbnails/length-display");
        auto win = CCDirector::get()->getWinSize();
        label->setPosition({8.f, win.height - 8.f});
        label->setOpacity(200);
        this->addChild(label, 40);
        m_fields->label = label;

        this->schedule(schedule_selector(PaimonLengthUI::lengthTick), 0.5f);
        refreshLengthLabel();
        return true;
    }
};
