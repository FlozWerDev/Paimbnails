#include "../CollabManager.hpp"
#include "../CollabOverlay.hpp"
#include "../CollabPopups.hpp"
#include "../../editor-suite/EditorAssets.hpp"

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
#include <Geode/ui/PopupManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>

using namespace geode::prelude;

namespace {

bool collabEnabled() {
    return Mod::get()->getSettingValue<bool>("collab-enabled");
}

void showBlocked(std::string const& name) {
    auto popup = PopupManager::get().alertFormat(
        "Collab Editor",
        "El host no permitio cambiar <cy>{}</c> en esta sala.",
        name
    );
    popup.setPriority(true);
    popup.showQueue();
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

// Cara del boton de collab: dos cubos de jugador con colores vivos (duo) y un
// rotulo "COLLAB" debajo, para que se entienda que abre salas entre amigos.
// SimplePlayer tiene contentSize 0, asi que va dentro de un CCNode con tamano
// fijo — CircleButtonSprite centra el top asumiendo un nodo medible.
CCMenuItemSpriteExtra* makeCollabButton(std::function<void()> onClick) {
    auto* wrap = CCNode::create();
    CCSize const sz{38.f, 34.f};
    wrap->setContentSize(sz);
    wrap->setAnchorPoint({0.5f, 0.5f});

    auto addPlayer = [&](float x, float scale, ccColor3B c1, ccColor3B c2, int z) {
        auto* p = SimplePlayer::create(1);
        if (!p) return;
        p->setColor(c1);
        p->setSecondColor(c2);
        p->setGlowOutline(c2);
        p->setScale(scale);
        p->setPosition({x, 20.f});
        wrap->addChild(p, z);
    };
    addPlayer(13.f, 0.60f, {0, 210, 255}, {0, 90, 210}, 0);   // cubo cian atras
    addPlayer(26.f, 0.66f, {255, 150, 40}, {255, 225, 90}, 1); // cubo naranja delante

    if (auto* label = CCLabelBMFont::create("COLLAB", "goldFont.fnt")) {
        label->limitLabelWidth(sz.width - 2.f, 0.4f, 0.05f);
        label->setPosition({sz.width / 2.f, 3.5f});
        wrap->addChild(label, 2);
    }

    auto* base = CircleButtonSprite::create(wrap, CircleBaseColor::Green, CircleBaseSize::Small);
    if (!base) return nullptr;
    base->setTopRelativeScale(1.f);
    return CCMenuItemExt::createSpriteExtra(
        base, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
}

} // namespace

class $modify(PaimonCollabEditLevelLayer, EditLevelLayer) {
    $override
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        if (!collabEnabled()) return true;

        auto* folderMenu = typeinfo_cast<CCMenu*>(this->getChildByID("folder-menu"));
        if (!folderMenu) return true;

        // Si el mod trae un PNG propio (paim_collab.png) se respeta; si no,
        // se compone la cara duo-de-cubos + "COLLAB" en vez del icono blanco.
        CCMenuItemSpriteExtra* btn = nullptr;
        if (paimon::editor::assets::hasCustom(paimon::editor::assets::files::collab)) {
            btn = paimon::editor::assets::circleButton(
                paimon::editor::assets::files::collab,
                { "accountBtn_friends_001.png" },
                0.75f,
                CircleBaseColor::Green,
                [this] { this->onCollab(nullptr); },
                CircleBaseSize::Small
            );
        } else {
            btn = makeCollabButton([this] { this->onCollab(nullptr); });
        }
        if (!btn) return true;
        btn->setID("collab-button"_spr);
        folderMenu->addChild(btn);
        folderMenu->updateLayout();
        return true;
    }

    void onCollab(CCObject*) {
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
        // Cheap remote apply path (setPosition) — alk MoveCommand style.
        paimon::collab::CollabManager::get().sendMovedObject(object);
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

    // --- Selection presence (alk SelectCommand + draw-selection-overlay) ---

    $override
    void selectObject(GameObject* object, bool ignoreFilter) {
        EditorUI::selectObject(object, ignoreFilter);
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendSelection(this->getSelectedObjects());
        }
    }

    $override
    void selectObjects(CCArray* objects, bool ignoreFilter) {
        EditorUI::selectObjects(objects, ignoreFilter);
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendSelection(this->getSelectedObjects());
        }
    }

    $override
    void deselectAll() {
        EditorUI::deselectAll();
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendSelection(nullptr);
        }
    }

    $override
    void deselectObject(GameObject* object) {
        EditorUI::deselectObject(object);
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendSelection(this->getSelectedObjects());
        }
    }

    // --- Edits that only reconcile used to catch late (alk hook coverage) ---

    $override
    void onPasteColor(CCObject* sender) {
        EditorUI::onPasteColor(sender);
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.reconcileObjects(this->getSelectedObjects());
        }
    }

    $override
    void assignNewGroups(bool groupY) {
        EditorUI::assignNewGroups(groupY);
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.reconcileObjects(this->getSelectedObjects());
        }
    }

    $override
    void onGroupSticky(CCObject* sender) {
        EditorUI::onGroupSticky(sender);
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.reconcileObjects(this->getSelectedObjects());
        }
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
