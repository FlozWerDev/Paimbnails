// Confirm before paste state / paste color when many objects are selected.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

int selectedCount(EditorUI* ui) {
    if (!ui) return 0;
    int n = 0;
    if (ui->m_selectedObject) n = 1;
    if (ui->m_selectedObjects) {
        n = std::max(n, static_cast<int>(ui->m_selectedObjects->count()));
    }
    return n;
}

int threshold() {
    return static_cast<int>(moduleSetting<int64_t>("editor-mod-paste-warn-min-objects", 2));
}

} // namespace

class $modify(PaimonPasteWarnUI, EditorUI) {
    $override
    void onPasteState(CCObject* sender) {
        if (!moduleEnabled("editor-mod-paste-warnings")
            || !moduleSetting<bool>("editor-mod-paste-warn-state", true)
            || selectedCount(this) < threshold()) {
            return EditorUI::onPasteState(sender);
        }
        PopupManager::get().quickPopup(
            "Paste State",
            fmt::format(
                "Paste state onto <cy>{}</c> selected objects?",
                selectedCount(this)
            ),
            "Cancel", "Paste",
            [this, sender](FLAlertLayer*, bool ok) {
                if (ok) EditorUI::onPasteState(sender);
            }
        ).showInstant();
    }

    $override
    void onPasteColor(CCObject* sender) {
        if (!moduleEnabled("editor-mod-paste-warnings")
            || !moduleSetting<bool>("editor-mod-paste-warn-color", true)
            || selectedCount(this) < threshold()) {
            return EditorUI::onPasteColor(sender);
        }
        PopupManager::get().quickPopup(
            "Paste Color",
            fmt::format(
                "Paste color onto <cy>{}</c> selected objects?",
                selectedCount(this)
            ),
            "Cancel", "Paste",
            [this, sender](FLAlertLayer*, bool ok) {
                if (ok) EditorUI::onPasteColor(sender);
            }
        ).showInstant();
    }
};
