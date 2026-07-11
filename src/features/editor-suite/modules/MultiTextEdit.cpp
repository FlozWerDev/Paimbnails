// When a TextGameObject's string changes, apply it to all selected text objects.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/TextGameObject.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <string>

using namespace geode::prelude;
using namespace paimon::editor;

namespace {
bool g_propagating = false;
}

class $modify(PaimonMultiTextUI, EditorUI) {
    struct Fields {
        std::string lastText;
        TextGameObject* watched = nullptr;
    };

    void multiTextTick(float) {
        if (!moduleEnabled("editor-mod-multi-text") || g_propagating) return;
        auto sel = paimon::editor::getSelectedObjects(this);
        TextGameObject* primary = nullptr;
        int textCount = 0;
        for (auto* o : sel) {
            if (auto* t = typeinfo_cast<TextGameObject*>(o)) {
                ++textCount;
                if (!primary) primary = t;
            }
        }
        if (textCount < 2 || !primary) {
            m_fields->watched = nullptr;
            m_fields->lastText.clear();
            return;
        }
        std::string cur = primary->m_text;
        if (m_fields->watched == primary && !m_fields->lastText.empty()
            && cur != m_fields->lastText) {
            g_propagating = true;
            for (auto* o : sel) {
                if (auto* t = typeinfo_cast<TextGameObject*>(o)) {
                    if (t == primary) continue;
                    t->updateTextObject(cur, false);
                }
            }
            g_propagating = false;
        }
        m_fields->watched = primary;
        m_fields->lastText = cur;
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (moduleEnabled("editor-mod-multi-text")) {
            this->schedule(schedule_selector(PaimonMultiTextUI::multiTextTick), 0.15f);
        }
        return true;
    }
};
