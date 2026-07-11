// Show approximate editor position percentage in the HUD (Tinker/BE historical idea).
// Updates while panning; cheap sample of max X.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-editor-percentage"); }
}

class $modify(PaimonEditorPctUI, EditorUI) {
    struct Fields {
        Ref<CCLabelBMFont> label;
        float cachedMaxX = 100.f;
        int tick = 0;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void refreshMaxX() {
        if (!m_editorLayer || !m_editorLayer->m_objects) return;
        float maxX = 100.f;
        unsigned n = m_editorLayer->m_objects->count();
        unsigned step = n > 300u ? n / 300u : 1u;
        for (unsigned i = 0; i < n; i += step) {
            if (auto* o = typeinfo_cast<GameObject*>(m_editorLayer->m_objects->objectAtIndex(i))) {
                maxX = std::max(maxX, o->getPositionX());
            }
        }
        m_fields->cachedMaxX = maxX;
    }

    void pctTick(float) {
        if (!on() || !m_fields->label || !m_editorLayer || !m_editorLayer->m_objectLayer) return;
        if (++m_fields->tick % 30 == 0) refreshMaxX(); // every ~1.5s at 20Hz
        auto win = CCDirector::get()->getWinSize();
        auto cam = m_editorLayer->m_objectLayer->convertToNodeSpace(win / 2.f);
        float pct = std::clamp(cam.x / std::max(m_fields->cachedMaxX, 1.f), 0.f, 1.f) * 100.f;
        m_fields->label->setString(fmt::format("{:.1f}%", pct).c_str());
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!on()) return true;

        auto* lab = CCLabelBMFont::create("0.0%", "chatFont.fnt");
        lab->setScale(0.4f);
        lab->setOpacity(200);
        lab->setID("paimbnails/editor-percentage");
        auto win = CCDirector::get()->getWinSize();
        lab->setPosition({win.width * 0.5f, win.height - 14.f});
        this->addChild(lab, 40);
        if (m_uiItems) m_uiItems->addObject(lab);
        m_fields->label = lab;
        refreshMaxX();
        this->schedule(schedule_selector(PaimonEditorPctUI::pctTick), 0.05f);
        return true;
    }
};
