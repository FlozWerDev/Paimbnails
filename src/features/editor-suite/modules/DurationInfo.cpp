// Show selected trigger duration near the HUD.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonDurationHUD, EditorUI) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void durationTick(float) {
        if (!moduleEnabled("editor-mod-duration-info") || !m_fields->label) return;
        auto sel = paimon::editor::getSelectedObjects(this);
        EffectGameObject* trig = nullptr;
        for (auto* o : sel) {
            if (auto* e = typeinfo_cast<EffectGameObject*>(o)) {
                trig = e;
                break;
            }
        }
        if (!trig || trig->m_duration <= 0.f) {
            m_fields->label->setVisible(false);
            return;
        }
        m_fields->label->setVisible(true);
        m_fields->label->setString(
            fmt::format("Dur: {:.2f}s  (ID {})", trig->m_duration, trig->m_objectID).c_str()
        );
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-duration-info")) return true;

        // Centered under the top HUD pills — the bottom-left corner belongs
        // to the BUILD/EDIT/DELETE menu.
        auto* lab = CCLabelBMFont::create("", "bigFont.fnt");
        lab->setScale(0.28f);
        lab->setAnchorPoint({0.5f, 1.f});
        lab->setID("paimbnails/duration-hud");
        auto win = CCDirector::get()->getWinSize();
        lab->setPosition({win.width / 2.f, win.height - 64.f});
        lab->setOpacity(210);
        lab->setVisible(false);
        this->addChild(lab, 45);
        m_fields->label = lab;
        this->schedule(schedule_selector(PaimonDurationHUD::durationTick), 0.1f);
        return true;
    }
};
