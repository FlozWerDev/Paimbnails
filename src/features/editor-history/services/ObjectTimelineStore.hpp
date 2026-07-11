#pragma once

#include "EditorHistoryTypes.hpp"

#include <Geode/Geode.hpp>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class GameObject;
class LevelEditorLayer;
class EditorUI;

namespace paimon::editorhistory {

// Only the three undo categories we expose in the editor.
enum class ObjChangeKind {
    Color = 0,
    Groups,
    EditorLayer,
};

inline char const* objChangeName(ObjChangeKind k) {
    switch (k) {
        case ObjChangeKind::Color: return "Color";
        case ObjChangeKind::Groups: return "Groups";
        case ObjChangeKind::EditorLayer: return "Layers";
    }
    return "Edit";
}

inline cocos2d::ccColor3B objChangeColor(ObjChangeKind k) {
    switch (k) {
        case ObjChangeKind::Color: return {220, 130, 255};
        case ObjChangeKind::Groups: return {80, 220, 190};
        case ObjChangeKind::EditorLayer: return {255, 170, 70};
    }
    return {170, 170, 170};
}

inline bool isTrackedKind(ObjChangeKind /*k*/) { return true; }

struct ObjectChangeNode {
    uint64_t id = 0;
    int64_t timeMs = 0;
    int uniqueId = 0;
    int objectTypeId = 0;
    ObjChangeKind kind = ObjChangeKind::Color;
    std::string label;
    std::string beforeSave;
    std::string afterSave;
    cocos2d::CCPoint worldPos{0.f, 0.f};
    cocos2d::ccColor3B previewBefore{255, 255, 255};
    cocos2d::ccColor3B previewAfter{255, 255, 255};
    bool hasColorPreview = false;
    bool canRevertBefore = false;
    bool canRevertAfter = false;
    std::vector<ChannelSnap> beforeChannels;
    std::vector<ChannelSnap> afterChannels;
};

class ObjectTimelineStore {
public:
    static ObjectTimelineStore& get();

    void clear();
    void setEditor(LevelEditorLayer* editor);
    LevelEditorLayer* editor() const { return m_editor; }

    void tick();
    void onObjectTouched(GameObject* go); // optional immediate poll

    ObjectChangeNode const* nodeById(uint64_t id) const;
    int countOfKind(ObjChangeKind kind) const;
    uint64_t revision() const { return m_revision; }
    int eventCount() const { return static_cast<int>(m_nodes.size()); }
    bool isMutating() const { return m_mutating; }

    // Newest-first snapshot for the history browser UI.
    std::vector<ObjectChangeNode> recentNodes(int limit = 50) const;

    // Undo the most recent change of the given kind. Removes it from the stack.
    bool undoLastOfKind(EditorUI* ui, ObjChangeKind kind);
    bool revertObjectToNode(EditorUI* ui, uint64_t nodeId, bool toBefore);
    // Undo this node (to before) and remove it from the stack.
    bool undoNode(EditorUI* ui, uint64_t nodeId);

private:
    ObjectTimelineStore() = default;

    void pollObject(GameObject* go);
    void pushOrCoalesce(ObjectChangeNode node);
    std::string saveOf(GameObject* go) const;
    std::vector<ChannelSnap> snapColors(GameObject* go) const;
    bool applyState(
        EditorUI* ui,
        int uniqueId,
        std::string save,
        std::vector<ChannelSnap> const& colors,
        cocos2d::CCPoint keepPos
    );

    LevelEditorLayer* m_editor = nullptr;
    std::unordered_map<int, std::string> m_lastSave;
    std::unordered_map<int, std::vector<ChannelSnap>> m_lastColors;
    std::deque<ObjectChangeNode> m_nodes;

    uint64_t m_nextNodeId = 1;
    uint64_t m_revision = 0;
    bool m_mutating = false;

    static constexpr size_t kMaxNodes = 200;
    static constexpr int64_t kCoalesceMs = 1200;
};

// Returns true and writes kind if the save-string diff is one of the 3 tracked types.
bool classifyTrackedChange(
    std::unordered_map<int, std::string> const& before,
    std::unordered_map<int, std::string> const& after,
    ObjChangeKind& outKind
);
std::unordered_map<int, std::string> parseObjectSaveKeys(std::string const& save);

bool historyEnabled();

} // namespace paimon::editorhistory
