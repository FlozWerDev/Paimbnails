// Editor history for Color / Groups / Layers.
// Toolbar button opens a scrollable history browser with filters and per-entry undo.

#include "../services/ObjectTimelineStore.hpp"
#include "../ui/EditorHistoryPopup.hpp"
#include "../../editor-suite/EditorAssets.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;
using namespace paimon::editorhistory;

namespace {
bool featureOn() { return historyEnabled(); }
}

// ---------------------------------------------------------------------------
class $modify(PaimonHistoryLEL, LevelEditorLayer) {
    struct Fields {
        LevelEditorLayer* self = nullptr;
        ~Fields() {
            if (ObjectTimelineStore::get().editor() == self)
                ObjectTimelineStore::get().clear();
            EditorUndoPanel::closeIfOpen();
        }
    };

    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        m_fields->self = this;
        ObjectTimelineStore::get().clear();
        ObjectTimelineStore::get().setEditor(this);
        if (featureOn())
            this->schedule(schedule_selector(PaimonHistoryLEL::historyPoll), 0.1f);
        return true;
    }

    void historyPoll(float) {
        if (!featureOn()) return;
        ObjectTimelineStore::get().setEditor(this);
        ObjectTimelineStore::get().tick();
    }
};

// ---------------------------------------------------------------------------
class $modify(PaimonHistoryEditorUI, EditorUI) {
    void onEditorUndo(CCObject*) {
        if (featureOn()) EditorUndoPanel::toggle(this);
    }

    $override
    void colorSelectClosed(CCNode* popup) {
        EditorUI::colorSelectClosed(popup);
        if (featureOn()) ObjectTimelineStore::get().tick();
    }

    $override
    void onPasteColor(CCObject* sender) {
        EditorUI::onPasteColor(sender);
        if (featureOn()) ObjectTimelineStore::get().tick();
    }

    $override
    void closeLiveColorSelect() {
        EditorUI::closeLiveColorSelect();
        if (featureOn()) ObjectTimelineStore::get().tick();
    }

    $override
    void closeLiveHSVSelect() {
        EditorUI::closeLiveHSVSelect();
        if (featureOn()) ObjectTimelineStore::get().tick();
    }

    $override
    bool init(LevelEditorLayer* editor) {
        if (!EditorUI::init(editor)) return false;
        if (!featureOn()) return true;

        CCNode* menuNode = this->querySelector("editor-buttons-menu");
        if (!menuNode && m_undoBtn) menuNode = m_undoBtn->getParent();
        if (!menuNode || menuNode->getChildByID("paimbnails/editor-history-btn")) return true;

        // Custom: paim_editor-history.png  |  Fallback: undo / time / info
        CCSprite* icon = paimon::editor::assets::loadIcon(
            paimon::editor::assets::files::editorHistory,
            { "edit_undoBtn_001.png", "GJ_timeIcon_001.png", "GJ_infoIcon_001.png" },
            1.f
        );
        if (!icon) return true;

        CCNode* spr = EditorButtonSprite::create(icon, EditorBaseColor::Gray);
        if (!spr) {
            icon->setScale(0.75f);
            spr = icon;
        }

        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(PaimonHistoryEditorUI::onEditorUndo)
        );
        btn->setID("paimbnails/editor-history-btn");
        paimon::editor::assets::fitGridItem(btn, menuNode);
        menuNode->addChild(btn);
        if (m_uiItems) m_uiItems->addObject(btn);
        if (auto* menu = typeinfo_cast<CCMenu*>(menuNode)) menu->updateLayout();
        return true;
    }
};
