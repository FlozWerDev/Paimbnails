#include "ObjectTimelineStore.hpp"

#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::editorhistory {

namespace {

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

GameObject* findByUid(LevelEditorLayer* lel, int uid) {
    if (!lel || !lel->m_objects) return nullptr;
    for (auto* o : CCArrayExt<CCObject*>(lel->m_objects)) {
        auto* go = typeinfo_cast<GameObject*>(o);
        if (go && go->getParent() && go->m_uniqueID == uid) return go;
    }
    return nullptr;
}

} // namespace

bool historyEnabled() {
    return Mod::get()->getSettingValue<bool>("editor-history-enable");
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

std::unordered_map<int, std::string> parseObjectSaveKeys(std::string const& save) {
    std::unordered_map<int, std::string> out;
    if (save.empty()) return out;
    std::vector<std::string> parts;
    std::string token;
    std::stringstream ss(save);
    while (std::getline(ss, token, ',')) parts.push_back(token);
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        try { out[std::stoi(parts[i])] = parts[i + 1]; } catch (...) {}
    }
    return out;
}

bool classifyTrackedChange(
    std::unordered_map<int, std::string> const& before,
    std::unordered_map<int, std::string> const& after,
    ObjChangeKind& outKind
) {
    bool color = false, groups = false, el = false;

    std::unordered_set<int> keys;
    for (auto const& [k, _] : before) keys.insert(k);
    for (auto const& [k, _] : after) keys.insert(k);

    for (int k : keys) {
        auto ib = before.find(k);
        auto ia = after.find(k);
        std::string vb = ib == before.end() ? "" : ib->second;
        std::string va = ia == after.end() ? "" : ia->second;
        if (vb == va) continue;
        switch (k) {
            // color channel / HSV / glow
            case 21: case 22: case 23: case 24: case 41: case 42: case 43:
                color = true;
                break;
            // group parent string
            case 57:
                groups = true;
                break;
            // editor layer / editor layer 2
            case 20: case 61:
                el = true;
                break;
            default:
                break;
        }
    }

    // Prefer the most specific single category. If several of our tracked
    // keys moved at once, pick in a stable order.
    if (color) { outKind = ObjChangeKind::Color; return true; }
    if (groups) { outKind = ObjChangeKind::Groups; return true; }
    if (el) { outKind = ObjChangeKind::EditorLayer; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

ObjectTimelineStore& ObjectTimelineStore::get() {
    static ObjectTimelineStore s;
    return s;
}

void ObjectTimelineStore::clear() {
    m_editor = nullptr;
    m_lastSave.clear();
    m_lastColors.clear();
    m_nodes.clear();
    m_mutating = false;
    ++m_revision;
}

void ObjectTimelineStore::setEditor(LevelEditorLayer* editor) {
    if (m_editor != editor) {
        clear();
        m_editor = editor;
    }
}

std::string ObjectTimelineStore::saveOf(GameObject* go) const {
    if (!go || !m_editor) return {};
    return std::string(go->getSaveString(m_editor));
}

std::vector<ChannelSnap> ObjectTimelineStore::snapColors(GameObject* go) const {
    std::vector<ChannelSnap> out;
    if (!go) return out;

    if (go->m_colorSprite) {
        ChannelSnap s;
        s.channelId = -1;
        s.color = go->m_colorSprite->getColor();
        s.opacity = go->m_colorSprite->getOpacity() / 255.f;
        out.push_back(s);
    }
    {
        ChannelSnap s;
        s.channelId = -3;
        s.color = go->getColor();
        s.opacity = go->getOpacity() / 255.f;
        out.push_back(s);
    }
    if (m_editor && m_editor->m_effectManager) {
        std::unordered_set<int> ids;
        if (go->m_baseColor && go->m_baseColor->m_colorID > 0)
            ids.insert(go->m_baseColor->m_colorID);
        if (go->m_detailColor && go->m_detailColor->m_colorID > 0)
            ids.insert(go->m_detailColor->m_colorID);
        for (int id : ids) {
            auto* ca = m_editor->m_effectManager->getColorAction(id);
            if (!ca) continue;
            ChannelSnap s;
            s.channelId = id;
            s.color = ca->m_color;
            if (s.color.r == 0 && s.color.g == 0 && s.color.b == 0) s.color = ca->m_toColor;
            s.opacity = ca->m_toOpacity > 0.f ? ca->m_toOpacity : 1.f;
            s.blending = ca->m_blending;
            s.playerColor = ca->m_playerColor;
            s.copyID = ca->m_copyID;
            s.copyHSV = ca->m_copyHSV;
            out.push_back(s);
        }
    }
    return out;
}

void ObjectTimelineStore::pushOrCoalesce(ObjectChangeNode node) {
    node.id = m_nextNodeId++;
    if (node.timeMs <= 0) node.timeMs = nowMs();
    node.canRevertBefore = !node.beforeSave.empty() || !node.beforeChannels.empty();
    node.canRevertAfter = !node.afterSave.empty() || !node.afterChannels.empty();
    node.hasColorPreview = true;
    if (node.label.empty()) node.label = objChangeName(node.kind);

    if (!m_nodes.empty()) {
        auto& last = m_nodes.back();
        if (last.uniqueId == node.uniqueId && last.kind == node.kind &&
            (node.timeMs - last.timeMs) < kCoalesceMs) {
            last.afterSave = std::move(node.afterSave);
            last.afterChannels = std::move(node.afterChannels);
            last.previewAfter = node.previewAfter;
            last.worldPos = node.worldPos;
            last.timeMs = node.timeMs;
            last.canRevertAfter = !last.afterSave.empty() || !last.afterChannels.empty();
            ++m_revision;
            return;
        }
    }

    m_nodes.push_back(std::move(node));
    while (m_nodes.size() > kMaxNodes) m_nodes.pop_front();
    ++m_revision;
}

void ObjectTimelineStore::pollObject(GameObject* go) {
    if (!go || !go->getParent() || !m_editor || m_mutating) return;
    int uid = go->m_uniqueID;
    auto cur = saveOf(go);

    auto it = m_lastSave.find(uid);
    if (it == m_lastSave.end()) {
        m_lastSave[uid] = cur;
        m_lastColors[uid] = snapColors(go);
        return;
    }

    if (it->second == cur) return;

    ObjChangeKind kind = ObjChangeKind::Color;
    bool tracked = classifyTrackedChange(
        parseObjectSaveKeys(it->second), parseObjectSaveKeys(cur), kind
    );
    auto curColors = snapColors(go);

    // Anything that is not Color / Groups / Layers: update baseline, no stack entry.
    if (!tracked) {
        m_lastSave[uid] = cur;
        m_lastColors[uid] = curColors;
        return;
    }

    ObjectChangeNode n;
    n.uniqueId = uid;
    n.objectTypeId = go->m_objectID;
    n.kind = kind;
    n.label = objChangeName(kind);
    n.beforeSave = it->second;
    n.afterSave = cur;
    n.beforeChannels = m_lastColors[uid];
    n.afterChannels = curColors;
    n.previewBefore = previewFromChannels(n.beforeChannels);
    n.previewAfter = previewFromChannels(n.afterChannels);
    n.worldPos = go->getPosition();

    m_lastSave[uid] = cur;
    m_lastColors[uid] = curColors;
    pushOrCoalesce(std::move(n));
}

void ObjectTimelineStore::onObjectTouched(GameObject* go) {
    pollObject(go);
}

void ObjectTimelineStore::tick() {
    if (!m_editor) {
        if (auto* lel = LevelEditorLayer::get()) setEditor(lel);
        if (!m_editor) return;
    }

    if (m_editor->m_editorUI) {
        if (auto* sel = m_editor->m_editorUI->getSelectedObjects()) {
            size_t lim = std::min<unsigned>(sel->count(), 200u);
            for (unsigned i = 0; i < lim; ++i) {
                if (auto* go = typeinfo_cast<GameObject*>(sel->objectAtIndex(i)))
                    pollObject(go);
            }
        }
    }

    static size_t cursor = 0;
    if (m_editor->m_objects && m_editor->m_objects->count() > 0) {
        size_t n = m_editor->m_objects->count();
        size_t slice = std::min<size_t>(60, n);
        for (size_t k = 0; k < slice; ++k) {
            auto* go = typeinfo_cast<GameObject*>(
                m_editor->m_objects->objectAtIndex(static_cast<unsigned>((cursor + k) % n))
            );
            if (go) pollObject(go);
        }
        cursor = (cursor + slice) % n;
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

ObjectChangeNode const* ObjectTimelineStore::nodeById(uint64_t id) const {
    for (auto const& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}

int ObjectTimelineStore::countOfKind(ObjChangeKind kind) const {
    int c = 0;
    for (auto const& n : m_nodes) {
        if (n.kind == kind && n.canRevertBefore) ++c;
    }
    return c;
}

std::vector<ObjectChangeNode> ObjectTimelineStore::recentNodes(int limit) const {
    std::vector<ObjectChangeNode> out;
    if (limit <= 0) return out;
    out.reserve(std::min<size_t>(m_nodes.size(), static_cast<size_t>(limit)));
    for (auto it = m_nodes.rbegin(); it != m_nodes.rend() && static_cast<int>(out.size()) < limit; ++it) {
        if (it->canRevertBefore) out.push_back(*it);
    }
    return out;
}

bool ObjectTimelineStore::undoNode(EditorUI* ui, uint64_t nodeId) {
    if (!ui || m_mutating) return false;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (it->id != nodeId) continue;
        m_mutating = true;
        bool ok = applyState(ui, it->uniqueId, it->beforeSave, it->beforeChannels, it->worldPos);
        m_mutating = false;
        if (ok) {
            m_nodes.erase(it);
            ++m_revision;
        }
        return ok;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Apply / revert
// ---------------------------------------------------------------------------

bool ObjectTimelineStore::applyState(
    EditorUI* ui,
    int uniqueId,
    std::string save,
    std::vector<ChannelSnap> const& colors,
    CCPoint keepPos
) {
    if (!ui || !m_editor) return false;

    GameObject* target = findByUid(m_editor, uniqueId);

    if (!colors.empty() && (save.size() < 24 || save.empty())) {
        if (target) {
            for (auto const& s : colors) {
                if (s.channelId > 0 && m_editor->m_effectManager) {
                    if (auto* ca = m_editor->m_effectManager->getColorAction(s.channelId)) {
                        ca->m_color = s.color;
                        ca->m_fromColor = s.color;
                        ca->m_toColor = s.color;
                        ca->m_fromOpacity = s.opacity;
                        ca->m_toOpacity = s.opacity;
                        ca->m_currentOpacity = s.opacity;
                        ca->m_blending = s.blending;
                        m_editor->m_effectManager->colorActionChanged(ca);
                    }
                } else if (s.channelId == -1 || s.channelId == -3) {
                    target->setObjectColor(s.color);
                    target->setColor(s.color);
                    if (target->m_colorSprite) {
                        target->m_colorSprite->setColor(s.color);
                        target->m_colorSprite->setOpacity(
                            static_cast<GLubyte>(std::clamp(s.opacity, 0.f, 1.f) * 255.f)
                        );
                    }
                }
            }
            if (m_editor->m_objects) m_editor->updateObjectColors(m_editor->m_objects);
            m_lastSave[target->m_uniqueID] = saveOf(target);
            m_lastColors[target->m_uniqueID] = snapColors(target);
            if (paimon::collab::CollabManager::get().connected())
                paimon::collab::CollabManager::get().sendUpdatedObject(target);
            return true;
        }
    }

    if (save.empty()) {
        if (!colors.empty() && m_editor->m_effectManager) {
            for (auto const& s : colors) {
                if (s.channelId <= 0) continue;
                if (auto* ca = m_editor->m_effectManager->getColorAction(s.channelId)) {
                    ca->m_color = s.color;
                    ca->m_fromColor = s.color;
                    ca->m_toColor = s.color;
                    m_editor->m_effectManager->colorActionChanged(ca);
                }
            }
            if (m_editor->m_objects) m_editor->updateObjectColors(m_editor->m_objects);
            return true;
        }
        return false;
    }

    std::string s = save;
    while (!s.empty() && (s.back() == ';' || s.back() == ' ')) s.pop_back();
    s.push_back(';');

    // Create the replacement before deleting the live object. A malformed
    // snapshot must never turn a failed history restore into object loss.
    m_mutating = true;
    CCArray* created = m_editor->createObjectsFromString(gd::string(s), true, true);
    if ((!created || created->count() == 0))
        created = ui->pasteObjects(gd::string(s), true, true);

    if (!created || created->count() == 0) {
        m_mutating = false;
        log::warn("[Undo] applyState failed ({}B)", s.size());
        return false;
    }

    if (target && target->getParent()) {
        ui->deselectAll();
        ui->deselectObject(target);
        m_editor->removeObject(target, true);
        target = nullptr;
    }
    m_mutating = false;

    m_editor->updateObjectColors(created);
    if (auto* go = typeinfo_cast<GameObject*>(created->objectAtIndex(created->count() - 1))) {
        if (!colors.empty()) {
            for (auto const& ch : colors) {
                if (ch.channelId == -1 || ch.channelId == -3) {
                    go->setObjectColor(ch.color);
                    go->setColor(ch.color);
                    if (go->m_colorSprite) go->m_colorSprite->setColor(ch.color);
                }
            }
        }
        float dx = go->getPositionX() - keepPos.x;
        float dy = go->getPositionY() - keepPos.y;
        if (dx * dx + dy * dy > 4.f) {
            go->setPosition(keepPos);
            m_editor->updateObjectSection(go);
        }
        auto const replacementUid = go->m_uniqueID;
        for (auto& node : m_nodes) {
            if (node.uniqueId == uniqueId) node.uniqueId = replacementUid;
        }
        m_lastSave[go->m_uniqueID] = saveOf(go);
        m_lastColors[go->m_uniqueID] = snapColors(go);
    }

    ui->deselectAll();
    ui->selectObjects(created, true);
    if (paimon::collab::CollabManager::get().connected())
        paimon::collab::CollabManager::get().sendCreatedObjects(created);
    return true;
}

bool ObjectTimelineStore::revertObjectToNode(EditorUI* ui, uint64_t nodeId, bool toBefore) {
    if (!ui || !m_editor) return false;

    ObjectChangeNode node;
    {
        auto const* n = nodeById(nodeId);
        if (!n) {
            log::warn("[Undo] revert: unknown node {}", nodeId);
            return false;
        }
        node = *n;
    }

    std::string save = toBefore ? node.beforeSave : node.afterSave;
    auto const& colors = toBefore ? node.beforeChannels : node.afterChannels;

    if (toBefore && !node.canRevertBefore) {
        log::warn("[Undo] revert: no BEFORE snapshot for node {}", nodeId);
        return false;
    }
    if (!toBefore && !node.canRevertAfter) {
        log::warn("[Undo] revert: no AFTER snapshot for node {}", nodeId);
        return false;
    }
    if (save.empty() && colors.empty()) {
        log::warn("[Undo] revert: empty snapshot node={}", nodeId);
        return false;
    }

    GameObject* live = findByUid(m_editor, node.uniqueId);
    CCPoint keep = live ? live->getPosition() : node.worldPos;

    bool ok = applyState(ui, node.uniqueId, save, colors, keep);
    if (ok) ++m_revision;
    log::info("[Undo] revert node={} {} ok={}", nodeId, toBefore ? "BEFORE" : "AFTER", ok);
    return ok;
}

bool ObjectTimelineStore::undoLastOfKind(EditorUI* ui, ObjChangeKind kind) {
    if (!ui || !m_editor) return false;

    // Newest first
    for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it) {
        if (it->kind != kind || !it->canRevertBefore) continue;
        uint64_t id = it->id;
        bool ok = revertObjectToNode(ui, id, true);
        if (ok) {
            // erase via reverse iterator base
            m_nodes.erase(std::next(it).base());
            ++m_revision;
        }
        return ok;
    }
    return false;
}

} // namespace paimon::editorhistory
