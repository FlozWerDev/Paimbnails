// Show high-detail (LDM) object count on editor pause.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

class $modify(PaimonLDMCountPause, EditorPauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorPauseLayer::init");
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorPauseLayer::init(lel)) return false;
        if (!moduleEnabled("editor-mod-ldm-count") || !lel || !lel->m_objects) return true;

        int total = static_cast<int>(lel->m_objects->count());
        int ldm = 0;
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (obj && obj->m_isHighDetail) ++ldm;
        }
        float pct = total > 0 ? (100.f * ldm / static_cast<float>(total)) : 0.f;

        auto text = fmt::format("LDM: {} / {} ({:.1f}%)", ldm, total, pct);
        auto* lab = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        lab->setScale(0.32f);
        lab->setID("paimbnails/ldm-count");
        auto win = CCDirector::get()->getWinSize();
        lab->setPosition({win.width / 2.f, 48.f});
        lab->setOpacity(220);
        this->addChild(lab, 20);
        return true;
    }
};
