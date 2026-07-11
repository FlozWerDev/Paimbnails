// Hide numeric IDs on Edit Level screen unless Shift is held.

#include "../EditorModule.hpp"

#include <Geode/binding/EditLevelLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <string>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonHideLevelIDs, EditLevelLayer) {
    struct Fields {
        std::vector<CCLabelBMFont*> idLabels;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditLevelLayer::init");
    }

    void collectIdLabels(CCNode* node) {
        if (!node) return;
        if (auto* lab = typeinfo_cast<CCLabelBMFont*>(node)) {
            auto s = std::string(lab->getString());
            // Heuristic: pure numbers or "ID: 123"
            bool looksId = false;
            if (s.rfind("ID", 0) == 0 || s.find("id") != std::string::npos) looksId = true;
            if (!looksId) {
                looksId = !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) || c == ' ' || c == '#';
                });
                // Avoid hiding short version-like numbers only if length >= 3
                if (looksId && s.size() < 2) looksId = false;
            }
            if (looksId) m_fields->idLabels.push_back(lab);
        }
        for (auto* c : CCArrayExt<CCNode*>(node->getChildren())) {
            collectIdLabels(c);
        }
    }

    void idTick(float) {
        if (!moduleEnabled("editor-mod-hide-level-ids")) return;
        auto* kd = CCKeyboardDispatcher::get();
        bool show = kd && kd->getShiftKeyPressed();
        for (auto* lab : m_fields->idLabels) {
            if (lab) lab->setVisible(show);
        }
    }

    $override
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        if (!moduleEnabled("editor-mod-hide-level-ids")) return true;

        // Defer collection until children exist
        Loader::get()->queueInMainThread([self = Ref(this)] {
            auto* me = static_cast<PaimonHideLevelIDs*>(self.data());
            if (!me) return;
            me->m_fields->idLabels.clear();
            me->collectIdLabels(me);
            // Also try known node IDs
            if (auto* n = me->getChildByIDRecursive("level-id-label")) {
                if (auto* lab = typeinfo_cast<CCLabelBMFont*>(n)) {
                    me->m_fields->idLabels.push_back(lab);
                }
            }
            for (auto* lab : me->m_fields->idLabels) {
                if (lab) lab->setVisible(false);
            }
            me->schedule(schedule_selector(PaimonHideLevelIDs::idTick), 0.05f);
        });
        return true;
    }
};
