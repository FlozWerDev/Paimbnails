#include "EditorHistoryTracker.hpp"

#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GameObjectCopy.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/UndoObject.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace paimon::editorhistory {

namespace {

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string localAuthorName() {
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected()) {
        auto name = collab.peerName(collab.clientId());
        if (!name.empty()) return name;
    }
    auto custom = Mod::get()->getSettingValue<std::string>("collab-username");
    if (!custom.empty()) return custom;
    if (auto* gm = GameManager::sharedState()) {
        auto n = std::string(gm->m_playerName);
        if (!n.empty()) return n;
    }
    return "You";
}

GameObject* asGameObject(CCObject* obj) {
    if (auto* go = typeinfo_cast<GameObject*>(obj)) return go;
    if (auto* copy = typeinfo_cast<GameObjectCopy*>(obj)) return copy->m_object;
    return nullptr;
}

std::unordered_map<int, std::string> parseSaveKeys(std::string const& save) {
    std::unordered_map<int, std::string> out;
    if (save.empty()) return out;
    std::vector<std::string> parts;
    parts.reserve(64);
    std::string token;
    std::stringstream ss(save);
    while (std::getline(ss, token, ',')) parts.push_back(token);
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        try {
            out[std::stoi(parts[i])] = parts[i + 1];
        } catch (...) {}
    }
    return out;
}

bool isColorKey(int k) {
    switch (k) {
        case 21: case 22: case 23: case 24:
        case 41: case 42: case 43:
        case 105:
            return true;
        default: return false;
    }
}

struct DiffFlags {
    bool color = false;
    bool transform = false;
    bool property = false;
    bool moved = false;
    bool scaled = false;
    bool rotated = false;
    bool flipped = false;
};

DiffFlags diffSaves(std::string const& before, std::string const& after) {
    DiffFlags f;
    if (before.empty() || after.empty() || before == after) return f;
    auto a = parseSaveKeys(before);
    auto b = parseSaveKeys(after);
    std::unordered_set<int> keys;
    for (auto const& [k, _] : a) keys.insert(k);
    for (auto const& [k, _] : b) keys.insert(k);
    for (int k : keys) {
        auto ia = a.find(k);
        auto ib = b.find(k);
        std::string va = ia == a.end() ? "" : ia->second;
        std::string vb = ib == b.end() ? "" : ib->second;
        if (va == vb) continue;
        if (isColorKey(k)) f.color = true;
        else if (k == 2 || k == 3) f.moved = true;
        else if (k == 32 || k == 128 || k == 129) f.scaled = true;
        else if (k == 6 || k == 131 || k == 132) f.rotated = true;
        else if (k == 4 || k == 5) f.flipped = true;
        else f.property = true;
    }
    if (f.moved || f.scaled || f.rotated || f.flipped) f.transform = true;
    return f;
}

ActionKind flagsToKind(DiffFlags const& f) {
    int geo = (f.moved ? 1 : 0) + (f.scaled ? 1 : 0) + (f.rotated ? 1 : 0) + (f.flipped ? 1 : 0);
    if (f.color && geo == 0 && !f.property) return ActionKind::Color;
    if (f.color && geo == 0) return ActionKind::Color; // color+groups still "Recolor" for UX
    if (geo == 1) {
        if (f.moved) return ActionKind::Move;
        if (f.scaled) return ActionKind::Scale;
        if (f.rotated) return ActionKind::Rotate;
        if (f.flipped) return ActionKind::Flip;
    }
    if (geo > 1) return ActionKind::Transform;
    if (f.property) return ActionKind::Property;
    if (f.color) return ActionKind::Color;
    return ActionKind::Property;
}

bool nearly(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

ActionKind kindFromCopy(GameObjectCopy* copy) {
    if (!copy || !copy->m_object) return ActionKind::Property;
    auto* live = copy->m_object;
    bool moved = !nearly(copy->m_position.x, live->getPositionX()) ||
                 !nearly(copy->m_position.y, live->getPositionY());
    bool scaled = !nearly(copy->m_customScaleX, live->m_scaleX) ||
                  !nearly(copy->m_customScaleY, live->m_scaleY);
    bool rotated = !nearly(copy->m_rotationX, live->getRotation()) &&
                   !(nearly(copy->m_rotationX, 0.f) && nearly(live->getRotation(), 0.f));
    bool flipped = (copy->m_isFlipX != live->m_isFlipX) || (copy->m_isFlipY != live->m_isFlipY);
    DiffFlags df;
    df.moved = moved; df.scaled = scaled; df.rotated = rotated; df.flipped = flipped;
    df.transform = moved || scaled || rotated || flipped;
    if (!df.transform) return ActionKind::Property;
    return flagsToKind(df);
}

ActionKind kindFromTransformState(UndoObject* undo) {
    if (!undo) return ActionKind::Transform;
    auto const& t = undo->m_transformState;
    bool scaled = !(nearly(t.m_scaleX, 0.f) && nearly(t.m_scaleY, 0.f)) &&
                  !(nearly(t.m_scaleX, 1.f) && nearly(t.m_scaleY, 1.f));
    bool rotated = !nearly(t.m_angleX, 0.f) || !nearly(t.m_angleY, 0.f) ||
                   !nearly(t.m_transformRotation, 0.f);
    bool moved = !nearly(t.m_transformPosition.x, 0.f) || !nearly(t.m_transformPosition.y, 0.f);
    int n = (scaled ? 1 : 0) + (rotated ? 1 : 0) + (moved ? 1 : 0);
    if (n == 0) return ActionKind::Property;
    if (n == 1) {
        if (moved) return ActionKind::Move;
        if (scaled) return ActionKind::Scale;
        if (rotated) return ActionKind::Rotate;
    }
    return ActionKind::Transform;
}

CCArray* selectedOrEmpty(LevelEditorLayer* lel) {
    if (!lel || !lel->m_editorUI) return nullptr;
    return lel->m_editorUI->getSelectedObjects();
}

} // namespace

// historyEnabled() lives in ObjectTimelineStore.cpp (shared by both systems).

EditorHistoryTracker& EditorHistoryTracker::get() {
    static EditorHistoryTracker instance;
    return instance;
}

void EditorHistoryTracker::clear() {
    m_meta.clear();
    m_collabFeed.clear();
    m_localEdits.clear();
    m_propSnap.clear();
    m_channelSnap.clear();
    m_preEditSnap.clear();
    m_preEditChannels.clear();
    m_hasPreEdit = false;
    m_editor = nullptr;
    m_lastUndoCount = -1;
    m_lastRedoCount = -1;
    ++m_undoRevision;
    ++m_collabRevision;
    ++m_localRevision;
}

void EditorHistoryTracker::setEditor(LevelEditorLayer* editor) {
    if (m_editor != editor) {
        m_meta.clear();
        m_propSnap.clear();
        m_channelSnap.clear();
        m_preEditSnap.clear();
        m_preEditChannels.clear();
        m_hasPreEdit = false;
        m_lastUndoCount = -1;
        m_lastRedoCount = -1;
        m_editor = editor;
        ++m_undoRevision;
    }
}

std::vector<ChannelSnap> EditorHistoryTracker::captureChannels(LevelEditorLayer* lel, GameObject* go) {
    std::vector<ChannelSnap> out;
    if (!go) return out;

    // 1) Always capture what the user actually sees on the sprites.
    if (go->m_colorSprite) {
        ChannelSnap s;
        s.channelId = -1; // main/color sprite
        s.color = go->m_colorSprite->getColor();
        s.opacity = go->m_colorSprite->getOpacity() / 255.f;
        out.push_back(s);
    }
    // GameObject itself is often a CCSprite subclass with a base tint.
    {
        ChannelSnap s;
        s.channelId = -3; // node tint
        s.color = go->getColor();
        s.opacity = go->getOpacity() / 255.f;
        out.push_back(s);
    }
    if (go->m_baseColor && go->m_baseColor->m_usesHSV) {
        ChannelSnap s;
        s.channelId = -4;
        s.color = go->m_baseColor->m_customColor;
        s.copyHSV = go->m_baseColor->m_hsv;
        s.opacity = go->m_baseColor->m_opacity;
        out.push_back(s);
    }

    // 2) Level color channels referenced by the object.
    if (lel && lel->m_effectManager) {
        std::unordered_set<int> ids;
        if (go->m_baseColor) {
            int id = go->m_baseColor->m_colorID;
            if (id > 0) ids.insert(id);
        }
        if (go->m_detailColor) {
            int id = go->m_detailColor->m_colorID;
            if (id > 0) ids.insert(id);
        }
        try {
            auto keys = parseSaveKeys(std::string(go->getSaveString(lel)));
            for (int k : {21, 22}) {
                auto it = keys.find(k);
                if (it == keys.end()) continue;
                try {
                    int id = std::stoi(it->second);
                    if (id > 0) ids.insert(id);
                } catch (...) {}
            }
        } catch (...) {}

        for (int id : ids) {
            auto* ca = lel->m_effectManager->getColorAction(id);
            if (!ca) continue;
            ChannelSnap s;
            s.channelId = id;
            s.color = ca->m_color;
            if (s.color.r == 0 && s.color.g == 0 && s.color.b == 0)
                s.color = ca->m_toColor;
            if (s.color.r == 0 && s.color.g == 0 && s.color.b == 0)
                s.color = ca->m_fromColor;
            s.opacity = ca->m_toOpacity > 0.f ? ca->m_toOpacity : ca->m_currentOpacity;
            s.blending = ca->m_blending;
            s.playerColor = ca->m_playerColor;
            s.copyID = ca->m_copyID;
            s.copyHSV = ca->m_copyHSV;
            s.copyOpacity = ca->m_copyOpacity;
            out.push_back(s);
        }
    }
    return out;
}

std::string EditorHistoryTracker::channelSignature(std::vector<ChannelSnap> const& ch) {
    std::string s;
    s.reserve(ch.size() * 28);
    for (auto const& c : ch) {
        s += fmt::format("{}:{},{},{},{:.3f},{},{};",
            c.channelId, c.color.r, c.color.g, c.color.b,
            c.opacity, c.blending ? 1 : 0, c.copyID);
    }
    return s;
}

void EditorHistoryTracker::applyChannels(LevelEditorLayer* lel, std::vector<ChannelSnap> const& ch) {
    applyChannels(lel, nullptr, ch);
}

void EditorHistoryTracker::applyChannels(
    LevelEditorLayer* lel, GameObject* go, std::vector<ChannelSnap> const& ch
) {
    if (ch.empty()) return;

    for (auto const& s : ch) {
        if (s.channelId > 0) {
            if (!lel || !lel->m_effectManager) continue;
            auto* ca = lel->m_effectManager->getColorAction(s.channelId);
            if (!ca) continue;
            ca->m_color = s.color;
            ca->m_fromColor = s.color;
            ca->m_toColor = s.color;
            ca->m_fromOpacity = s.opacity;
            ca->m_toOpacity = s.opacity;
            ca->m_currentOpacity = s.opacity;
            ca->m_blending = s.blending;
            ca->m_playerColor = s.playerColor;
            ca->m_copyID = s.copyID;
            ca->m_copyHSV = s.copyHSV;
            ca->m_copyOpacity = s.copyOpacity;
            lel->m_effectManager->colorActionChanged(ca);
            continue;
        }
        // Sprite / object tint snaps
        if (!go) continue;
        if (s.channelId == -1 || s.channelId == -3) {
            go->setObjectColor(s.color);
            go->setColor(s.color);
            if (go->m_colorSprite) {
                go->m_colorSprite->setColor(s.color);
                go->m_colorSprite->setOpacity(static_cast<GLubyte>(
                    std::clamp(s.opacity, 0.f, 1.f) * 255.f
                ));
            }
        } else if (s.channelId == -2 && go->m_colorSprite) {
            go->m_colorSprite->setColor(s.color);
        } else if (s.channelId == -4 && go->m_baseColor) {
            go->m_baseColor->m_customColor = s.color;
            go->m_baseColor->m_hsv = s.copyHSV;
            go->m_baseColor->m_usesHSV = true;
            go->m_baseColor->m_opacity = s.opacity;
        }
    }
}

LocalEditEntry* EditorHistoryTracker::mutableLocalEditById(uint64_t id) {
    for (auto& e : m_localEdits) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

ActionKind EditorHistoryTracker::classify(UndoObject* undo, LevelEditorLayer* lel) {
    if (!undo) return ActionKind::Unknown;
    switch (undo->m_command) {
        case UndoCommand::New: return ActionKind::Create;
        case UndoCommand::Paste: return ActionKind::Paste;
        case UndoCommand::Delete:
        case UndoCommand::DeleteMulti: return ActionKind::Delete;
        case UndoCommand::Select: return ActionKind::Select;
        case UndoCommand::Transform: break;
        default: return ActionKind::Unknown;
    }
    if (undo->m_objectCopy) {
        auto k = kindFromCopy(undo->m_objectCopy);
        if (k == ActionKind::Property && lel && undo->m_objectCopy->m_object) {
            auto* go = undo->m_objectCopy->m_object;
            auto& self = EditorHistoryTracker::get();
            auto it = self.m_propSnap.find(go->m_uniqueID);
            if (it != self.m_propSnap.end()) {
                auto df = diffSaves(it->second, std::string(go->getSaveString(lel)));
                return flagsToKind(df);
            }
            return kindFromTransformState(undo);
        }
        return k;
    }
    if (undo->m_objects && undo->m_objects->count() > 0) {
        if (auto* copy = typeinfo_cast<GameObjectCopy*>(undo->m_objects->objectAtIndex(0))) {
            return kindFromCopy(copy);
        }
        return kindFromTransformState(undo);
    }
    return kindFromTransformState(undo);
}

std::string EditorHistoryTracker::describe(UndoObject* undo, LevelEditorLayer* lel) {
    return actionLabel(classify(undo, lel), objectCount(undo));
}

void EditorHistoryTracker::pushLocalEdit(LocalEditEntry e) {
    // Fill preview swatches from channel/sprite snaps.
    if (!e.beforeChannels.empty() || !e.afterChannels.empty()) {
        e.previewBefore = previewFromChannels(e.beforeChannels);
        e.previewAfter = previewFromChannels(e.afterChannels);
        e.hasColorPreview = true;
    }

    // Coalesce rapid identical edits on the same object (slider spam).
    if (!m_localEdits.empty()) {
        auto& last = m_localEdits.back();
        if (last.uniqueId == e.uniqueId && last.kind == e.kind &&
            (e.timeMs - last.timeMs) < 800) {
            last.afterSave = std::move(e.afterSave);
            last.afterChannels = std::move(e.afterChannels);
            last.previewAfter = e.previewAfter;
            last.hasColorPreview = last.hasColorPreview || e.hasColorPreview;
            last.timeMs = e.timeMs;
            last.label = e.label;
            last.objectCount = e.objectCount;
            last.restored = false;
            last.canRestore = !last.beforeSave.empty() || !last.beforeChannels.empty();
            ++m_localRevision;
            ++m_undoRevision;
            return;
        }
    }
    e.canRestore = !e.beforeSave.empty() || !e.beforeChannels.empty();
    e.restored = false;
    log::info("[History] local edit id={} kind={} uid={} save={}B ch={} preview={}",
        e.id, actionKindName(e.kind), e.uniqueId, e.beforeSave.size(),
        e.beforeChannels.size(), e.hasColorPreview);
    m_localEdits.push_back(std::move(e));
    while (m_localEdits.size() > kMaxLocalEdits) m_localEdits.pop_front();
    ++m_localRevision;
    ++m_undoRevision;
}

void EditorHistoryTracker::refineTopUndo(ActionKind kind) {
    if (!m_editor || !m_editor->m_undoObjects || m_editor->m_undoObjects->count() == 0) return;
    auto* top = typeinfo_cast<UndoObject*>(m_editor->m_undoObjects->lastObject());
    if (!top) return;
    auto it = m_meta.find(top);
    if (it == m_meta.end()) {
        onUndoAdded(top, true);
        it = m_meta.find(top);
    }
    if (it == m_meta.end()) return;
    // Prefer more specific labels over generic Transform/Property
    if (kind != ActionKind::Unknown && kind != ActionKind::Select) {
        it->second.kind = kind;
        it->second.label = actionLabel(kind, objectCount(top));
        ++m_undoRevision;
    }
}

void EditorHistoryTracker::onUndoAdded(UndoObject* undo, bool /*keepRedo*/) {
    if (!undo) return;
    if (m_meta.count(undo)) return;

    UndoMeta meta;
    meta.seq = m_nextSeq++;
    meta.timeMs = nowMs();
    auto& collab = paimon::collab::CollabManager::get();
    meta.collabSession = collab.connected();
    meta.clientId = meta.collabSession ? collab.clientId() : 0;
    meta.author = meta.collabSession ? localAuthorName() : "You";
    meta.kind = classify(undo, m_editor);
    meta.label = actionLabel(meta.kind, objectCount(undo));
    m_meta[undo] = std::move(meta);
    ++m_undoRevision;

    auto const& stored = m_meta[undo];
    if (stored.collabSession && false) {
        if (stored.kind == ActionKind::Select) return;
        CCPoint pos{};
        if (auto* arr = collectObjects(undo); arr && arr->count() > 0) {
            if (auto* go = asGameObject(arr->objectAtIndex(0))) pos = go->getPosition();
        }
        onCollabOp(stored.clientId, stored.author, actionKindName(stored.kind),
                   objectCount(undo), firstObjectId(undo), pos,
                   buildSaveString(undo, m_editor), true);
    }
}

void EditorHistoryTracker::snapshotSelection(char const* /*reason*/) {
    if (!m_editor) return;
    m_preEditSnap.clear();
    m_preEditChannels.clear();
    m_hasPreEdit = false;
    auto* selected = selectedOrEmpty(m_editor);
    if (!selected || selected->count() == 0) return;

    size_t limit = std::min<unsigned int>(selected->count(), 400u);
    for (unsigned int i = 0; i < limit; ++i) {
        auto* go = typeinfo_cast<GameObject*>(selected->objectAtIndex(i));
        if (!go || !go->getParent()) continue;
        m_preEditSnap[go->m_uniqueID] = std::string(go->getSaveString(m_editor));
        m_propSnap[go->m_uniqueID] = m_preEditSnap[go->m_uniqueID];
        auto ch = captureChannels(m_editor, go);
        m_channelSnap[go->m_uniqueID] = ch;
        m_preEditChannels[go->m_uniqueID] = std::move(ch);
    }
    m_hasPreEdit = !m_preEditSnap.empty() || !m_preEditChannels.empty();
}

void EditorHistoryTracker::noteObjectsChanged(ActionKind hint) {
    m_preEditHint = hint;
    // Defer one frame-ish: flush on next poll, or call flush immediately.
    flushSelectionDiff(hint);
}

void EditorHistoryTracker::flushSelectionDiff(ActionKind hint) {
    if (!m_editor || paimon::collab::CollabManager::get().isApplyingRemote()) return;

    auto* selected = selectedOrEmpty(m_editor);
    if (!selected || selected->count() == 0) {
        m_hasPreEdit = false;
        m_preEditSnap.clear();
        return;
    }

    int undoN = m_editor->m_undoObjects ? static_cast<int>(m_editor->m_undoObjects->count()) : 0;
    bool stackGrew = (m_lastUndoCount >= 0 && undoN > m_lastUndoCount);

    DiffFlags aggregate{};
    std::vector<GameObject*> changed;
    std::string beforeSample, afterSample;
    std::vector<ChannelSnap> beforeCh, afterCh;
    int firstUid = 0, firstOid = 0;
    CCPoint pos{};
    bool channelOnly = false;

    auto const& baseline = m_hasPreEdit ? m_preEditSnap : m_propSnap;

    size_t limit = std::min<unsigned int>(selected->count(), 400u);
    for (unsigned int i = 0; i < limit; ++i) {
        auto* go = typeinfo_cast<GameObject*>(selected->objectAtIndex(i));
        if (!go || !go->getParent()) continue;
        std::string cur = std::string(go->getSaveString(m_editor));
        auto curCh = captureChannels(m_editor, go);

        bool saveChanged = false;
        bool chanChanged = false;
        DiffFlags df{};

        auto it = baseline.find(go->m_uniqueID);
        if (it != baseline.end() && it->second != cur) {
            df = diffSaves(it->second, cur);
            saveChanged = df.color || df.property || df.transform || df.moved ||
                          df.scaled || df.rotated || df.flipped;
        }

        // Channel RGB: compare against pre-edit channels or last poll snap.
        std::vector<ChannelSnap> prevCh;
        auto pit = m_preEditChannels.find(go->m_uniqueID);
        if (pit != m_preEditChannels.end()) prevCh = pit->second;
        else {
            auto cit = m_channelSnap.find(go->m_uniqueID);
            if (cit != m_channelSnap.end()) prevCh = cit->second;
        }
        if (!prevCh.empty() && channelSignature(prevCh) != channelSignature(curCh)) {
            chanChanged = true;
        }

        // Color hint from UI hooks: always treat as color change even if
        // save string textually matches (channel RGB only).
        if (hint == ActionKind::Color && m_hasPreEdit) {
            chanChanged = true;
        }

        if (saveChanged || chanChanged) {
            changed.push_back(go);
            if (saveChanged) {
                aggregate.color |= df.color;
                aggregate.property |= df.property;
                aggregate.transform |= df.transform;
                aggregate.moved |= df.moved;
                aggregate.scaled |= df.scaled;
                aggregate.rotated |= df.rotated;
                aggregate.flipped |= df.flipped;
            }
            if (chanChanged) {
                aggregate.color = true;
                if (!saveChanged) channelOnly = true;
            }
            if (!firstUid) {
                firstUid = go->m_uniqueID;
                firstOid = go->m_objectID;
                pos = go->getPosition();
                beforeSample = (it != baseline.end()) ? it->second : cur;
                afterSample = cur;
                beforeCh = prevCh;
                afterCh = curCh;
            }
        }

        m_propSnap[go->m_uniqueID] = cur;
        m_channelSnap[go->m_uniqueID] = curCh;
    }

    m_hasPreEdit = false;
    m_preEditSnap.clear();
    m_preEditChannels.clear();

    if (changed.empty()) return;

    ActionKind kind = (hint != ActionKind::Unknown) ? hint : flagsToKind(aggregate);
    if (hint == ActionKind::Unknown || hint == ActionKind::Property || hint == ActionKind::Transform) {
        kind = channelOnly ? ActionKind::Color : flagsToKind(aggregate);
    } else if (hint == ActionKind::Color && aggregate.scaled && !channelOnly) {
        kind = ActionKind::Scale;
    }
    if (channelOnly) kind = ActionKind::Color;

    if (stackGrew) {
        refineTopUndo(kind);
        return;
    }

    LocalEditEntry e;
    e.id = m_nextLocalId++;
    e.timeMs = nowMs();
    e.kind = kind;
    e.label = actionLabel(kind, static_cast<int>(changed.size()));
    e.objectCount = static_cast<int>(changed.size());
    e.objectId = firstOid;
    e.uniqueId = firstUid;
    e.worldPos = pos;
    e.beforeSave = std::move(beforeSample);
    e.afterSave = std::move(afterSample);
    e.beforeChannels = std::move(beforeCh);
    e.afterChannels = std::move(afterCh);
    pushLocalEdit(std::move(e));

    if (paimon::collab::CollabManager::get().connected()) {
        onCollabOp(
            paimon::collab::CollabManager::get().clientId(),
            localAuthorName(),
            actionKindName(kind),
            static_cast<int>(changed.size()),
            firstOid, pos, {}, true
        );
    }
}

void EditorHistoryTracker::pollPropertyEdits() {
    if (!m_editor || !m_editor->m_editorUI) return;
    if (paimon::collab::CollabManager::get().isApplyingRemote()) return;

    auto* selected = selectedOrEmpty(m_editor);
    if (!selected || selected->count() == 0) return;

    int undoN = m_editor->m_undoObjects ? static_cast<int>(m_editor->m_undoObjects->count()) : 0;
    bool stackGrew = (m_lastUndoCount >= 0 && undoN > m_lastUndoCount);

    DiffFlags aggregate{};
    std::vector<GameObject*> changed;
    std::string beforeSample, afterSample;
    std::vector<ChannelSnap> beforeCh, afterCh;
    int firstUid = 0, firstOid = 0;
    CCPoint pos{};
    bool channelOnly = false;

    size_t limit = std::min<unsigned int>(selected->count(), 300u);
    for (unsigned int i = 0; i < limit; ++i) {
        auto* go = typeinfo_cast<GameObject*>(selected->objectAtIndex(i));
        if (!go || !go->getParent()) continue;

        std::string cur = std::string(go->getSaveString(m_editor));
        auto curCh = captureChannels(m_editor, go);
        auto curSig = channelSignature(curCh);

        bool saveChanged = false;
        bool chanChanged = false;
        DiffFlags df{};

        auto it = m_propSnap.find(go->m_uniqueID);
        if (it != m_propSnap.end() && it->second != cur) {
            df = diffSaves(it->second, cur);
            saveChanged = df.color || df.property || df.transform || df.moved ||
                          df.scaled || df.rotated || df.flipped;
        }

        auto cit = m_channelSnap.find(go->m_uniqueID);
        std::string prevSig = (cit != m_channelSnap.end()) ? channelSignature(cit->second) : std::string();
        if (cit != m_channelSnap.end() && prevSig != curSig) {
            chanChanged = true;
        }

        if (saveChanged || chanChanged) {
            changed.push_back(go);
            if (saveChanged) {
                aggregate.color |= df.color;
                aggregate.property |= df.property;
                aggregate.transform |= df.transform;
                aggregate.moved |= df.moved;
                aggregate.scaled |= df.scaled;
                aggregate.rotated |= df.rotated;
                aggregate.flipped |= df.flipped;
            }
            if (chanChanged) {
                aggregate.color = true;
                if (!saveChanged) channelOnly = true;
            }
            if (!firstUid) {
                firstUid = go->m_uniqueID;
                firstOid = go->m_objectID;
                pos = go->getPosition();
                beforeSample = (it != m_propSnap.end()) ? it->second : cur;
                afterSample = cur;
                afterCh = curCh;
                // Prefer explicit pre-edit from color UI hooks, else previous poll snap.
                auto pit = m_preEditChannels.find(go->m_uniqueID);
                if (pit != m_preEditChannels.end() && !pit->second.empty()) {
                    beforeCh = pit->second;
                } else if (cit != m_channelSnap.end()) {
                    beforeCh = cit->second;
                }
            }
        }

        m_propSnap[go->m_uniqueID] = std::move(cur);
        m_channelSnap[go->m_uniqueID] = std::move(curCh);
    }

    if (m_propSnap.size() > 5000) {
        m_propSnap.clear();
        m_channelSnap.clear();
    }
    if (changed.empty()) return;

    ActionKind kind = channelOnly ? ActionKind::Color : flagsToKind(aggregate);
    if (stackGrew) {
        refineTopUndo(kind);
        return;
    }

    LocalEditEntry e;
    e.id = m_nextLocalId++;
    e.timeMs = nowMs();
    e.kind = kind;
    e.label = actionLabel(kind, static_cast<int>(changed.size()));
    e.objectCount = static_cast<int>(changed.size());
    e.objectId = firstOid;
    e.uniqueId = firstUid;
    e.worldPos = pos;
    e.beforeSave = std::move(beforeSample);
    e.afterSave = std::move(afterSample);
    e.beforeChannels = std::move(beforeCh);
    e.afterChannels = std::move(afterCh);
    pushLocalEdit(std::move(e));
}

void EditorHistoryTracker::pollStacks() {
    if (!m_editor) {
        if (auto* lel = LevelEditorLayer::get()) setEditor(lel);
        if (!m_editor) return;
    }

    auto scan = [&](CCArray* arr) {
        if (!arr) return;
        for (auto* o : CCArrayExt<CCObject*>(arr)) {
            if (auto* undo = typeinfo_cast<UndoObject*>(o)) {
                if (!m_meta.count(undo)) onUndoAdded(undo, true);
            }
        }
    };
    scan(m_editor->m_undoObjects);
    scan(m_editor->m_redoObjects);

    pollPropertyEdits();

    int undoN = m_editor->m_undoObjects ? static_cast<int>(m_editor->m_undoObjects->count()) : 0;
    int redoN = m_editor->m_redoObjects ? static_cast<int>(m_editor->m_redoObjects->count()) : 0;
    if (undoN != m_lastUndoCount || redoN != m_lastRedoCount) {
        m_lastUndoCount = undoN;
        m_lastRedoCount = redoN;
        ++m_undoRevision;
    }

    prune();
}

void EditorHistoryTracker::onCollabOp(
    int clientId, std::string const& author, std::string const& kind,
    int objectCount, int objectId, CCPoint worldPos, std::string savePreview, bool isLocal
) {
    if (!isLocal) return; // collab audit feed removed

    CollabFeedEntry e;
    e.id = m_nextCollabId++;
    e.timeMs = nowMs();
    e.clientId = clientId;
    e.author = author.empty() ? (isLocal ? "You" : "peer") : author;
    e.kind = kind;
    e.objectCount = objectCount;
    e.objectId = objectId;
    e.worldPos = worldPos;
    if (savePreview.size() > kMaxSavePreview) savePreview.resize(kMaxSavePreview);
    e.savePreview = std::move(savePreview);
    e.isLocal = isLocal;
    e.isRemote = !isLocal;
    m_collabFeed.push_back(std::move(e));
    while (m_collabFeed.size() > kMaxCollabFeed) m_collabFeed.pop_front();
    ++m_collabRevision;
}

UndoMeta const* EditorHistoryTracker::metaFor(UndoObject* undo) const {
    if (!undo) return nullptr;
    auto it = m_meta.find(undo);
    return it == m_meta.end() ? nullptr : &it->second;
}

void EditorHistoryTracker::prune() {
    if (!m_editor) { m_meta.clear(); return; }
    std::unordered_set<UndoObject*> live;
    auto absorb = [&](CCArray* arr) {
        if (!arr) return;
        for (auto* o : CCArrayExt<CCObject*>(arr)) {
            if (auto* u = typeinfo_cast<UndoObject*>(o)) live.insert(u);
        }
    };
    absorb(m_editor->m_undoObjects);
    absorb(m_editor->m_redoObjects);
    for (auto it = m_meta.begin(); it != m_meta.end();) {
        if (!live.count(it->first)) it = m_meta.erase(it);
        else ++it;
    }
}

std::vector<CollabFeedEntry> EditorHistoryTracker::collabFeed(size_t maxCount) const {
    std::vector<CollabFeedEntry> out;
    if (m_collabFeed.empty() || maxCount == 0) return out;
    size_t n = std::min(maxCount, m_collabFeed.size());
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
        out.push_back(m_collabFeed[m_collabFeed.size() - 1 - i]);
    return out;
}

std::vector<LocalEditEntry> EditorHistoryTracker::localEdits(size_t maxCount) const {
    std::vector<LocalEditEntry> out;
    if (m_localEdits.empty() || maxCount == 0) return out;
    size_t n = std::min(maxCount, m_localEdits.size());
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
        out.push_back(m_localEdits[m_localEdits.size() - 1 - i]);
    return out;
}

LocalEditEntry const* EditorHistoryTracker::localEditById(uint64_t id) const {
    for (auto const& e : m_localEdits) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

namespace {

// Try to apply color/HSV fields from a save string onto a live object in-place
// (avoids destroy/recreate when only colors changed).
bool applyColorKeysInPlace(GameObject* go, std::string const& save) {
    if (!go || save.empty()) return false;
    auto keys = parseSaveKeys(save);
    if (keys.empty()) return false;
    bool any = false;

    auto readInt = [&](int k, int& out) -> bool {
        auto it = keys.find(k);
        if (it == keys.end()) return false;
        try {
            out = std::stoi(it->second);
            return true;
        } catch (...) { return false; }
    };
    auto readFloat = [&](int k, float& out) -> bool {
        auto it = keys.find(k);
        if (it == keys.end()) return false;
        try {
            out = std::stof(it->second);
            return true;
        } catch (...) { return false; }
    };

    // GD object keys: 21 base channel, 22 detail channel, 41/42 glow, etc.
    if (go->m_baseColor) {
        int v = 0;
        if (readInt(21, v)) { go->m_baseColor->m_colorID = v; any = true; }
        float op = 0.f;
        if (readFloat(43, op)) { go->m_baseColor->m_opacity = op; any = true; }
    }
    if (go->m_detailColor) {
        int v = 0;
        if (readInt(22, v)) { go->m_detailColor->m_colorID = v; any = true; }
    }

    // HSV on base (key 23) / detail (key 24): "h a s a v a satChecked a absH a absS a absV"
    // Values separated by 'a'. Best-effort parse.
    auto applyHsv = [&](int key, GJSpriteColor* sc) {
        if (!sc) return;
        auto it = keys.find(key);
        if (it == keys.end() || it->second.empty()) return;
        std::vector<std::string> parts;
        std::string token;
        std::stringstream ss(it->second);
        while (std::getline(ss, token, 'a')) parts.push_back(token);
        if (parts.size() < 3) return;
        try {
            sc->m_hsv.h = std::stof(parts[0]);
            sc->m_hsv.s = std::stof(parts[1]);
            sc->m_hsv.v = std::stof(parts[2]);
            if (parts.size() > 3) sc->m_hsv.absoluteSaturation = (parts[3] == "1");
            if (parts.size() > 4) sc->m_hsv.absoluteBrightness = (parts[4] == "1");
            sc->m_usesHSV = true;
            any = true;
        } catch (...) {}
    };
    applyHsv(23, go->m_baseColor);
    applyHsv(24, go->m_detailColor);

    return any;
}

GameObject* findLiveObject(LevelEditorLayer* lel, int uniqueId, int objectId, CCPoint nearPos) {
    if (!lel) return nullptr;

    auto scanArray = [&](CCArray* arr) -> GameObject* {
        if (!arr) return nullptr;
        GameObject* byTypeNear = nullptr;
        float bestDist = 1e12f;
        for (auto* o : CCArrayExt<CCObject*>(arr)) {
            auto* go = typeinfo_cast<GameObject*>(o);
            if (!go || !go->getParent()) continue;
            if (uniqueId != 0 && go->m_uniqueID == uniqueId) return go;
            if (objectId != 0 && go->m_objectID == objectId) {
                float dx = go->getPositionX() - nearPos.x;
                float dy = go->getPositionY() - nearPos.y;
                float d = dx * dx + dy * dy;
                if (d < bestDist) {
                    bestDist = d;
                    byTypeNear = go;
                }
            }
        }
        // Accept proximity match within ~2 blocks (60 units² ≈ 2 tiles).
        if (byTypeNear && bestDist < 90.f * 90.f) return byTypeNear;
        return nullptr;
    };

    if (auto* found = scanArray(lel->m_objects)) return found;
    if (auto* all = lel->getAllObjects()) {
        if (auto* found = scanArray(all)) return found;
    }
    if (lel->m_editorUI) {
        if (auto* found = scanArray(lel->m_editorUI->getSelectedObjects())) return found;
    }
    return nullptr;
}

std::string normalizeObjectSave(std::string save) {
    while (!save.empty() && (save.back() == ';' || save.back() == ' ' || save.back() == '\n')) {
        save.pop_back();
    }
    if (save.empty()) return save;
    // createObjectsFromString / pasteObjects expect object(s) terminated by ';'
    save.push_back(';');
    return save;
}

} // namespace

bool EditorHistoryTracker::restoreLocalEdit(EditorUI* ui, uint64_t localId) {
    auto* live = mutableLocalEditById(localId);
    if (!live) {
        log::warn("[History] restore: entry {} not found", localId);
        return false;
    }
    // Work on a copy of the data we need, but toggle the live entry after success.
    return restoreLocalEdit(ui, *live);
}

bool EditorHistoryTracker::restoreLocalEdit(EditorUI* ui, LocalEditEntry const& entryIn) {
    if (!ui || !m_editor) return false;

    // Resolve live mutable entry so we can update uniqueId / restored flag.
    LocalEditEntry* live = mutableLocalEditById(entryIn.id);
    LocalEditEntry entry = live ? *live : entryIn;

    // Toggle: if already restored once, re-apply the "after" state instead.
    bool applyBefore = !entry.restored;
    std::string const& saveSrc = applyBefore ? entry.beforeSave : entry.afterSave;
    auto const& channels = applyBefore ? entry.beforeChannels : entry.afterChannels;

    if (saveSrc.empty() && channels.empty()) {
        log::warn("[History] restore: nothing to apply for id={} (restored={})",
            entry.id, entry.restored);
        return false;
    }

    GameObject* target = findLiveObject(m_editor, entry.uniqueId, entry.objectId, entry.worldPos);
    log::info("[History] restore id={} kind={} uid={} target={} apply={} save={}B ch={}",
        entry.id, actionKindName(entry.kind), entry.uniqueId,
        target ? target->m_uniqueID : -1,
        applyBefore ? "before" : "after",
        saveSrc.size(), channels.size());

    bool ok = false;

    // --- 1) Restore color CHANNELS + sprite tints (what the user actually sees) ---
    if (!channels.empty()) {
        applyChannels(m_editor, target, channels);
        ok = true;
        log::info("[History] restore: applied {} color snap(s)", channels.size());
    }

    // --- 2) Apply object-level color keys in place (channel id / HSV on obj) --
    if (target && !saveSrc.empty()) {
        if (applyColorKeysInPlace(target, saveSrc)) {
            ok = true;
            log::info("[History] restore: applied object color keys");
        }
    }

    // --- 3) Refresh sprites --------------------------------------------------
    if (ok) {
        if (target) {
            auto* arr = CCArray::create();
            arr->addObject(target);
            m_editor->updateObjectColors(arr);
            if (m_editor->m_objects) {
                m_editor->updateObjectColors(m_editor->m_objects);
            }
            m_propSnap[target->m_uniqueID] = std::string(target->getSaveString(m_editor));
            m_channelSnap[target->m_uniqueID] = captureChannels(m_editor, target);
            ui->deselectAll();
            auto* sel = CCArray::create();
            sel->addObject(target);
            ui->selectObjects(sel, true);

            if (paimon::collab::CollabManager::get().connected()) {
                paimon::collab::CollabManager::get().sendUpdatedObject(target);
            }
        } else if (!channels.empty() && m_editor->m_objects) {
            m_editor->updateObjectColors(m_editor->m_objects);
        }
    }

    // --- 4) Fallback recreate ONLY if we have a full save and nothing worked --
    if (!ok && !saveSrc.empty() && saveSrc.size() >= 20) {
        std::string save = normalizeObjectSave(saveSrc);
        CCPoint keepPos = target ? target->getPosition() : entry.worldPos;

        if (target && target->getParent()) {
            ui->deselectAll();
            ui->deselectObject(target);
            m_editor->removeObject(target, true);
            target = nullptr;
        }

        CCArray* created = m_editor->createObjectsFromString(gd::string(save), true, true);
        if (!created || created->count() == 0) {
            created = ui->pasteObjects(gd::string(save), true, true);
        }
        if (created && created->count() > 0) {
            m_editor->updateObjectColors(created);
            // Re-apply channels AFTER recreate so RGB is correct.
            if (!channels.empty()) applyChannels(m_editor, channels);
            if (m_editor->m_objects) m_editor->updateObjectColors(m_editor->m_objects);

            if (auto* go = typeinfo_cast<GameObject*>(created->objectAtIndex(created->count() - 1))) {
                float dx = go->getPositionX() - keepPos.x;
                float dy = go->getPositionY() - keepPos.y;
                if (dx * dx + dy * dy > 1.f) {
                    go->setPosition(keepPos);
                    m_editor->updateObjectSection(go);
                }
                m_propSnap[go->m_uniqueID] = std::string(go->getSaveString(m_editor));
                m_channelSnap[go->m_uniqueID] = captureChannels(m_editor, go);
                // Keep entry pointing at the new object for the next toggle.
                if (live) live->uniqueId = go->m_uniqueID;
            }
            ui->deselectAll();
            ui->selectObjects(created, true);
            if (paimon::collab::CollabManager::get().connected()) {
                paimon::collab::CollabManager::get().sendCreatedObjects(created);
            }
            ok = true;
            log::info("[History] restore: recreated {} object(s)", created->count());
        }
    } else if (!ok && saveSrc.size() < 20 && channels.empty()) {
        log::warn("[History] restore: save too short ({}B) and no channel snap — cannot restore color",
            saveSrc.size());
    }

    if (ok) {
        if (live) {
            live->restored = applyBefore; // true after applying before → next click uses after
            if (target) live->uniqueId = target->m_uniqueID;
        }
        ++m_undoRevision;
        ++m_localRevision;
    }
    return ok;
}

EditorHistoryTracker::Stats EditorHistoryTracker::stats() const {
    Stats s;
    if (m_editor) {
        if (m_editor->m_undoObjects) s.undoCount = static_cast<int>(m_editor->m_undoObjects->count());
        if (m_editor->m_redoObjects) s.redoCount = static_cast<int>(m_editor->m_redoObjects->count());
    }
    s.collabCount = static_cast<int>(m_collabFeed.size());
    s.localCount = static_cast<int>(m_localEdits.size());
    for (auto const& e : m_collabFeed) {
        if (e.isLocal) ++s.localActions;
        else ++s.peerActions;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Object helpers
// ---------------------------------------------------------------------------

CCArray* EditorHistoryTracker::collectObjects(UndoObject* undo) {
    auto* arr = CCArray::create();
    if (!undo) return arr;
    if (undo->m_objectCopy && undo->m_objectCopy->m_object) {
        arr->addObject(undo->m_objectCopy->m_object);
        return arr;
    }
    if (!undo->m_objects) return arr;
    for (auto* o : CCArrayExt<CCObject*>(undo->m_objects)) {
        if (auto* go = asGameObject(o)) arr->addObject(go);
    }
    return arr;
}

int EditorHistoryTracker::objectCount(UndoObject* undo) {
    if (!undo) return 0;
    if (undo->m_objectCopy && undo->m_objectCopy->m_object) return 1;
    if (undo->m_objects) return static_cast<int>(undo->m_objects->count());
    return 0;
}

int EditorHistoryTracker::firstObjectId(UndoObject* undo) {
    auto* arr = collectObjects(undo);
    if (!arr || arr->count() == 0) return 0;
    if (auto* go = typeinfo_cast<GameObject*>(arr->objectAtIndex(0))) return go->m_objectID;
    return 0;
}

std::string EditorHistoryTracker::buildSaveString(UndoObject* undo, LevelEditorLayer* lel) {
    auto* arr = collectObjects(undo);
    if (!arr || arr->count() == 0) return {};
    std::string out;
    size_t limit = std::min<unsigned int>(arr->count(), 200u);
    for (unsigned int i = 0; i < limit; ++i) {
        auto* go = typeinfo_cast<GameObject*>(arr->objectAtIndex(i));
        if (!go) continue;
        if (lel) out += std::string(go->getSaveString(lel));
        out += ';';
    }
    return out;
}

void EditorHistoryTracker::focusAndSelect(EditorUI* ui, CCArray* objects) {
    if (!ui || !ui->m_editorLayer || !objects || objects->count() == 0) return;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    int n = 0;
    auto* alive = CCArray::create();
    for (auto* o : CCArrayExt<CCObject*>(objects)) {
        auto* go = asGameObject(o);
        if (!go) continue;
        auto p = go->getPosition();
        minX = std::min(minX, p.x); minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
        ++n;
        if (go->getParent()) alive->addObject(go);
    }
    if (n == 0) return;
    focusPoint(ui, {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f});
    if (alive->count() > 0) {
        ui->deselectAll();
        ui->selectObjects(alive, true);
    }
}

void EditorHistoryTracker::focusPoint(EditorUI* ui, CCPoint worldPos) {
    if (!ui || !ui->m_editorLayer || !ui->m_editorLayer->m_objectLayer) return;
    auto* layer = ui->m_editorLayer->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    float s = layer->getScale();
    if (s <= 0.f) s = 1.f;
    layer->setPosition({win.width * 0.5f - worldPos.x * s, win.height * 0.55f - worldPos.y * s});
}

bool EditorHistoryTracker::undoSpecific(EditorUI* ui, UndoObject* undo) {
    if (!ui || !ui->m_editorLayer || !undo) return false;
    auto* stack = ui->m_editorLayer->m_undoObjects;
    if (!stack || stack->count() == 0) return false;
    unsigned int idx = stack->indexOfObject(undo);
    if (idx == UINT_MAX) return false;
    int last = static_cast<int>(stack->count()) - 1;
    if (static_cast<int>(idx) != last) stack->exchangeObjectAtIndex(static_cast<int>(idx), last);
    ui->undoLastAction(ui);
    return true;
}

int EditorHistoryTracker::undoToHere(EditorUI* ui, UndoObject* undo, bool includeSelf) {
    if (!ui || !ui->m_editorLayer || !undo) return 0;
    auto* stack = ui->m_editorLayer->m_undoObjects;
    if (!stack || stack->count() == 0) return 0;
    unsigned int idx = stack->indexOfObject(undo);
    if (idx == UINT_MAX) return 0;
    int last = static_cast<int>(stack->count()) - 1;
    int undos = last - static_cast<int>(idx) + (includeSelf ? 1 : 0);
    undos = std::min(std::max(undos, 0), 500);
    for (int i = 0; i < undos; ++i) {
        if (!stack || stack->count() == 0) break;
        ui->undoLastAction(ui);
    }
    return undos;
}

} // namespace paimon::editorhistory
