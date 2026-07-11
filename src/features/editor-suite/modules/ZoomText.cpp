// Shows a brief zoom percentage label when zooming.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

class $modify(PaimonZoomTextUI, EditorUI) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-zoom-text")) return true;

        auto* label = CCLabelBMFont::create("100%", "bigFont.fnt");
        label->setScale(0.45f);
        label->setOpacity(0);
        label->setID("paimbnails/zoom-level-text");
        auto win = CCDirector::get()->getWinSize();
        label->setPosition({win.width / 2.f, win.height * 0.72f});
        this->addChild(label, 50);
        m_fields->label = label;
        return true;
    }

    $override
    void updateZoom(float zoom) {
        EditorUI::updateZoom(zoom);
        if (!moduleEnabled("editor-mod-zoom-text")) return;
        auto* label = m_fields->label;
        if (!label) return;

        int pct = static_cast<int>(std::round(zoom * 100.f));
        label->setString(fmt::format("{}%", pct).c_str());
        label->stopAllActions();
        label->setOpacity(220);
        label->runAction(CCSequence::create(
            CCDelayTime::create(0.45f),
            CCFadeTo::create(0.35f, 0),
            nullptr
        ));
    }
};
