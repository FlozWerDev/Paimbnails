// Object count summary from editor pause (top IDs via alert).

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <map>
#include <string>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonObjectSummaryPause, EditorPauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorPauseLayer::init");
    }

    void onObjectSummary(CCObject*) {
        auto* lel = LevelEditorLayer::get();
        if (!lel || !lel->m_objects) {
            PopupManager::get().alert("Object Summary", "No objects in this level.").showInstant();
            return;
        }

        std::map<int, int> counts;
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (obj) counts[obj->m_objectID]++;
        }

        std::multimap<int, int, std::greater<int>> byCount;
        for (auto const& [id, n] : counts) byCount.emplace(n, id);

        std::string body = fmt::format(
            "Total: <cy>{}</c> objects, <cg>{}</c> types\n\n",
            lel->m_objects->count(), counts.size()
        );
        int shown = 0;
        for (auto const& [n, id] : byCount) {
            body += fmt::format("ID {}  x{}\n", id, n);
            if (++shown >= 20) {
                body += "...";
                break;
            }
        }

        PopupManager::get().alert("Object Summary", body).showInstant();
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorPauseLayer::init(lel)) return false;
        if (!moduleEnabled("editor-mod-object-summary")) return true;

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("settings-menu"));
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("small-actions-menu"));
        if (!menu) {
            menu = CCMenu::create();
            menu->setPosition({60.f, 80.f});
            this->addChild(menu, 20);
        }

        // Custom: paim_object-summary.png  |  Fallback: text "Objects"
        auto* btn = assets::iconOrTextButton(
            assets::files::objectSummary, {},
            "Objects", "GJ_button_04.png", 0.5f, CircleBaseColor::Green,
            [this] { this->onObjectSummary(nullptr); }
        );
        if (!btn) return true;
        btn->setID("paimbnails/object-summary");
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
};
