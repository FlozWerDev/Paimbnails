#include "PhysicsTriggerEmitter.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/KeyframeAnimTriggerObject.hpp>
#include <Geode/binding/KeyframeGameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

constexpr int kKeyframeObject = 3032;
constexpr int kKeyframeTrigger = 3033;
constexpr std::size_t kMaxOutputObjects = 8000;
constexpr float kRadiansToDegrees = 57.295779513082320876f;

struct AssignedGroup {
    WeakRef<GameObject> object;
    int group = 0;
};

struct LastEmission {
    WeakRef<LevelEditorLayer> editor;
    std::vector<WeakRef<GameObject>> objects;
    std::vector<WeakRef<GameObject>> sources;
    std::vector<AssignedGroup> assignments;
    std::vector<int> animations;
};

LastEmission g_last;

bool hasGroup(GameObject* object, int group) {
    if (!object || group <= 0) return false;
    for (int i = 0; i < object->m_groupCount; ++i) {
        if (object->getGroupID(i) == group) return true;
    }
    return false;
}

std::vector<int> freeGroups(LevelEditorLayer* editor, std::size_t count) {
    std::vector<int> groups;
    groups.reserve(count);
    gd::unordered_set<int> excluded;
    for (std::size_t i = 0; i < count; ++i) {
        int const group = editor->getNextFreeGroupID(excluded);
        if (group <= 0 || group > 9999) break;
        groups.push_back(group);
        excluded.insert(group);
    }
    return groups;
}

bool prepareProbe(Ref<GameObject>& output, int objectID) {
    output = GameObject::createWithKey(objectID);
    return output && output->m_objectID == objectID;
}

std::string saveObject(GameObject* object, LevelEditorLayer* editor) {
    std::string save(object->getSaveString(editor));
    if (save.empty() || save.back() != ';') save += ';';
    return save;
}

bool assignGroup(
    LevelEditorLayer* editor,
    std::vector<GameObject*> const& objects,
    int group,
    std::vector<AssignedGroup>& assigned
) {
    for (auto* object : objects) {
        if (hasGroup(object, group)) continue;
        object->addToGroup(group);
        if (!hasGroup(object, group)) return false;
        assigned.push_back({object, group});
    }
    editor->recreateGroups();
    return true;
}

void rollbackAssignments(LevelEditorLayer* editor, std::vector<AssignedGroup> const& assigned) {
    for (auto const& entry : assigned) {
        if (auto object = entry.object.lock(); object && hasGroup(object.data(), entry.group)) {
            object->removeFromGroup(entry.group);
        }
    }
    if (editor) editor->recreateGroups();
}

CCArray* assignmentArray(std::vector<AssignedGroup> const& assigned) {
    auto* array = CCArray::create();
    std::unordered_set<GameObject*> seen;
    for (auto const& entry : assigned) {
        if (auto object = entry.object.lock(); object && seen.insert(object.data()).second) {
            array->addObject(object.data());
        }
    }
    return array;
}

void removeEmission(LastEmission const& emission, LevelEditorLayer* editor, bool notifyCollab) {
    if (!editor) return;
    auto& collab = paimon::collab::CollabManager::get();
    for (auto const& weak : emission.objects) {
        auto object = weak.lock();
        if (!object || !object->getParent()) continue;
        if (notifyCollab && collab.connected()) collab.sendDeletedObject(object.data());
        editor->removeObject(object.data(), true);
    }
    rollbackAssignments(editor, emission.assignments);
    for (int animation : emission.animations) editor->updateKeyframeOrder(animation);
    if (notifyCollab && collab.connected()) {
        collab.sendUpdatedObjects(assignmentArray(emission.assignments));
    }
    editor->dirtifyTriggers();
}

bool sameSources(LastEmission const& emission, std::vector<ResolvedBody> const& bodies) {
    std::unordered_set<GameObject*> current;
    for (auto const& body : bodies) {
        if (body.spec.motion != Motion::Dynamic) continue;
        current.insert(body.objects.begin(), body.objects.end());
    }
    if (current.size() != emission.sources.size()) return false;
    for (auto const& source : emission.sources) {
        auto object = source.lock();
        if (!object || !current.erase(object.data())) return false;
    }
    return current.empty();
}

} // namespace

Result<EmitReport> emitToEditor(
    EditorUI* ui,
    std::vector<ResolvedBody> const& bodies,
    SimulationTrace const& trace
) {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    if (bodies.empty() || trace.frames.size() < 2) return Err("La simulacion no tiene trayectoria.");
    for (auto const& frame : trace.frames) {
        if (frame.poses.size() != bodies.size()) return Err("La trayectoria no coincide con los cuerpos.");
    }

    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        return Err("La sesion Collab esta en modo solo lectura.");
    }

    auto* editor = ui->m_editorLayer;
    std::vector<std::size_t> dynamicBodies;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].spec.motion == Motion::Dynamic) dynamicBodies.push_back(i);
    }
    if (dynamicBodies.empty()) return Err("No hay cuerpos dinamicos para hornear.");
    auto previousEditor = g_last.editor.lock();
    bool const replacingLast = previousEditor && previousEditor.data() == editor &&
        sameSources(g_last, bodies);

    std::vector<int> preferredGroups(bodies.size(), 0);
    for (auto index : dynamicBodies) {
        preferredGroups[index] = bodies[index].preferredGroup;
        if (!replacingLast || preferredGroups[index] <= 0) continue;
        bool const assignedByLast = std::ranges::any_of(
            g_last.assignments,
            [&](AssignedGroup const& entry) { return entry.group == preferredGroups[index]; }
        );
        if (assignedByLast) preferredGroups[index] = 0;
    }

    std::size_t const samples = trace.frames.size();
    std::size_t const maximumObjects = dynamicBodies.size() * (samples + 1);
    if (maximumObjects > kMaxOutputObjects) {
        return Err("La calidad y duracion producirian mas de 8000 objetos. Reduce una de las dos.");
    }

    std::size_t newTargets = 0;
    for (auto index : dynamicBodies) newTargets += preferredGroups[index] <= 0;
    std::size_t const requiredGroups = newTargets + dynamicBodies.size();
    auto groups = freeGroups(editor, requiredGroups);
    if (groups.size() != requiredGroups) {
        return Err("No hay suficientes grupos libres para hornear la simulacion.");
    }

    std::size_t groupCursor = 0;
    std::vector<int> targetGroups(bodies.size(), 0);
    std::vector<int> animGroups(bodies.size(), 0);
    for (auto index : dynamicBodies) {
        targetGroups[index] = preferredGroups[index] > 0
            ? preferredGroups[index]
            : groups[groupCursor++];
    }
    for (auto index : dynamicBodies) animGroups[index] = groups[groupCursor++];

    Ref<GameObject> keyframeProbe;
    Ref<GameObject> triggerProbe;
    if (!prepareProbe(keyframeProbe, kKeyframeObject) ||
        !prepareProbe(triggerProbe, kKeyframeTrigger)) {
        return Err("Esta version de GD no expone el sistema de keyframes.");
    }
    auto* keyframe = typeinfo_cast<KeyframeGameObject*>(keyframeProbe.data());
    auto* animTrigger = typeinfo_cast<KeyframeAnimTriggerObject*>(triggerProbe.data());
    if (!keyframe || !animTrigger) {
        return Err("Los bindings no reconocieron los objetos de keyframe.");
    }

    std::vector<AssignedGroup> assigned;
    for (auto index : dynamicBodies) {
        if (preferredGroups[index] > 0) continue;
        if (!assignGroup(editor, bodies[index].objects, targetGroups[index], assigned)) {
            rollbackAssignments(editor, assigned);
            return Err("Un cuerpo ya usa el maximo de 10 grupos; no se hizo ningun cambio.");
        }
    }

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    for (auto const& body : bodies) {
        minX = std::min(minX, body.spec.position.x);
        minY = std::min(minY, body.spec.position.y);
    }
    float const triggerX = minX - 90.f;
    float const triggerY = minY - 60.f;
    // The solver samples on a fixed grid, so every keyframe carries the same slice of
    // the timeline. Uniform slices keep the bake independent of whether GD reads a
    // keyframe's duration as the segment reaching it or the one leaving it.
    float const step = trace.frames.back().time / static_cast<float>(samples - 1);
    std::string payload;
    payload.reserve(maximumObjects * 90);
    std::vector<int> animations;
    animations.reserve(dynamicBodies.size());
    EmitReport report;
    report.impacts = trace.impacts;
    report.assignedObjects = assigned.size();
    report.groups = requiredGroups;

    std::size_t triggerSlot = 0;
    for (auto index : dynamicBodies) {
        auto* animation = editor->createNewKeyframeAnim();
        if (!animation) {
            rollbackAssignments(editor, assigned);
            return Err("GD no pudo reservar una animacion de keyframes.");
        }
        int const animationID = animation->getTag();
        animations.push_back(animationID);

        bool spins = false;
        for (std::size_t sample = 0; sample < samples; ++sample) {
            auto const& pose = trace.frames[sample].poses[index];
            // Solver angles are counter-clockwise; cocos rotation grows clockwise.
            float const degrees = -pose.angle * kRadiansToDegrees;
            spins = spins || std::abs(degrees) > 0.002f;

            keyframe->resetGroups();
            keyframe->addToGroup(animGroups[index]);
            keyframe->setPosition({pose.position.x, pose.position.y});
            keyframe->setRotation(degrees);
            keyframe->m_keyframeGroup = animationID;
            keyframe->m_keyframeIndex = static_cast<int>(sample);
            keyframe->m_targetGroupID = targetGroups[index];
            keyframe->m_duration = step;
            keyframe->m_timeMode = 0;
            keyframe->m_curve = false;
            keyframe->m_closeLoop = false;
            keyframe->m_referenceOnly = false;
            keyframe->m_previewArt = false;
            keyframe->m_proximity = false;
            keyframe->m_direction = 0;
            keyframe->m_revolutions = 0;
            keyframe->m_lineOpacity = 1.f;
            keyframe->m_easingType = EasingType::None;
            keyframe->m_easingRate = 2.f;
            payload += saveObject(keyframe, editor);
            ++report.objects;
            ++report.keyframes;
        }

        animTrigger->resetGroups();
        animTrigger->setPosition({triggerX, triggerY - static_cast<float>(triggerSlot++) * 30.f});
        animTrigger->m_targetGroupID = targetGroups[index];
        animTrigger->m_animationID = animGroups[index];
        animTrigger->m_timeMod = 1.f;
        animTrigger->m_positionXMod = 1.f;
        animTrigger->m_positionYMod = 1.f;
        // A body with no spin keeps whatever rotation it already had in the editor.
        animTrigger->m_rotationMod = spins ? 1.f : 0.f;
        animTrigger->m_scaleXMod = 1.f;
        animTrigger->m_scaleYMod = 1.f;
        animTrigger->m_isMultiTriggered = true;
        payload += saveObject(animTrigger, editor);
        ++report.objects;
        ++report.triggers;
    }

    CCArray* created = editor->createObjectsFromString(payload, false, true);
    if (!created || created->count() != report.objects) {
        if (created) {
            for (auto* item : CCArrayExt<CCObject*>(created)) {
                if (auto* object = typeinfo_cast<GameObject*>(item)) editor->removeObject(object, true);
            }
        }
        rollbackAssignments(editor, assigned);
        return Err("GD no pudo crear todos los objetos; se revirtieron los cambios.");
    }

    LastEmission next;
    next.editor = editor;
    next.assignments = assigned;
    next.animations = animations;
    next.objects.reserve(created->count());
    for (auto* item : CCArrayExt<CCObject*>(created)) {
        if (auto* object = typeinfo_cast<GameObject*>(item)) next.objects.emplace_back(object);
    }
    for (auto index : dynamicBodies) {
        for (auto* object : bodies[index].objects) next.sources.emplace_back(object);
    }

    if (replacingLast) {
        removeEmission(g_last, editor, true);
    }
    g_last = std::move(next);

    editor->updateObjectColors(created);
    editor->recreateGroups();
    for (int animation : animations) editor->updateKeyframeOrder(animation);
    editor->refreshKeyframeAnims();
    editor->dirtifyTriggers();
    ui->deselectAll();
    ui->selectObjects(created, false);

    if (collab.connected()) {
        collab.sendUpdatedObjects(assignmentArray(assigned));
        collab.sendCreatedObjects(created);
    }
    return Ok(report);
}

Result<std::size_t> removeLastEmission(EditorUI* ui) {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    auto editor = g_last.editor.lock();
    if (!editor || editor.data() != ui->m_editorLayer || g_last.objects.empty()) {
        return Err("No hay una salida reciente en este editor.");
    }
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        return Err("La sesion Collab esta en modo solo lectura.");
    }
    std::size_t count = 0;
    for (auto const& object : g_last.objects) {
        if (object.lock()) ++count;
    }
    removeEmission(g_last, ui->m_editorLayer, true);
    g_last = {};
    return Ok(count);
}

} // namespace paimon::editorphysics
