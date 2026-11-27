#pragma once

#include "../ReferenceTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace paimon::editorref {

struct StashedAnchor {
    unsigned int index = 0;
    geode::Ref<GameObject> object;
};

class ReferenceSession {
public:
    static ReferenceSession& get();

    void attach(LevelEditorLayer* editor);
    void detach(LevelEditorLayer* editor, bool allowPersist = true);

    bool isAttached(LevelEditorLayer* editor) const;
    LevelEditorLayer* editor() const;

    // Live entries currently active (excluding items deleted via GD undoable delete).
    std::vector<ReferenceEntry> activeEntries() const;
    ReferenceEntry const* findEntry(int id) const;
    int entryIdForAnchor(GameObject* object) const;
    bool isReferenceAnchor(GameObject* object) const;
    bool isLockedAnchor(LevelEditorLayer* editor, GameObject* object) const;

    geode::Result<int> addFromFile(std::filesystem::path const& path);
    geode::Result<int> addFromClipboard();
    geode::Result<int> addFromHistory(std::string const& historyId);
    geode::Result<int> duplicateEntry(int id);
    bool removeEntry(int id);
    // Safely removes any selected reference anchors before EditorUI::onDeleteSelected runs.
    // Returns true if no normal GD objects remain selected.
    bool deleteSelectedAnchors(EditorUI* ui);

    void setOpacity(int id, int opacity);
    void setLocked(int id, bool locked);
    void setVisible(int id, bool visible);
    void setZPlacement(int id, ZPlacement z);
    void setShowInPlaytest(int id, bool show);
    void flipEntry(int id, bool axisY);
    void resetTransform(int id);
    bool selectAndFocus(int id, bool unlockIfLocked = true);
    void focusEntry(int id);

    // Returns reference id if the editor currently has a single reference anchor selected, else 0.
    int singleSelectedReferenceId(EditorUI* ui) const;
    // Hit-tests unlocked visible reference sprites at an EditorUI screen-space point.
    GameObject* pickUnlockedAnchorAtScreenPos(
        LevelEditorLayer* editor,
        cocos2d::CCPoint screenPos
    ) const;

    void syncAll(LevelEditorLayer* editor);
    void onObjectRemoved(LevelEditorLayer* editor, GameObject* object);
    void reconcileUndoRedo(LevelEditorLayer* editor);

    std::vector<StashedAnchor> stripAnchorsForSerialization(LevelEditorLayer* editor);
    void restoreAnchorsAfterSerialization(
        LevelEditorLayer* editor,
        std::vector<StashedAnchor>& stashed
    );

    void persist(LevelEditorLayer* editor);

private:
    struct RuntimeItem {
        ReferenceEntry entry;
        geode::WeakRef<GameObject> anchor;
        geode::WeakRef<cocos2d::CCSprite> sprite;
        geode::WeakRef<cocos2d::CCDrawNode> border;
        int texWidth = 1;
        int texHeight = 1;
        bool deletedInEditor = false;
    };

    ReferenceSession() = default;

    RuntimeItem* findItem(int id);
    RuntimeItem const* findItem(int id) const;

    bool spawnRuntimeItem(LevelEditorLayer* editor, RuntimeItem& item);
    geode::Result<int> addImported(
        std::string const& assetFile,
        std::string const& name,
        int texW,
        int texH
    );
    void applyAnchorSilentStyle(GameObject* anchor);
    void syncItem(LevelEditorLayer* editor, RuntimeItem& item, bool playtesting);
    void deselectAnchor(GameObject* anchor);

    // Non-owning pointer validated against LevelEditorLayer::get() to avoid
    // Geode WeakRefPool retaining old LevelEditorLayer instances across editor re-entry.
    LevelEditorLayer* m_editor = nullptr;
    int m_nextId = 1;
    std::vector<RuntimeItem> m_items;
    bool m_internalRemoval = false;
};

} // namespace paimon::editorref
