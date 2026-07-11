#pragma once

#include "EditorHistoryTypes.hpp"

#include <Geode/Geode.hpp>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class GameObject;
class LevelEditorLayer;
class UndoObject;
class EditorUI;

namespace paimon::editorhistory {

class EditorHistoryTracker {
public:
    static EditorHistoryTracker& get();

    void clear();
    void setEditor(LevelEditorLayer* editor);
    LevelEditorLayer* editor() const { return m_editor; }

    void onUndoAdded(UndoObject* undo, bool keepRedo = true);
    void pollStacks();

    // Call before a batch edit (color popup open / paste color / scale…) to
    // snapshot selection, then noteObjectsChanged() after the edit applies.
    void snapshotSelection(char const* reason = nullptr);
    void noteObjectsChanged(ActionKind hint = ActionKind::Unknown);
    // Force re-diff of current selection immediately (after hooks).
    void flushSelectionDiff(ActionKind hint = ActionKind::Unknown);

    void onCollabOp(
        int clientId,
        std::string const& author,
        std::string const& kind,
        int objectCount,
        int objectId,
        cocos2d::CCPoint worldPos,
        std::string savePreview,
        bool isLocal
    );

    UndoMeta const* metaFor(UndoObject* undo) const;
    void prune();

    std::vector<CollabFeedEntry> collabFeed(size_t maxCount = 500) const;
    std::vector<LocalEditEntry> localEdits(size_t maxCount = 300) const;
    LocalEditEntry const* localEditById(uint64_t id) const;
    LocalEditEntry* mutableLocalEditById(uint64_t id);

    uint64_t collabRevision() const { return m_collabRevision; }
    uint64_t undoRevision() const { return m_undoRevision; }
    uint64_t localRevision() const { return m_localRevision; }

    // Try to restore a synthetic edit (re-applies beforeSave). Returns ok.
    bool restoreLocalEdit(EditorUI* ui, uint64_t localId);
    bool restoreLocalEdit(EditorUI* ui, LocalEditEntry const& entry);

    struct Stats {
        int undoCount = 0;
        int redoCount = 0;
        int collabCount = 0;
        int localCount = 0;
        int localActions = 0;
        int peerActions = 0;
    };
    Stats stats() const;

    static ActionKind classify(UndoObject* undo, LevelEditorLayer* lel);
    static std::string describe(UndoObject* undo, LevelEditorLayer* lel);

    static cocos2d::CCArray* collectObjects(UndoObject* undo);
    static int objectCount(UndoObject* undo);
    static int firstObjectId(UndoObject* undo);
    static std::string buildSaveString(UndoObject* undo, LevelEditorLayer* lel);

    static void focusAndSelect(EditorUI* ui, cocos2d::CCArray* objects);
    static void focusPoint(EditorUI* ui, cocos2d::CCPoint worldPos);
    static bool undoSpecific(EditorUI* ui, UndoObject* undo);
    static int undoToHere(EditorUI* ui, UndoObject* undo, bool includeSelf);

    bool hideSelectNoise() const { return m_hideSelectNoise; }
    void setHideSelectNoise(bool v) { m_hideSelectNoise = v; ++m_undoRevision; }

private:
    EditorHistoryTracker() = default;

    void pollPropertyEdits();
    void pushLocalEdit(LocalEditEntry e);
    void refineTopUndo(ActionKind kind);

    LevelEditorLayer* m_editor = nullptr;
    std::unordered_map<UndoObject*, UndoMeta> m_meta;
    std::deque<CollabFeedEntry> m_collabFeed;
    std::deque<LocalEditEntry> m_localEdits;

    // uniqueID -> last known save string
    std::unordered_map<int, std::string> m_propSnap;
    // uniqueID -> last known color-channel snapshot (RGB lives on channels)
    std::unordered_map<int, std::vector<ChannelSnap>> m_channelSnap;
    // Pre-edit snapshot taken by snapshotSelection()
    std::unordered_map<int, std::string> m_preEditSnap;
    std::unordered_map<int, std::vector<ChannelSnap>> m_preEditChannels;
    bool m_hasPreEdit = false;
    ActionKind m_preEditHint = ActionKind::Unknown;

    static std::vector<ChannelSnap> captureChannels(LevelEditorLayer* lel, GameObject* go);
    static std::string channelSignature(std::vector<ChannelSnap> const& ch);
    static void applyChannels(LevelEditorLayer* lel, std::vector<ChannelSnap> const& ch);
    static void applyChannels(LevelEditorLayer* lel, GameObject* go, std::vector<ChannelSnap> const& ch);

    int m_lastUndoCount = -1;
    int m_lastRedoCount = -1;
    bool m_hideSelectNoise = true; // hide de-select spam by default

    uint64_t m_nextSeq = 1;
    uint64_t m_nextCollabId = 1;
    uint64_t m_nextLocalId = 1;
    uint64_t m_undoRevision = 0;
    uint64_t m_collabRevision = 0;
    uint64_t m_localRevision = 0;

    static constexpr size_t kMaxCollabFeed = 800;
    static constexpr size_t kMaxLocalEdits = 400;
    static constexpr size_t kMaxSavePreview = 6000;
};

// historyEnabled() is declared in ObjectTimelineStore.hpp

} // namespace paimon::editorhistory
