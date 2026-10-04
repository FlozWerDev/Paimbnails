#include "AnimateSession.hpp"

#include "AnimateCompiler.hpp"
#include "AnimatePlanner.hpp"
#include "AnimateStore.hpp"
#include "../AnimateText.hpp"
#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::animate {

namespace {

constexpr char const* kOpenKey = "editor-animate-open";

bool hasGroup(GameObject* object, int group) {
    if (!object || group <= 0) return false;
    for (int i = 0; i < object->m_groupCount; ++i) {
        if (object->getGroupID(i) == group) return true;
    }
    return false;
}

std::vector<GameObject*> objectsIn(LevelEditorLayer* editor, int group) {
    std::vector<GameObject*> objects;
    if (!editor || group <= 0) return objects;
    auto* array = editor->getGroup(group);
    if (!array) return objects;
    objects.reserve(array->count());
    for (auto* object : CCArrayExt<GameObject*>(array)) {
        if (object) objects.push_back(object);
    }
    return objects;
}

std::vector<GameObject*> selectionOf(EditorUI* ui) {
    std::vector<GameObject*> objects;
    if (!ui) return objects;
    if (auto* selected = ui->getSelectedObjects()) {
        for (auto* object : CCArrayExt<GameObject*>(selected)) {
            if (object) objects.push_back(object);
        }
    }
    if (objects.empty() && ui->m_selectedObject) objects.push_back(ui->m_selectedObject);
    std::sort(objects.begin(), objects.end());
    objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
    return objects;
}

CCArray* toArray(std::vector<GameObject*> const& objects) {
    auto* array = CCArray::create();
    for (auto* object : objects) array->addObject(object);
    return array;
}

std::string defaultName(Document const& doc) {
    return fmt::format("{} {}", tr("Clip", "Animacion"), doc.nextId);
}

void syncUpdated(std::vector<GameObject*> const& objects) {
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !objects.empty()) collab.sendUpdatedObjects(toArray(objects));
}

} // namespace

AnimateSession& AnimateSession::get() {
    static AnimateSession session;
    return session;
}

void AnimateSession::attach(LevelEditorLayer* editor) {
    if (!editor || m_editor == editor) return;
    m_editor = editor;
    m_key = levelKey(editor->m_level);
    m_doc = loadDocument(m_key);
    m_prefs = loadPrefs();
    m_open = Mod::get()->getSavedValue<bool>(kOpenKey, false);
    m_playing = false;
    m_time = 0.f;
    m_overrides.clear();
    m_locked.clear();
    m_touched.clear();
    clampCursor();
    changed();
    log::info("[Animate] Editor enlazado ({} clips, clave {})", m_doc.clips.size(), m_key);
}

void AnimateSession::detach(LevelEditorLayer* editor) {
    if (m_editor != editor) return;
    m_editor = nullptr;
    m_doc = {};
    m_key.clear();
    m_playing = false;
    m_overrides.clear();
    m_locked.clear();
    m_touched.clear();
    ++m_revision;
}

LevelEditorLayer* AnimateSession::editor() const {
    return m_editor && LevelEditorLayer::get() == m_editor ? m_editor : nullptr;
}

EditorUI* AnimateSession::ui() const {
    auto* editor = this->editor();
    return editor ? editor->m_editorUI : nullptr;
}

void AnimateSession::prefsChanged() {
    sanitize(m_prefs);
    savePrefs(m_prefs);
    m_dirty = true;
    ++m_revision;
}

void AnimateSession::persist() {
    if (m_key.empty()) return;
    saveDocument(m_key, m_doc);
}

void AnimateSession::changed(Clip* touched) {
    if (touched) touch(*touched);
    clampCursor();
    m_dirty = true;
    ++m_revision;
}

void AnimateSession::setOpen(bool open) {
    if (m_open == open) return;
    m_open = open;
    Mod::get()->setSavedValue<bool>(kOpenKey, open);
    if (!open) m_playing = false;
    m_dirty = true;
    ++m_revision;
    if (open) deselectLocked();
}

bool AnimateSession::isPlaytesting() const {
    auto* editor = this->editor();
    return editor && editor->m_playbackMode != PlaybackMode::Not;
}

Clip* AnimateSession::clipAt(int index) {
    if (index < 0 || index >= static_cast<int>(m_doc.clips.size())) return nullptr;
    return &m_doc.clips[static_cast<std::size_t>(index)];
}

Clip* AnimateSession::findClip(int id) {
    for (auto& clip : m_doc.clips) {
        if (clip.id == id) return &clip;
    }
    return nullptr;
}

int AnimateSession::indexOf(int id) const {
    for (std::size_t i = 0; i < m_doc.clips.size(); ++i) {
        if (m_doc.clips[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

Clip* AnimateSession::activeClip() {
    return clipAt(m_doc.activeClip);
}

void AnimateSession::clampCursor() {
    int const clips = static_cast<int>(m_doc.clips.size());
    m_doc.activeClip = clips == 0 ? 0 : std::clamp(m_doc.activeClip, 0, clips - 1);
    auto* clip = activeClip();
    int const frames = clip ? static_cast<int>(clip->frames.size()) : 0;
    m_doc.currentFrame = frames == 0 ? 0 : std::clamp(m_doc.currentFrame, 0, frames - 1);
}

void AnimateSession::touch(Clip& clip) {
    if (clip.compiled.present()) clip.compiled.stale = true;
}

bool AnimateSession::canEdit(std::string& error) const {
    if (!editor()) {
        error = tr("The editor is no longer available.", "El editor ya no esta disponible.");
        return false;
    }
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        error = tr("The collab room is read only.", "La sala collab esta en solo lectura.");
        return false;
    }
    return true;
}

void AnimateSession::selectClip(int index) {
    if (!clipAt(index) || index == m_doc.activeClip) return;
    m_doc.activeClip = index;
    m_doc.currentFrame = 0;
    changed();
    deselectLocked();
}

Result<int> AnimateSession::createClip(std::string name) {
    std::string error;
    if (!canEdit(error)) return Err(error);
    if (m_doc.clips.size() >= static_cast<std::size_t>(kMaxClips)) {
        return Err(fmt::format("{} {}", tr("Limit reached:", "Limite alcanzado:"), kMaxClips));
    }
    Clip clip;
    clip.id = m_doc.nextId++;
    clip.name = name.empty() ? defaultName(m_doc) : std::move(name);
    clip.color = (clip.id - 1) % kColorTags;
    clip.frames.push_back({});
    sanitize(clip);
    m_doc.clips.push_back(std::move(clip));
    m_doc.activeClip = static_cast<int>(m_doc.clips.size()) - 1;
    m_doc.currentFrame = 0;
    changed();
    return Ok(m_doc.activeClip);
}

Result<int> AnimateSession::createClipFromSelection(std::string name) {
    auto selected = selectionOf(ui());
    if (selected.empty()) {
        return Err(tr("Select the objects of the first frame first.",
            "Primero selecciona los objetos del primer frame."));
    }
    auto created = createClip(std::move(name));
    if (!created) return created;
    auto& clip = m_doc.clips[static_cast<std::size_t>(created.unwrap())];
    assignObjects(clip, 0, selected);
    changed();
    return created;
}

Result<int> AnimateSession::importGroups(std::string name, std::vector<int> const& groups) {
    std::vector<int> valid;
    for (int group : groups) {
        if (group <= 0 || group > kMaxGroup) continue;
        if (std::find(valid.begin(), valid.end(), group) != valid.end()) continue;
        valid.push_back(group);
        if (valid.size() >= static_cast<std::size_t>(kMaxFrames)) break;
    }
    if (valid.empty()) return Err(tr("No valid group ids.", "No hay ids de grupo validos."));
    auto created = createClip(std::move(name));
    if (!created) return created;
    auto& clip = m_doc.clips[static_cast<std::size_t>(created.unwrap())];
    clip.frames.clear();
    for (int group : valid) clip.frames.push_back({group, 1, "", false});
    changed();
    return created;
}

Result<int> AnimateSession::cloneGroup(int source, std::vector<int> const& strip, int target) {
    auto* editor = this->editor();
    auto objects = objectsIn(editor, source);
    if (objects.empty()) return Ok(0);
    std::string payload;
    for (auto* object : objects) {
        payload += rewriteGroups(std::string(object->getSaveString(editor)), strip, target);
        payload += ';';
    }
    auto* created = editor->createObjectsFromString(payload, true, true);
    if (!created || created->count() != objects.size()) {
        if (created) {
            for (auto* object : CCArrayExt<GameObject*>(created)) editor->removeObject(object, true);
        }
        return Err(tr("GD could not copy the frame objects.",
            "GD no pudo copiar los objetos del frame."));
    }
    editor->updateObjectColors(created);
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected()) collab.sendCreatedObjects(created);
    return Ok(static_cast<int>(created->count()));
}

Result<int> AnimateSession::duplicateClip(int index) {
    std::string error;
    if (!canEdit(error)) return Err(error);
    auto* source = clipAt(index);
    if (!source) return Err(tr("That clip no longer exists.", "Ese clip ya no existe."));
    if (m_doc.clips.size() >= static_cast<std::size_t>(kMaxClips)) {
        return Err(fmt::format("{} {}", tr("Limit reached:", "Limite alcanzado:"), kMaxClips));
    }

    Clip copy = *source;
    copy.id = m_doc.nextId++;
    copy.name = fmt::format("{} {}", source->name, tr("copy", "copia")).substr(0, 32);
    copy.compiled = {};
    auto const strip = frameGroups(*source);
    auto taken = reservedGroups();
    std::unordered_map<int, int> remap;
    for (int group : strip) {
        int const fresh = allocateGroup(taken);
        if (fresh <= 0) return Err(tr("No free groups left.", "No quedan grupos libres."));
        auto cloned = cloneGroup(group, strip, fresh);
        if (!cloned) return Err(cloned.unwrapErr());
        remap[group] = cloned.unwrap() > 0 ? fresh : 0;
    }
    for (auto& frame : copy.frames) {
        if (frame.group > 0) frame.group = remap[frame.group];
    }
    m_doc.clips.insert(m_doc.clips.begin() + index + 1, std::move(copy));
    m_doc.activeClip = index + 1;
    m_doc.currentFrame = 0;
    if (auto* editor = this->editor()) editor->dirtifyTriggers();
    changed();
    return Ok(index + 1);
}

Result<> AnimateSession::deleteClip(int index, bool deleteObjects) {
    std::string error;
    if (!canEdit(error)) return Err(error);
    auto* clip = clipAt(index);
    if (!clip) return Err(tr("That clip no longer exists.", "Ese clip ya no existe."));
    removeCompiled(editor(), *clip);
    if (deleteObjects) {
        for (int group : frameGroups(*clip)) removeObjects(objectsIn(editor(), group));
    }
    m_doc.clips.erase(m_doc.clips.begin() + index);
    if (m_doc.activeClip >= index) m_doc.activeClip = std::max(0, m_doc.activeClip - 1);
    m_doc.currentFrame = 0;
    changed();
    return Ok();
}

void AnimateSession::renameClip(int index, std::string name) {
    auto* clip = clipAt(index);
    if (!clip || name.empty()) return;
    clip->name = name.substr(0, 32);
    changed();
}

void AnimateSession::cycleColor(int index) {
    auto* clip = clipAt(index);
    if (!clip) return;
    clip->color = (clip->color + 1) % kColorTags;
    changed();
}

void AnimateSession::toggleHidden(int index) {
    auto* clip = clipAt(index);
    if (!clip) return;
    clip->hidden = !clip->hidden;
    changed();
    deselectLocked();
}

void AnimateSession::toggleLocked(int index) {
    auto* clip = clipAt(index);
    if (!clip) return;
    clip->locked = !clip->locked;
    changed();
    deselectLocked();
}

void AnimateSession::moveClip(int index, int direction) {
    int const target = index + direction;
    if (!clipAt(index) || !clipAt(target)) return;
    std::swap(m_doc.clips[static_cast<std::size_t>(index)], m_doc.clips[static_cast<std::size_t>(target)]);
    if (m_doc.activeClip == index) m_doc.activeClip = target;
    else if (m_doc.activeClip == target) m_doc.activeClip = index;
    changed();
}

Frame* AnimateSession::frameAt(int index) {
    auto* clip = activeClip();
    if (!clip || index < 0 || index >= static_cast<int>(clip->frames.size())) return nullptr;
    return &clip->frames[static_cast<std::size_t>(index)];
}

void AnimateSession::setFrame(int index) {
    auto* clip = activeClip();
    if (!clip || clip->frames.empty()) return;
    int const clamped = std::clamp(index, 0, static_cast<int>(clip->frames.size()) - 1);
    if (clamped == m_doc.currentFrame) return;
    m_doc.currentFrame = clamped;
    m_dirty = true;
    deselectLocked();
}

void AnimateSession::stepFrame(int direction) {
    auto* clip = activeClip();
    if (!clip || clip->frames.empty()) return;
    int const count = static_cast<int>(clip->frames.size());
    setFrame(((m_doc.currentFrame + direction) % count + count) % count);
}

void AnimateSession::firstFrame() {
    setFrame(0);
}

void AnimateSession::lastFrame() {
    if (auto* clip = activeClip()) setFrame(static_cast<int>(clip->frames.size()) - 1);
}

Result<> AnimateSession::insertFrame(bool after) {
    auto* clip = activeClip();
    if (!clip) return Err(tr("Create a clip first.", "Primero crea una animacion."));
    if (clip->frames.size() >= static_cast<std::size_t>(kMaxFrames)) {
        return Err(fmt::format("{} {}", tr("Frame limit:", "Limite de frames:"), kMaxFrames));
    }
    int const at = clip->frames.empty() ? 0 : m_doc.currentFrame + (after ? 1 : 0);
    Frame frame;
    if (auto* current = frameAt(m_doc.currentFrame)) frame.hold = current->hold;
    clip->frames.insert(clip->frames.begin() + at, frame);
    m_doc.currentFrame = at;
    changed(clip);
    deselectLocked();
    return Ok();
}

Result<> AnimateSession::duplicateFrame() {
    std::string error;
    if (!canEdit(error)) return Err(error);
    auto* clip = activeClip();
    auto* source = frameAt(m_doc.currentFrame);
    if (!clip || !source) return Err(tr("Create a clip first.", "Primero crea una animacion."));
    if (clip->frames.size() >= static_cast<std::size_t>(kMaxFrames)) {
        return Err(fmt::format("{} {}", tr("Frame limit:", "Limite de frames:"), kMaxFrames));
    }

    Frame copy = *source;
    copy.group = 0;
    if (source->group > 0 && objectCount(source->group) > 0) {
        auto taken = reservedGroups();
        int const fresh = allocateGroup(taken);
        if (fresh <= 0) return Err(tr("No free groups left.", "No quedan grupos libres."));
        auto cloned = cloneGroup(source->group, frameGroups(*clip), fresh);
        if (!cloned) return Err(cloned.unwrapErr());
        copy.group = fresh;
        if (auto* editor = this->editor()) editor->dirtifyTriggers();
    }
    int const at = m_doc.currentFrame + 1;
    clip->frames.insert(clip->frames.begin() + at, std::move(copy));
    m_doc.currentFrame = at;
    changed(clip);
    deselectLocked();
    return Ok();
}

Result<> AnimateSession::deleteFrame(bool deleteObjects) {
    std::string error;
    if (!canEdit(error)) return Err(error);
    auto* clip = activeClip();
    auto* frame = frameAt(m_doc.currentFrame);
    if (!clip || !frame) return Err(tr("Nothing to delete.", "No hay nada que borrar."));
    int const group = frame->group;
    clip->frames.erase(clip->frames.begin() + m_doc.currentFrame);
    bool const shared = group > 0 && std::any_of(clip->frames.begin(), clip->frames.end(),
        [group](Frame const& other) { return other.group == group; });
    if (deleteObjects && group > 0 && !shared) removeObjects(objectsIn(editor(), group));
    if (clip->frames.empty()) clip->frames.push_back({});
    if (m_doc.currentFrame >= static_cast<int>(clip->frames.size())) --m_doc.currentFrame;
    changed(clip);
    deselectLocked();
    return Ok();
}

Result<std::size_t> AnimateSession::clearFrame() {
    std::string error;
    if (!canEdit(error)) return Err(error);
    auto* clip = activeClip();
    auto* frame = frameAt(m_doc.currentFrame);
    if (!clip || !frame || frame->group <= 0) return Ok(std::size_t{0});
    auto const removed = removeObjects(objectsIn(editor(), frame->group));
    changed(clip);
    return Ok(removed);
}

void AnimateSession::moveFrame(int direction) {
    auto* clip = activeClip();
    if (!clip) return;
    int const target = m_doc.currentFrame + direction;
    if (target < 0 || target >= static_cast<int>(clip->frames.size())) return;
    std::swap(clip->frames[static_cast<std::size_t>(m_doc.currentFrame)],
        clip->frames[static_cast<std::size_t>(target)]);
    m_doc.currentFrame = target;
    changed(clip);
}

void AnimateSession::reverseFrames() {
    auto* clip = activeClip();
    if (!clip || clip->frames.size() < 2) return;
    std::reverse(clip->frames.begin(), clip->frames.end());
    m_doc.currentFrame = static_cast<int>(clip->frames.size()) - 1 - m_doc.currentFrame;
    changed(clip);
}

void AnimateSession::setHold(int index, int hold) {
    auto* frame = frameAt(index);
    if (!frame) return;
    hold = std::clamp(hold, 1, kMaxHold);
    if (frame->hold == hold) return;
    frame->hold = hold;
    changed(activeClip());
}

void AnimateSession::setLabel(int index, std::string label) {
    auto* frame = frameAt(index);
    if (!frame) return;
    frame->label = label.substr(0, 24);
    changed();
}

void AnimateSession::toggleSkip(int index) {
    auto* frame = frameAt(index);
    if (!frame) return;
    frame->skip = !frame->skip;
    changed(activeClip());
}

Result<> AnimateSession::linkGroup(int index, int group) {
    auto* frame = frameAt(index);
    if (!frame) return Err(tr("That frame no longer exists.", "Ese frame ya no existe."));
    if (group < 0 || group > kMaxGroup) {
        return Err(tr("Group ids go from 1 to 9999.", "Los grupos van de 1 a 9999."));
    }
    for (std::size_t i = 0; i < m_doc.clips.size(); ++i) {
        if (static_cast<int>(i) == m_doc.activeClip || group == 0) continue;
        for (auto const& other : m_doc.clips[i].frames) {
            if (other.group == group) {
                return Err(fmt::format("{} {}", tr("That group belongs to",
                    "Ese grupo pertenece a"), m_doc.clips[i].name));
            }
        }
    }
    frame->group = group;
    changed(activeClip());
    return Ok();
}

int AnimateSession::ensureGroup(Clip& clip, std::size_t frame) {
    if (frame >= clip.frames.size()) return 0;
    auto& target = clip.frames[frame];
    if (target.group > 0) return target.group;
    auto taken = reservedGroups();
    target.group = allocateGroup(taken);
    return target.group;
}

std::size_t AnimateSession::assignObjects(
    Clip& clip, std::size_t frame, std::vector<GameObject*> const& objects
) {
    auto* editor = this->editor();
    int const group = ensureGroup(clip, frame);
    if (!editor || group <= 0) return 0;
    auto const strip = frameGroups(clip);
    std::size_t assigned = 0;
    std::vector<GameObject*> touched;
    for (auto* object : objects) {
        for (int other : strip) {
            if (other != group && hasGroup(object, other)) object->removeFromGroup(other);
        }
        if (!hasGroup(object, group)) object->addToGroup(group);
        if (hasGroup(object, group)) ++assigned;
        touched.push_back(object);
    }
    editor->recreateGroups();
    syncUpdated(touched);
    touch(clip);
    return assigned;
}

std::size_t AnimateSession::removeObjects(std::vector<GameObject*> const& objects) {
    auto* editor = this->editor();
    if (!editor || objects.empty()) return 0;
    auto* ui = editor->m_editorUI;
    auto& collab = paimon::collab::CollabManager::get();
    std::vector<Ref<GameObject>> held(objects.begin(), objects.end());
    std::size_t removed = 0;
    for (auto& object : held) {
        if (!object || !object->getParent()) continue;
        if (ui && object->m_isSelected) ui->deselectObject(object);
        if (collab.connected()) collab.sendDeletedObject(object);
        editor->removeObject(object, true);
        ++removed;
    }
    m_dirty = true;
    return removed;
}

Result<std::size_t> AnimateSession::assignSelection() {
    std::string error;
    if (!canEdit(error)) return Err(error);
    auto* clip = activeClip();
    if (!clip) return Err(tr("Create a clip first.", "Primero crea una animacion."));
    if (clip->locked) return Err(tr("This clip is locked.", "Esta animacion esta bloqueada."));
    auto selected = selectionOf(ui());
    if (selected.empty()) return Err(tr("Nothing is selected.", "No hay nada seleccionado."));
    auto const assigned = assignObjects(*clip, static_cast<std::size_t>(m_doc.currentFrame), selected);
    changed(clip);
    return Ok(assigned);
}

Result<std::size_t> AnimateSession::selectFrameObjects() {
    auto* ui = this->ui();
    auto* frame = frameAt(m_doc.currentFrame);
    if (!ui || !frame) return Err(tr("Create a clip first.", "Primero crea una animacion."));
    auto objects = objectsIn(editor(), frame->group);
    ui->deselectAll();
    if (objects.empty()) return Ok(std::size_t{0});
    ui->selectObjects(toArray(objects), true);
    ui->updateButtons();
    return Ok(objects.size());
}

std::size_t AnimateSession::objectCount(int group) const {
    auto* editor = this->editor();
    if (!editor || group <= 0) return 0;
    auto* array = editor->getGroup(group);
    return array ? array->count() : 0;
}

std::size_t AnimateSession::selectionCount() const {
    return selectionOf(ui()).size();
}

std::unordered_set<int> AnimateSession::reservedGroups() const {
    std::unordered_set<int> groups;
    for (auto const& clip : m_doc.clips) {
        for (auto const& frame : clip.frames) {
            if (frame.group > 0) groups.insert(frame.group);
        }
        auto const& c = clip.compiled;
        for (int group : {c.marker, c.chain, c.start, c.stop, c.pause, c.resume}) {
            if (group > 0) groups.insert(group);
        }
    }
    return groups;
}

int AnimateSession::allocateGroup(std::unordered_set<int>& taken) const {
    auto* editor = this->editor();
    if (!editor) return 0;
    gd::unordered_set<int> exclude;
    for (int group : taken) exclude.insert(group);
    int const group = editor->getNextFreeGroupID(exclude);
    if (group <= 0 || group > kMaxGroup) return 0;
    taken.insert(group);
    return group;
}

void AnimateSession::togglePlay() {
    if (m_playing) {
        stopPlayback();
        return;
    }
    if (!activeClip() || isPlaytesting()) return;
    if (auto* ui = this->ui()) ui->deselectAll();
    m_playing = true;
    m_time = 0.f;
    rebuildSequences();
    // playback starts at the cursor, so play doubles as a scrub from where you are.
    auto const& steps = m_sequences[static_cast<std::size_t>(m_doc.activeClip)];
    for (auto const& step : steps) {
        if (static_cast<int>(step.frame) == m_doc.currentFrame) break;
        m_time += step.duration;
    }
    if (m_time >= sequenceDuration(steps)) m_time = 0.f;
    m_dirty = true;
}

void AnimateSession::stopPlayback() {
    if (!m_playing) return;
    m_playing = false;
    m_dirty = true;
}

void AnimateSession::rebuildSequences() {
    if (m_sequenceRevision == m_revision && m_sequences.size() == m_doc.clips.size()) return;
    m_sequences.clear();
    m_sequences.reserve(m_doc.clips.size());
    for (auto const& clip : m_doc.clips) m_sequences.push_back(buildSequence(clip));
    m_sequenceRevision = m_revision;
}

int AnimateSession::shownFrame(int clipIndex) {
    auto const& clip = m_doc.clips[static_cast<std::size_t>(clipIndex)];
    if (clip.frames.empty()) return -1;
    if (!m_playing) {
        if (clipIndex == m_doc.activeClip) return m_doc.currentFrame;
        auto const& steps = m_sequences[static_cast<std::size_t>(clipIndex)];
        return steps.empty() ? 0 : static_cast<int>(steps.front().frame);
    }

    auto const& steps = m_sequences[static_cast<std::size_t>(clipIndex)];
    float const total = sequenceDuration(steps);
    if (steps.empty() || total <= 0.f) return -1;
    float t = m_time;
    if (loops(clip.settings.mode) || m_prefs.previewLoop) {
        t = std::fmod(t, total);
    } else if (t >= total) {
        switch (clip.settings.end) {
            case EndAction::HoldLast: return static_cast<int>(steps.back().frame);
            case EndAction::HideAll: return -1;
            case EndAction::ShowFirst: return static_cast<int>(steps.front().frame);
        }
    }
    for (auto const& step : steps) {
        if (t < step.duration) return static_cast<int>(step.frame);
        t -= step.duration;
    }
    return static_cast<int>(steps.back().frame);
}

int AnimateSession::displayedFrame() {
    if (!activeClip()) return -1;
    rebuildSequences();
    return shownFrame(m_doc.activeClip);
}

void AnimateSession::tick(float dt) {
    if (!m_playing) return;
    if (!m_open || isPlaytesting() || !std::isfinite(dt)) {
        stopPlayback();
        return;
    }
    rebuildSequences();
    m_time += std::clamp(dt, 0.f, 0.25f);

    if (!m_prefs.previewLoop) {
        float longest = 0.f;
        bool endless = false;
        for (std::size_t i = 0; i < m_doc.clips.size(); ++i) {
            if (m_doc.clips[i].hidden) continue;
            endless |= loops(m_doc.clips[i].settings.mode);
            longest = std::max(longest, sequenceDuration(m_sequences[i]));
        }
        if (!endless && m_time >= longest) {
            m_time = longest;
            stopPlayback();
        }
    }

    if (m_prefs.followPlayhead && activeClip()) {
        int const shown = shownFrame(m_doc.activeClip);
        if (shown >= 0) m_doc.currentFrame = shown;
    }
    m_dirty = true;
}

void AnimateSession::recompute() {
    m_dirty = false;
    m_overrides.clear();
    m_locked.clear();
    if (!m_open || !editor()) return;
    rebuildSequences();

    for (int index = 0; index < static_cast<int>(m_doc.clips.size()); ++index) {
        auto const& clip = m_doc.clips[static_cast<std::size_t>(index)];
        auto const groups = frameGroups(clip);
        if (clip.hidden) {
            for (int group : groups) {
                m_overrides[group] = 0;
                m_locked.insert(group);
            }
            continue;
        }

        bool const active = index == m_doc.activeClip;
        int const shown = shownFrame(index);
        int const shownGroup = shown >= 0 ? clip.frames[static_cast<std::size_t>(shown)].group : 0;

        int ghost = 0;
        if (!m_playing) {
            if (m_prefs.ghosts == Ghosts::Visible) ghost = 255;
            else if (m_prefs.ghosts == Ghosts::Dim && active) ghost = m_prefs.dimOpacity;
        }

        for (int group : groups) {
            if (group == shownGroup) continue;
            if (ghost < 255) m_overrides[group] = static_cast<GLubyte>(ghost);
            if (m_prefs.lockOthers || clip.locked) m_locked.insert(group);
        }
        if (clip.locked && shownGroup > 0) m_locked.insert(shownGroup);

        if (active && !m_playing && m_prefs.onion && shown >= 0) {
            int const count = static_cast<int>(clip.frames.size());
            auto onion = [&](int distance, int steps) {
                int const at = shown + distance;
                if (at < 0 || at >= count) return;
                int const group = clip.frames[static_cast<std::size_t>(at)].group;
                if (group <= 0 || group == shownGroup) return;
                float const falloff = 1.f - static_cast<float>(std::abs(distance) - 1) /
                    static_cast<float>(std::max(1, steps));
                auto const opacity = static_cast<GLubyte>(std::clamp(
                    static_cast<float>(m_prefs.onionOpacity) * falloff, 8.f, 254.f));
                auto it = m_overrides.find(group);
                if (it != m_overrides.end()) it->second = std::max(it->second, opacity);
            };
            for (int d = 1; d <= m_prefs.onionBefore; ++d) onion(-d, m_prefs.onionBefore);
            for (int d = 1; d <= m_prefs.onionAfter; ++d) onion(d, m_prefs.onionAfter);
        }
    }
}

void AnimateSession::restore(std::vector<int> const& groups) {
    auto* editor = this->editor();
    if (!editor) return;
    for (int group : groups) {
        if (auto* objects = editor->getGroup(group)) {
            for (auto* object : CCArrayExt<GameObject*>(objects)) object->setOpacity(255);
        }
    }
}

void AnimateSession::applyOverrides() {
    auto* editor = this->editor();
    if (!editor) return;
    if (!m_open || isPlaytesting()) {
        if (!m_touched.empty()) {
            restore(m_touched);
            m_touched.clear();
        }
        return;
    }
    if (m_dirty) recompute();

    std::vector<int> released;
    for (int group : m_touched) {
        if (!m_overrides.contains(group)) released.push_back(group);
    }
    if (!released.empty()) restore(released);

    m_touched.clear();
    m_touched.reserve(m_overrides.size());
    for (auto const& [group, opacity] : m_overrides) {
        m_touched.push_back(group);
        auto* objects = editor->getGroup(group);
        if (!objects) continue;
        for (auto* object : CCArrayExt<GameObject*>(objects)) object->setOpacity(opacity);
    }
}

void AnimateSession::releaseOverrides() {
    if (!m_touched.empty()) restore(m_touched);
    m_touched.clear();
    m_dirty = true;
}

bool AnimateSession::isLocked(GameObject* object) {
    if (!m_open || m_suspendLock || !object || isPlaytesting()) return false;
    if (m_dirty) recompute();
    if (m_locked.empty()) return false;
    for (int i = 0; i < object->m_groupCount; ++i) {
        if (m_locked.contains(object->getGroupID(i))) return true;
    }
    return false;
}

void AnimateSession::deselectLocked() {
    auto* ui = this->ui();
    if (!ui || !m_open) return;
    m_dirty = true;
    std::vector<Ref<GameObject>> locked;
    for (auto* object : selectionOf(ui)) {
        if (isLocked(object)) locked.emplace_back(object);
    }
    for (auto& object : locked) ui->deselectObject(object);
    if (!locked.empty()) ui->updateButtons();
}

void AnimateSession::onObjectCreated(GameObject* object) {
    auto* editor = this->editor();
    auto* clip = activeClip();
    if (!object || !editor || !m_open || !m_prefs.autoAssign || isPlaytesting()) return;
    if (!clip || clip->locked || clip->hidden) return;
    int const group = ensureGroup(*clip, static_cast<std::size_t>(m_doc.currentFrame));
    if (group <= 0) return;
    object->addToGroup(group);
    editor->addToGroup(object, group, false);
    syncUpdated({object});
    changed(clip);
}

void AnimateSession::onObjectsPasted(CCArray* objects) {
    auto* editor = this->editor();
    auto* clip = activeClip();
    if (!objects || !editor || !m_open || !m_prefs.autoAssign || isPlaytesting()) return;
    if (!clip || clip->locked || clip->hidden || objects->count() == 0) return;

    auto const others = reservedGroups();
    auto const own = frameGroups(*clip);
    std::vector<GameObject*> adopt;
    for (auto* object : CCArrayExt<GameObject*>(objects)) {
        if (!object) continue;
        bool ours = false;
        bool foreign = false;
        for (int i = 0; i < object->m_groupCount; ++i) {
            int const group = object->getGroupID(i);
            if (std::find(own.begin(), own.end(), group) != own.end()) ours = true;
            else if (others.contains(group)) foreign = true;
        }
        if (ours || !foreign) adopt.push_back(object);
    }
    if (adopt.empty()) return;
    assignObjects(*clip, static_cast<std::size_t>(m_doc.currentFrame), adopt);
    changed(clip);
}

} // namespace paimon::animate
