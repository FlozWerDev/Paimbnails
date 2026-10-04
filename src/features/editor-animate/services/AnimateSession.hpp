#pragma once

#include "../AnimateTypes.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EditorUI;
class GameObject;
class LevelEditorLayer;

namespace paimon::animate {

class AnimateSession {
public:
    static AnimateSession& get();

    void attach(LevelEditorLayer* editor);
    void detach(LevelEditorLayer* editor);
    LevelEditorLayer* editor() const;
    EditorUI* ui() const;

    Document& doc() { return m_doc; }
    ViewPrefs& prefs() { return m_prefs; }
    void prefsChanged();
    void persist();

    // ui polls this instead of listening; bumps on any document edit.
    int revision() const { return m_revision; }
    void changed(Clip* touched = nullptr);

    bool isOpen() const { return m_open; }
    void setOpen(bool open);
    bool isPlaytesting() const;

    Clip* activeClip();
    Clip* clipAt(int index);
    Clip* findClip(int id);
    int indexOf(int id) const;
    int activeIndex() const { return m_doc.activeClip; }
    void selectClip(int index);

    geode::Result<int> createClip(std::string name);
    geode::Result<int> createClipFromSelection(std::string name);
    geode::Result<int> importGroups(std::string name, std::vector<int> const& groups);
    geode::Result<int> duplicateClip(int index);
    geode::Result<> deleteClip(int index, bool deleteObjects);
    void renameClip(int index, std::string name);
    void cycleColor(int index);
    void toggleHidden(int index);
    void toggleLocked(int index);
    void moveClip(int index, int direction);

    int currentFrame() const { return m_doc.currentFrame; }
    Frame* frameAt(int index);
    void setFrame(int index);
    void stepFrame(int direction);
    void firstFrame();
    void lastFrame();

    geode::Result<> insertFrame(bool after);
    geode::Result<> duplicateFrame();
    geode::Result<> deleteFrame(bool deleteObjects);
    geode::Result<std::size_t> clearFrame();
    void moveFrame(int direction);
    void reverseFrames();
    void setHold(int index, int hold);
    void setLabel(int index, std::string label);
    void toggleSkip(int index);
    geode::Result<> linkGroup(int index, int group);
    geode::Result<std::size_t> assignSelection();
    geode::Result<std::size_t> selectFrameObjects();
    std::size_t objectCount(int group) const;
    std::size_t selectionCount() const;

    bool playing() const { return m_playing; }
    void togglePlay();
    void stopPlayback();
    float playTime() const { return m_time; }
    // where the playhead sits; differs from the cursor while playing unfollowed.
    int displayedFrame();
    void tick(float dt);

    void applyOverrides();
    // hands every object its opacity back, e.g. right before a playtest starts.
    void releaseOverrides();
    void invalidate() { m_dirty = true; }
    bool needsApply() const { return m_dirty; }
    bool isLocked(GameObject* object);
    void suspendLock(bool suspended) { m_suspendLock = suspended; }
    void onObjectCreated(GameObject* object);
    void onObjectsPasted(cocos2d::CCArray* objects);

    std::unordered_set<int> reservedGroups() const;
    int allocateGroup(std::unordered_set<int>& taken) const;

private:
    AnimateSession() = default;

    bool canEdit(std::string& error) const;
    void clampCursor();
    void touch(Clip& clip);
    int ensureGroup(Clip& clip, std::size_t frame);
    std::size_t assignObjects(Clip& clip, std::size_t frame, std::vector<GameObject*> const& objects);
    std::size_t removeObjects(std::vector<GameObject*> const& objects);
    geode::Result<int> cloneGroup(int source, std::vector<int> const& strip, int target);
    int shownFrame(int clipIndex);
    void rebuildSequences();
    void recompute();
    void restore(std::vector<int> const& groups);
    void deselectLocked();

    // raw on purpose: the weakref pool would keep the whole level alive after exit.
    LevelEditorLayer* m_editor = nullptr;
    std::string m_key;
    Document m_doc;
    ViewPrefs m_prefs;
    bool m_open = false;
    bool m_playing = false;
    float m_time = 0.f;
    int m_revision = 1;
    int m_sequenceRevision = 0;
    bool m_dirty = true;
    bool m_suspendLock = false;

    std::vector<std::vector<Step>> m_sequences;
    std::unordered_map<int, GLubyte> m_overrides;
    std::unordered_set<int> m_locked;
    std::vector<int> m_touched;
};

} // namespace paimon::animate
