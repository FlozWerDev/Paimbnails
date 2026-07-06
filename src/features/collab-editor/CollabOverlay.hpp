#pragma once

#include "CollabTypes.hpp"

#include <Geode/Geode.hpp>
#include <unordered_map>
#include <vector>

class LevelEditorLayer;

namespace paimon::collab {

// In-editor overlay for a collab session:
//  - Place-style attribution: a colored name tag per peer that jumps to the
//    object they just placed/edited and fades out, plus a brief flash on the
//    object itself (tags live in the object layer, so they follow the camera).
//  - Toast notifications that slide down from the top for chat/system notices
//    (max 2 at once, each animating independently) + "who's talking" label.
// Registers itself with CollabManager on init and unregisters on destruction.
class CollabEditorOverlay : public cocos2d::CCNode {
public:
    static CollabEditorOverlay* create(LevelEditorLayer* editor);

    // A remote edit by `clientId` was applied at `worldPos` (object layer space).
    void onRemoteEdit(int clientId, std::string const& name, cocos2d::CCPoint worldPos, bool isDelete);
    // A chat message (or local system notice) arrived.
    void onChat(ChatMessage const& msg);

private:
    bool init(LevelEditorLayer* editor);
    ~CollabEditorOverlay() override;

    void buildButtons();
    void onFlashDone(cocos2d::CCObject*);
    void refresh(float dt);

    // Toast notifications (top of the screen).
    cocos2d::CCNode* buildToast(ChatMessage const& msg);
    void showToast(ChatMessage const& msg);
    void layoutToasts();
    void dismissToast(cocos2d::CCNode* toast);
    void onToastExpired(cocos2d::CCObject* sender);

    LevelEditorLayer* m_editor = nullptr;

    // Per-peer attribution tag, child of the editor's object layer.
    std::unordered_map<int, geode::Ref<cocos2d::CCLabelBMFont>> m_tags;

    // Screen-space container for toasts, plus the currently live ones
    // (front = oldest, back = newest).
    cocos2d::CCNode* m_toastLayer = nullptr;
    std::vector<geode::Ref<cocos2d::CCNode>> m_toasts;

    cocos2d::CCLabelBMFont* m_speakingLabel = nullptr;
    int m_flashCount = 0;
};

} // namespace paimon::collab
