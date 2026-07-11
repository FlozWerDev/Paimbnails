// Hold Alt and click a selected object to remove only that object from selection.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonSingleDeselectUI, EditorUI) {
    $override
    void selectObject(GameObject* obj, bool filter) {
        if (!moduleEnabled("editor-mod-single-deselect") || !obj) {
            return EditorUI::selectObject(obj, filter);
        }

        auto* keys = CCKeyboardDispatcher::get();
        bool alt = keys && keys->getAltKeyPressed();
        if (!alt) {
            return EditorUI::selectObject(obj, filter);
        }

        // If object is already selected, deselect only it
        bool wasSelected = false;
        if (m_selectedObject == obj) wasSelected = true;
        if (!wasSelected && m_selectedObjects) {
            for (auto* o : CCArrayExt<GameObject*>(m_selectedObjects)) {
                if (o == obj) {
                    wasSelected = true;
                    break;
                }
            }
        }

        if (!wasSelected) {
            // Alt+click unselected: still add to selection (vanilla multi)
            return EditorUI::selectObject(obj, filter);
        }

        // Deselect this single object via vanilla API
        this->deselectObject(obj);
        this->updateButtons();
        this->updateObjectInfoLabel();
    }
};
