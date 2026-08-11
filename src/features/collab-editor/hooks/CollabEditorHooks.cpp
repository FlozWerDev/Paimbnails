#include "../CollabManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../CollabOverlay.hpp"
#include "../CollabPopups.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorModule.hpp"

#include <Geode/binding/UndoObject.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/modify/ColorSelectPopup.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/LevelSettingsLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include "../../../utils/ExtendedKeybind.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../editor-suite/EditorHelpers.hpp"

using namespace geode::prelude;

namespace {

bool collabEnabled() {
    return paimon::editor::featureEnabled("collab-enabled");
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

// Re-sync undo/redo objects; transform copies are handled by selection reconcile.
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

// Build the collab button from two players; SimplePlayer needs a measurable wrapper.
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
    addPlayer(13.f, 0.60f, {0, 210, 255}, {0, 90, 210}, 0);
    addPlayer(26.f, 0.66f, {255, 150, 40}, {255, 225, 90}, 1);

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

}

class $modify(PaimonCollabEditLevelLayer, EditLevelLayer) {
    $override
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        if (!collabEnabled()) return true;

        auto* folderMenu = typeinfo_cast<CCMenu*>(this->getChildByID("folder-menu"));
        if (!folderMenu) return true;

        // Prefer the custom asset; otherwise compose the duo-cube icon.
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
        // Keep the pointer captured at init; LevelEditorLayer::get() is already null during teardown.
        LevelEditorLayer* m_self = nullptr;
        ~Fields() {
            if (paimon::isRuntimeShuttingDown()) return;
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
        auto& mgr = paimon::collab::CollabManager::get();
        mgr.tick();

        // Middle-click ping in object-layer space.
#if defined(GEODE_IS_WINDOWS)
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            static bool s_wasMiddle = false;
            bool middle = paimon::keybinds::isMouseButtonHeld(paimon::keybinds::MouseButton::Middle);
            if (middle && !s_wasMiddle) {
                auto* director = CCDirector::get();
                auto* glView = director ? director->getOpenGLView() : nullptr;
                auto* layer = m_objectLayer;
                if (glView && layer) {
                    auto mouse = glView->getMousePosition();
                    auto win = director->getWinSize();
                    // GLFW y is top-down; Cocos is bottom-up.
                    CCPoint glPos{mouse.x, win.height - mouse.y};
                    auto world = layer->convertToNodeSpace(glPos);
                    mgr.sendPing(world.x, world.y);
                }
            }
            s_wasMiddle = middle;
        }
#endif
    }

    $override
    void removeObject(GameObject* object, bool noUndo) {
        if (object && !paimon::collab::CollabManager::get().isApplyingRemote()) {
            paimon::collab::CollabManager::get().sendDeletedObject(object);
        }
        LevelEditorLayer::removeObject(object, noUndo);
    }

    $override
    void levelSettingsUpdated() {
        LevelEditorLayer::levelSettingsUpdated();
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendLevelSettings(false);
        }
    }
};

// Color popups may skip levelSettingsUpdated; push full metadata on close.
class $modify(PaimonCollabColorSelectPopup, ColorSelectPopup) {
    bool init(EffectGameObject* object, CCArray* objects, ColorAction* action) {
        if (!ColorSelectPopup::init(object, objects, action)) return false;
        return true;
    }

    $override
    void keyBackClicked() {
        ColorSelectPopup::keyBackClicked();
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendLevelSettings(false);
        }
    }

    $override
    void onClose(CCObject* sender) {
        ColorSelectPopup::onClose(sender);
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendLevelSettings(false);
        }
    }
};

class $modify(PaimonCollabEditorUI, EditorUI) {
    $override
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        if (collabEnabled() && paimon::collab::CollabManager::get().connected()) {
            auto* kb = CCDirector::sharedDirector()->getKeyboardDispatcher();
            bool mod = kb && (kb->getControlKeyPressed() || kb->getCommandKeyPressed() ||
                              kb->getShiftKeyPressed() || kb->getAltKeyPressed());

            // F cycles peer cameras when no modifier or text field is active.
            if (key == cocos2d::KEY_F && !mod && !paimon::editor::focusedTextInput()) {
                auto name = paimon::collab::CollabManager::get().cycleFollowPeer();
                if (name.empty()) {
                    Notification::create("Follow off", NotificationIcon::Info)->show();
                } else {
                    Notification::create(fmt::format("Siguiendo a {}", name), NotificationIcon::Info)->show();
                }
                return;
            }
            if (key == cocos2d::KEY_Escape &&
                paimon::collab::CollabManager::get().followClientId() > 0) {
                paimon::collab::CollabManager::get().clearFollow();
                Notification::create("Follow off", NotificationIcon::Info)->show();
                // Let pause and other Escape handlers continue.
            }

            if (key == cocos2d::KEY_E && !paimon::editor::focusedTextInput() &&
                kb && (kb->getControlKeyPressed() || kb->getCommandKeyPressed())) {
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
        auto& mgr = paimon::collab::CollabManager::get();
        if (object && !mgr.canEditObjectLayer(object)) {
            // Keep the local placement, but do not sync across layers.
            Notification::create("No es tu layer", NotificationIcon::Warning)->show();
        } else {
            mgr.sendCreatedObject(object);
        }
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
        // Position-only updates use the cheap remote path.
        paimon::collab::CollabManager::get().sendMovedObject(object);
    }

    $override
    void transformObject(GameObject* object, EditCommand command, bool noOffset) {
        EditorUI::transformObject(object, command, noOffset);
        // Mixed transforms use a full update.
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
        // Scale updates set peer axes without recreating the object.
        paimon::collab::CollabManager::get().sendScaledObjects(objects);
    }

    $override
    void rotateObjects(CCArray* objects, float rotation, CCPoint pivotPoint) {
        EditorUI::rotateObjects(objects, rotation, pivotPoint);
        paimon::collab::CollabManager::get().sendRotatedObjects(objects);
    }

    $override
    void flipObjectsX(CCArray* objects) {
        EditorUI::flipObjectsX(objects);
        paimon::collab::CollabManager::get().sendFlippedObjects(objects);
    }

    $override
    void flipObjectsY(CCArray* objects) {
        EditorUI::flipObjectsY(objects);
        paimon::collab::CollabManager::get().sendFlippedObjects(objects);
    }

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

// Mark the host's active room in the level list.
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
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorPauseLayer::init");
    }

    $override
    bool init(LevelEditorLayer* editor) {
        if (!EditorPauseLayer::init(editor)) return false;
        if (!collabEnabled()) return true;

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("info-menu"));
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("settings-menu"));
        if (!menu) return true;

        auto* sprite = ButtonSprite::create("Sala", "goldFont.fnt", "GJ_button_05.png", 0.45f);
        if (!sprite) return true;
        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(PaimonCollabEditorPauseLayer::onCollabRoom)
        );
        if (!button) return true;
        button->setID("collab-room-button"_spr);
        button->setScale(0.65f);
        menu->addChild(button);
        menu->updateLayout();
        return true;
    }

    void onCollabRoom(CCObject*) {
        auto* scene = CCDirector::get()->getRunningScene();
        if (scene && scene->getChildByIDRecursive("collab-room"_spr)) return;
        if (auto* popup = paimon::collab::CollabRoomPopup::create(
                m_editorLayer ? m_editorLayer->m_level : nullptr)) {
            popup->show();
        }
    }

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
        showBlocked("mode / ajustes del nivel");
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

    $override
    void onClose(CCObject* sender) {
        LevelSettingsLayer::onClose(sender);
        // Song, art, and mode changes apply on close.
        auto& mgr = paimon::collab::CollabManager::get();
        if (mgr.connected() && !mgr.isApplyingRemote()) {
            mgr.sendLevelSettings(false);
        }
    }
};
