#include "../CollabManager.hpp"
#include "../CollabOverlay.hpp"
#include "../CollabPopups.hpp"

#include <Geode/binding/UndoObject.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/LevelSettingsLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

namespace {

bool collabEnabled() {
    return Mod::get()->getSettingValue<bool>("collab-enabled");
}

void showBlocked(std::string const& name) {
    FLAlertLayer::create(
        "Collab Editor",
        fmt::format("El host no permitio cambiar <cy>{}</c> en esta sala.", name),
        "OK"
    )->show();
}

// After an undo/redo, re-sync the objects the action touched: re-added objects
// (they have a parent again) become adds/updates, removed ones become deletes.
// The array may also hold GameObjectCopy entries (transform undos); those are
// skipped here and picked up by the selection reconcile pass.
void syncAfterUndoRedo(cocos2d::CCArray* affected, EditorUI* ui) {
    auto& mgr = paimon::collab::CollabManager::get();
    if (!mgr.connected()) return;
    if (affected) {
        for (auto* item : CCArrayExt<CCObject*>(affected)) {
            auto* o = typeinfo_cast<GameObject*>(item);
            if (!o) continue;
            if (o->getParent()) {
                mgr.sendUpdatedObject(o);
            } else {
                mgr.sendDeletedObject(o);
            }
        }
    }
    if (ui) mgr.reconcileObjects(ui->getSelectedObjects());
}

} // namespace

class $modify(PaimonCollabEditLevelLayer, EditLevelLayer) {
    $override
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        if (!collabEnabled()) return true;

        auto* folderMenu = typeinfo_cast<CCMenu*>(this->getChildByID("folder-menu"));
        if (!folderMenu) return true;

        auto* spr = CircleButtonSprite::createWithSpriteFrameName(
            "paim_Paimon.png"_spr, 1.f, CircleBaseColor::Green, CircleBaseSize::Small
        );
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonCollabEditLevelLayer::onCollab));
        btn->setID("collab-button"_spr);
        folderMenu->addChild(btn);
        folderMenu->updateLayout();
        return true;
    }

    void onCollab(CCObject*) {
        if (!paimon::collab::isUnlocked()) {
            if (auto* popup = paimon::collab::CollabGatePopup::create()) popup->show();
            return;
        }
        if (auto* popup = paimon::collab::CollabRoomPopup::create(m_level)) popup->show();
    }
};

class $modify(PaimonCollabLevelEditorLayer, LevelEditorLayer) {
    struct Fields {
        // Keep the actual editor pointer: LevelEditorLayer::get() already
        // returns null while the scene is being torn down, so clearing by it
        // would leave the manager holding a freed editor/overlay.
        LevelEditorLayer* m_self = nullptr;
        ~Fields() {
            paimon::collab::CollabManager::get().clearEditor(m_self);
        }
    };

    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        if (!collabEnabled()) return true;
        m_fields->m_self = this;
        paimon::collab::CollabManager::get().setEditor(this);
        if (auto* overlay = paimon::collab::CollabEditorOverlay::create(this)) {
            this->addChild(overlay, 10000);
        }
        this->schedule(schedule_selector(PaimonCollabLevelEditorLayer::collabTick), 0.05f);
        return true;
    }

    void collabTick(float) {
        paimon::collab::CollabManager::get().tick();
    }

    $override
    void removeObject(GameObject* object, bool noUndo) {
        if (object && !paimon::collab::CollabManager::get().isApplyingRemote()) {
            paimon::collab::CollabManager::get().sendDeletedObject(object);
        }
        LevelEditorLayer::removeObject(object, noUndo);
    }
};

class $modify(PaimonCollabEditorUI, EditorUI) {
    $override
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        // Ctrl+E (Cmd+E on mac) opens the collab chat popup while in a room.
        if (collabEnabled() && key == cocos2d::KEY_E &&
            paimon::collab::CollabManager::get().connected()) {
            auto* kb = CCDirector::sharedDirector()->getKeyboardDispatcher();
            if (kb && (kb->getControlKeyPressed() || kb->getCommandKeyPressed())) {
                auto* scene = CCDirector::sharedDirector()->getRunningScene();
                if (!scene || !scene->getChildByID("collab-chat"_spr)) {
                    if (auto* popup = paimon::collab::CollabChatPopup::create()) popup->show();
                }
                return;
            }
        }
        EditorUI::keyDown(key, timestamp);
    }

    $override
    GameObject* createObject(int objectID, CCPoint position) {
        auto* object = EditorUI::createObject(objectID, position);
        paimon::collab::CollabManager::get().sendCreatedObject(object);
        return object;
    }

    $override
    CCArray* pasteObjects(gd::string str, bool withColor, bool noUndo) {
        auto* objects = EditorUI::pasteObjects(str, withColor, noUndo);
        paimon::collab::CollabManager::get().sendCreatedObjects(objects);
        return objects;
    }

    $override
    void onDuplicate(CCObject* sender) {
        EditorUI::onDuplicate(sender);
        // Duplicates end up selected; reconcile registers any untracked copy.
        paimon::collab::CollabManager::get().reconcileObjects(this->getSelectedObjects());
    }

    $override
    void undoLastAction(CCObject* sender) {
        Ref<CCArray> affected;
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && m_editorLayer && m_editorLayer->m_undoObjects && m_editorLayer->m_undoObjects->count() > 0) {
            if (auto* undo = typeinfo_cast<UndoObject*>(m_editorLayer->m_undoObjects->lastObject())) {
                affected = undo->m_objects;
            }
        }
        EditorUI::undoLastAction(sender);
        syncAfterUndoRedo(affected, this);
    }

    $override
    void redoLastAction(CCObject* sender) {
        Ref<CCArray> affected;
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && m_editorLayer && m_editorLayer->m_redoObjects && m_editorLayer->m_redoObjects->count() > 0) {
            if (auto* redo = typeinfo_cast<UndoObject*>(m_editorLayer->m_redoObjects->lastObject())) {
                affected = redo->m_objects;
            }
        }
        EditorUI::redoLastAction(sender);
        syncAfterUndoRedo(affected, this);
    }

    $override
    void moveObject(GameObject* object, CCPoint offset) {
        EditorUI::moveObject(object, offset);
        paimon::collab::CollabManager::get().sendUpdatedObject(object);
    }

    $override
    void transformObject(GameObject* object, EditCommand command, bool noOffset) {
        EditorUI::transformObject(object, command, noOffset);
        paimon::collab::CollabManager::get().sendUpdatedObject(object);
    }

    $override
    void transformObjects(CCArray* objects, CCPoint anchor, float scaleX, float scaleY, float rotateX, float rotateY, float warpX, float warpY) {
        EditorUI::transformObjects(objects, anchor, scaleX, scaleY, rotateX, rotateY, warpX, warpY);
        paimon::collab::CollabManager::get().sendUpdatedObjects(objects);
    }

    $override
    void scaleObjects(CCArray* objects, float scaleX, float scaleY, CCPoint pivotPoint, ObjectScaleType type, bool lockMove) {
        EditorUI::scaleObjects(objects, scaleX, scaleY, pivotPoint, type, lockMove);
        paimon::collab::CollabManager::get().sendUpdatedObjects(objects);
    }

    $override
    void rotateObjects(CCArray* objects, float rotation, CCPoint pivotPoint) {
        EditorUI::rotateObjects(objects, rotation, pivotPoint);
        paimon::collab::CollabManager::get().sendUpdatedObjects(objects);
    }
};

// Green "live" dot on the level cell whose level is the active collab room, so
// the host can spot it and pop back in after leaving the editor.
class $modify(PaimonCollabLevelCell, LevelCell) {
    $override
    void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);
        if (auto* old = this->getChildByID("collab-live-dot"_spr)) old->removeFromParent();
        if (!collabEnabled()) return;

        auto& mgr = paimon::collab::CollabManager::get();
        if (!level || !mgr.connected() || !mgr.isHost() || level != mgr.hostLevel()) return;

        auto* dot = CCDrawNode::create();
        dot->drawDot({0.f, 0.f}, 6.f, ccColor4F{0.29f, 0.87f, 0.35f, 1.f});
        dot->setID("collab-live-dot"_spr);
        dot->setZOrder(50);
        dot->setPosition({10.f, this->getContentSize().height - 12.f});
        this->addChild(dot);
    }
};

class $modify(PaimonCollabEditorPauseLayer, EditorPauseLayer) {
    $override
    void onSong(CCObject* sender) {
        if (!paimon::collab::CollabManager::get().clientCanOpenSong()) {
            showBlocked("musica");
            return;
        }
        EditorPauseLayer::onSong(sender);
    }

    $override
    void onOptions(CCObject* sender) {
        if (!paimon::collab::CollabManager::get().clientCanOpenOptions()) {
            showBlocked("options");
            return;
        }
        EditorPauseLayer::onOptions(sender);
    }
};

class $modify(PaimonCollabLevelSettingsLayer, LevelSettingsLayer) {
    bool collabModeBlocked() {
        if (paimon::collab::CollabManager::get().clientCanOpenLevelSettings()) return false;
        showBlocked("mode");
        return true;
    }

    $override
    void onMode(CCObject* sender) {
        if (collabModeBlocked()) return;
        LevelSettingsLayer::onMode(sender);
    }

    $override
    void onGameplayMode(CCObject* sender) {
        if (collabModeBlocked()) return;
        LevelSettingsLayer::onGameplayMode(sender);
    }

    $override
    void onSettings(CCObject* sender) {
        if (collabModeBlocked()) return;
        LevelSettingsLayer::onSettings(sender);
    }

    $override
    void onSpeed(CCObject* sender) {
        if (collabModeBlocked()) return;
        LevelSettingsLayer::onSpeed(sender);
    }
};
