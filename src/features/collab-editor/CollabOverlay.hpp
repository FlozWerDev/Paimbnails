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
//  - Toast notifications for chat/system notices that slide in from the right
//    edge (stacked, each animating independently) so they never cover the
//    center of the canvas.
//  - Voice chips at the top: one per active speaker (including yourself) with
//    a colored avatar, the name and a live audio-level bar; the row re-centers
//    itself as speakers come and go.
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

    void onFlashDone(cocos2d::CCObject*);
    void refresh(float dt);

    // Toast notifications (right edge of the screen).
    cocos2d::CCNode* buildToast(ChatMessage const& msg);
    void showToast(ChatMessage const& msg);
    void layoutToasts();
    void dismissToast(cocos2d::CCNode* toast);
    void onToastExpired(cocos2d::CCObject* sender);

    // Voice chips ("who's talking" with live level bars).
    struct VoiceChip {
        geode::Ref<cocos2d::CCNode> root;
        cocos2d::CCLayerColor* barFill = nullptr;
        float shown = 0.f;   // displayed level (smoothed)
        float target = 0.f;  // latest reported level
        float silent = 0.f;  // seconds since the speaker was last active
        float width = 0.f;
        bool placed = false; // got its first layout position
    };
    void buildVoiceChip(int clientId, std::string const& name, VoiceChip& chip);
    void updateVoice(float dt);
    void layoutVoiceChips();

    LevelEditorLayer* m_editor = nullptr;

    // Per-peer attribution tag, child of the editor's object layer.
    std::unordered_map<int, geode::Ref<cocos2d::CCLabelBMFont>> m_tags;

    // Screen-space container for toasts, plus the currently live ones
    // (front = oldest, back = newest).
    cocos2d::CCNode* m_toastLayer = nullptr;
    std::vector<geode::Ref<cocos2d::CCNode>> m_toasts;

    // clientId (0 = local) -> live voice chip.
    cocos2d::CCNode* m_voiceLayer = nullptr;
    std::unordered_map<int, VoiceChip> m_voiceChips;

    int m_flashCount = 0;
};

} // namespace paimon::collab
