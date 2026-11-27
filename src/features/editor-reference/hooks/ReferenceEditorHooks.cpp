#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../services/ReferenceSession.hpp"
#include "../services/ReferenceStore.hpp"
#include "../ui/ReferenceManagerPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

using namespace geode::prelude;
using paimon::editorref::ReferenceSession;

namespace {

bool enabled() {
    return paimon::modules::isEnabled("paimbnails.reference.editor");
}

void openReferenceManager(int initialSelectedId = 0) {
    if (!enabled()) return;
    auto* editor = LevelEditorLayer::get();
    if (!editor) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("reference-manager-popup"_spr)) return;
    if (initialSelectedId <= 0 && editor->m_editorUI) {
        initialSelectedId = ReferenceSession::get().singleSelectedReferenceId(editor->m_editorUI);
    }
    if (auto* popup = paimon::editorref::ReferenceManagerPopup::create(initialSelectedId)) {
        popup->show();
    }
}

// Lightweight lifetime sentinel attached to LevelEditorLayer so ReferenceSession detaches cleanly.
class ReferenceEditorSentinel : public cocos2d::CCNode {
public:
    static ReferenceEditorSentinel* create(LevelEditorLayer* editor) {
        auto* node = new ReferenceEditorSentinel();
        if (node && node->init()) {
            node->m_editor = editor;
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void cleanup() override {
        if (m_editor) {
            ReferenceSession::get().detach(m_editor, true);
            m_editor = nullptr;
        }
        CCNode::cleanup();
    }

    ~ReferenceEditorSentinel() override {
        if (m_editor) {
            ReferenceSession::get().detach(m_editor, false);
            m_editor = nullptr;
        }
    }

private:
    LevelEditorLayer* m_editor = nullptr;
};

} // namespace

class $modify(PaimonReferenceLevelIdentity, GJGameLevel) {
    void dataLoaded(DS_Dictionary* dict) {
        GJGameLevel::dataLoaded(dict);
        if (dict) {
            auto const identity = std::string(
                dict->getStringForKey(paimon::editorref::kLevelIdentityKey)
            );
            if (!identity.empty()) {
                paimon::editorref::store::restoreLevelIdentity(this, identity);
            }
        }
    }

    void encodeWithCoder(DS_Dictionary* dict) override {
        GJGameLevel::encodeWithCoder(dict);
        if (!dict) return;
        if (auto* identity = typeinfo_cast<CCString*>(
                getUserObject(paimon::editorref::kLevelIdentityKey))) {
            dict->setStringForKey(paimon::editorref::kLevelIdentityKey, identity->getCString());
        }
    }

    void copyLevelInfo(GJGameLevel* source) {
        GJGameLevel::copyLevelInfo(source);
        paimon::editorref::store::copyLevelDocument(source, this);
    }
};

class $modify(PaimonReferenceEditorLayer, LevelEditorLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "LevelEditorLayer::init");
    }

    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        if (noUI || !enabled()) return true;
        ReferenceSession::get().attach(this);
        if (auto* sentinel = ReferenceEditorSentinel::create(this)) {
            sentinel->setID("reference-editor-sentinel"_spr);
            this->addChild(sentinel);
        }
        return true;
    }

    $override
    void updateVisibility(float dt) {
        LevelEditorLayer::updateVisibility(dt);
        ReferenceSession::get().syncAll(this);
    }

    $override
    void removeObject(GameObject* object, bool noUndo) {
        ReferenceSession::get().onObjectRemoved(this, object);
        LevelEditorLayer::removeObject(object, noUndo);
    }

    $override
    std::string getLevelString() {
        auto stashed = ReferenceSession::get().stripAnchorsForSerialization(this);
        auto result = LevelEditorLayer::getLevelString();
        ReferenceSession::get().restoreAnchorsAfterSerialization(this, stashed);
        return result;
    }

    $override
    bool saveLevel() {
        bool const attached = ReferenceSession::get().isAttached(this);
        if (attached) {
            ReferenceSession::get().syncAll(this);
        }
        bool const saved = LevelEditorLayer::saveLevel();
        if (attached) {
            ReferenceSession::get().persist(this);
        }
        return saved;
    }
};

class $modify(PaimonReferenceEditorUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!enabled()) return true;

        auto* button = paimon::editor::assets::squareButton(
            "paim_editor_reference.png",
            {"GJ_paintBtn_001.png", "GJ_optionsBtn_001.png"},
            0.88f,
            EditorBaseColor::Green,
            [] { openReferenceManager(); },
            paimon::editor::toolbarToggleSize(this)
        );
        if (!button) return true;
        button->setID("reference-image-button"_spr);

        paimon::editor::addToolbarButton(
            this,
            button,
            "reference-image-menu"_spr,
            {28.f, CCDirector::get()->getWinSize().height - 304.f}
        );
        return true;
    }

    $override
    void selectObject(GameObject* object, bool ignoreFilter) {
        if (ReferenceSession::get().isLockedAnchor(m_editorLayer, object)) return;
        EditorUI::selectObject(object, ignoreFilter);
    }

    $override
    void selectObjects(CCArray* objects, bool ignoreFilter) {
        if (!objects || !ReferenceSession::get().isAttached(m_editorLayer)) {
            return EditorUI::selectObjects(objects, ignoreFilter);
        }
        auto* allowed = CCArray::create();
        bool filtered = false;
        for (auto* object : CCArrayExt<GameObject*>(objects)) {
            if (ReferenceSession::get().isLockedAnchor(m_editorLayer, object)) {
                filtered = true;
                continue;
            }
            allowed->addObject(object);
        }
        EditorUI::selectObjects(filtered ? allowed : objects, ignoreFilter);
    }

    $override
    void clickOnPosition(CCPoint position) {
        EditorUI::clickOnPosition(position);
        if (!enabled() || m_selectedMode != 3) return; // Edit mode only
        if (m_selectedObject || (m_selectedObjects && m_selectedObjects->count() > 0)) return;
        if (auto* anchor = ReferenceSession::get().pickUnlockedAnchorAtScreenPos(m_editorLayer, position)) {
            EditorUI::selectObject(anchor, true);
            this->updateButtons();
        }
    }

    $override
    void onDeleteSelected(CCObject* sender) {
        if (enabled() && ReferenceSession::get().isAttached(m_editorLayer)) {
            if (ReferenceSession::get().deleteSelectedAnchors(this)) {
                this->updateButtons();
                return;
            }
        }
        EditorUI::onDeleteSelected(sender);
    }

    $override
    void editObject(CCObject* sender) {
        if (enabled() && ReferenceSession::get().isAttached(m_editorLayer)) {
            int const refId = ReferenceSession::get().singleSelectedReferenceId(this);
            if (refId > 0) {
                openReferenceManager(refId);
                return;
            }
        }
        EditorUI::editObject(sender);
    }

    $override
    void undoLastAction(CCObject* object) {
        EditorUI::undoLastAction(object);
        ReferenceSession::get().reconcileUndoRedo(m_editorLayer);
    }

    $override
    void redoLastAction(CCObject* sender) {
        EditorUI::redoLastAction(sender);
        ReferenceSession::get().reconcileUndoRedo(m_editorLayer);
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editor-reference-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || !enabled() || !LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            openReferenceManager();
        }
    ).leak();
}
