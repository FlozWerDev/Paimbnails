// When copying objects in the editor, also put the object string on the system clipboard.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonCopyStringUI, EditorUI) {
    $override
    void onCopy(CCObject* sender) {
        EditorUI::onCopy(sender);
        if (!moduleEnabled("editor-mod-copy-string")) return;

        // Build string from current selection (post-copy internal clipboard)
        auto* arr = CCArray::create();
        if (m_selectedObject) arr->addObject(m_selectedObject);
        if (m_selectedObjects) {
            for (auto* o : CCArrayExt<GameObject*>(m_selectedObjects)) {
                if (o && o != m_selectedObject) arr->addObject(o);
            }
        }
        if (!arr->count()) return;

        auto str = this->copyObjects(arr, true, true);
        if (str.empty()) return;
        geode::utils::clipboard::write(str);
    }
};
