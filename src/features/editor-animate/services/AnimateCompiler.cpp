#include "AnimateCompiler.hpp"

#include "AnimatePlanner.hpp"
#include "AnimateSession.hpp"
#include "../AnimateText.hpp"
#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <limits>

using namespace geode::prelude;

namespace paimon::animate {

namespace {

bool generated(GameObject* object) {
    if (!object || !object->m_isTrigger) return false;
    switch (object->m_objectID) {
        case triggerids::Alpha:
        case triggerids::Toggle:
        case triggerids::Spawn:
        case triggerids::Stop:
            return true;
    }
    return false;
}

struct Bounds {
    float minX = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    bool valid = false;
};

Bounds boundsOf(LevelEditorLayer* editor, Clip const& clip) {
    Bounds bounds;
    for (int group : frameGroups(clip)) {
        auto* array = editor->getGroup(group);
        if (!array) continue;
        for (auto* object : CCArrayExt<GameObject*>(array)) {
            if (!object || generated(object)) continue;
            bounds.minX = std::min(bounds.minX, object->getPositionX());
            bounds.maxY = std::max(bounds.maxY, object->getPositionY());
            bounds.valid = true;
        }
    }
    return bounds;
}

cocos2d::CCPoint viewCenter(LevelEditorLayer* editor) {
    auto const win = CCDirector::get()->getWinSize();
    if (!editor->m_objectLayer) return {win.width / 2.f, win.height / 2.f};
    return editor->m_objectLayer->convertToNodeSpace(win / 2.f);
}

int reuseOr(int current, AnimateSession& session, std::unordered_set<int>& taken) {
    if (current > 0) return current;
    return session.allocateGroup(taken);
}

} // namespace

std::size_t removeCompiled(LevelEditorLayer* editor, Clip& clip) {
    auto& c = clip.compiled;
    c.triggers = 0;
    c.stale = false;
    if (!editor || c.marker <= 0) return 0;
    auto* array = editor->getGroup(c.marker);
    if (!array || array->count() == 0) return 0;

    std::vector<Ref<GameObject>> doomed;
    for (auto* object : CCArrayExt<GameObject*>(array)) {
        if (generated(object)) doomed.emplace_back(object);
    }
    auto* ui = editor->m_editorUI;
    auto& collab = paimon::collab::CollabManager::get();
    for (auto& object : doomed) {
        if (ui && object->m_isSelected) ui->deselectObject(object);
        if (collab.connected()) collab.sendDeletedObject(object);
        editor->removeObject(object, true);
    }
    editor->dirtifyTriggers();
    return doomed.size();
}

Result<CompileReport> compileClip(LevelEditorLayer* editor, Clip& clip) {
    if (!editor) return Err(tr("The editor is no longer available.", "El editor ya no esta disponible."));
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        return Err(tr("The collab room is read only.", "La sala collab esta en solo lectura."));
    }

    auto const steps = buildSequence(clip);
    if (steps.empty()) return Err(tr("Every frame is skipped.", "Todos los frames estan omitidos."));
    if (frameGroups(clip).empty()) {
        return Err(tr("Draw something in a frame first.", "Primero dibuja algo en un frame."));
    }

    removeCompiled(editor, clip);

    auto& session = AnimateSession::get();
    auto taken = session.reservedGroups();
    auto const& s = clip.settings;
    PlanGroups groups;
    groups.marker = reuseOr(clip.compiled.marker, session, taken);
    groups.chain = reuseOr(clip.compiled.chain, session, taken);
    groups.start = reuseOr(clip.compiled.start, session, taken);
    if (s.controls) {
        groups.stop = reuseOr(clip.compiled.stop, session, taken);
        groups.pause = reuseOr(clip.compiled.pause, session, taken);
        groups.resume = reuseOr(clip.compiled.resume, session, taken);
    }
    auto const events = eventGroupsNeeded(clip, steps);
    for (std::size_t i = 0; i < events; ++i) groups.events.push_back(session.allocateGroup(taken));

    bool const missing = groups.marker <= 0 || groups.chain <= 0 || groups.start <= 0 ||
        (s.controls && (groups.stop <= 0 || groups.pause <= 0 || groups.resume <= 0)) ||
        std::any_of(groups.events.begin(), groups.events.end(), [](int g) { return g <= 0; });
    if (missing) return Err(tr("Not enough free groups left.", "No quedan suficientes grupos libres."));

    auto const bounds = boundsOf(editor, clip);
    auto const center = viewCenter(editor);
    PlanLayout layout;
    layout.gridX = bounds.valid ? bounds.minX : center.x;
    float const top = bounds.valid ? bounds.maxY + 45.f : center.y;
    layout.positionX = s.startX > 0.f ? s.startX : layout.gridX;

    // first pass only counts rows so the grid can sit right above the drawing.
    auto triggers = planTriggers(clip, steps, groups, layout);
    std::size_t const rows = (triggers.size() + kGridColumns - 1) / kGridColumns;
    layout.gridY = top + static_cast<float>(rows > 0 ? rows - 1 : 0) * kGridStep;
    layout.looseY = layout.gridY;
    triggers = planTriggers(clip, steps, groups, layout);
    if (triggers.empty()) return Err(tr("Nothing to compile.", "No hay nada que compilar."));

    auto const payload = serializeTriggers(triggers, s.editorLayer);
    auto* created = editor->createObjectsFromString(payload, true, true);
    if (!created || created->count() != triggers.size()) {
        if (created) {
            for (auto* object : CCArrayExt<GameObject*>(created)) editor->removeObject(object, true);
        }
        return Err(tr("GD could not create the triggers.", "GD no pudo crear los triggers."));
    }
    editor->dirtifyTriggers();
    if (collab.connected()) collab.sendCreatedObjects(created);

    auto& c = clip.compiled;
    c.marker = groups.marker;
    c.chain = groups.chain;
    c.start = groups.start;
    c.stop = groups.stop;
    c.pause = groups.pause;
    c.resume = groups.resume;
    c.triggers = static_cast<int>(triggers.size());
    c.steps = static_cast<int>(steps.size());
    c.stale = false;

    log::info("[Animate] '{}' compilado: {} triggers, {} pasos, grupo inicio {}",
        clip.name, c.triggers, c.steps, c.start);

    CompileReport report;
    report.triggers = c.triggers;
    report.steps = c.steps;
    report.groups = static_cast<int>(groups.events.size()) + (s.controls ? 6 : 3);
    report.duration = sequenceDuration(steps);
    return Ok(report);
}

int compileStale(LevelEditorLayer* editor) {
    auto& session = AnimateSession::get();
    int compiled = 0;
    for (auto& clip : session.doc().clips) {
        if (!clip.settings.compileOnSave) continue;
        if (clip.compiled.present() && !clip.compiled.stale) continue;
        auto result = compileClip(editor, clip);
        if (result) {
            ++compiled;
            continue;
        }
        log::warn("[Animate] '{}' no se compilo al guardar: {}", clip.name, result.unwrapErr());
    }
    if (compiled > 0) session.changed();
    return compiled;
}

} // namespace paimon::animate
