#include "PhysicsTriggerEmitter.hpp"

#include "../PhysicsNative.hpp"
#include "../NativeTriggerCatalog.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/AdvancedFollowEditObject.hpp>
#include <Geode/binding/AdvancedFollowTriggerObject.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/KeyframeAnimTriggerObject.hpp>
#include <Geode/binding/KeyframeGameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/SpawnTriggerGameObject.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

constexpr int kKeyframeObject = nativeids::KeyframePoint;
constexpr int kKeyframeTrigger = nativeids::KeyframeAnimation;
constexpr std::size_t kMaxOutputObjects = 8000;
constexpr float kRadiansToDegrees = 57.295779513082320876f;

struct AssignedGroup {
    WeakRef<GameObject> object;
    int group = 0;
};

struct ParentChange {
    WeakRef<GameObject> object;
    int group = 0;
    bool previous = false;
};

struct LastEmission {
    WeakRef<LevelEditorLayer> editor;
    std::vector<WeakRef<GameObject>> objects;
    // Every captured object identifies the authored system. Controlled keeps
    // only the objects that were dynamic in this particular compilation.
    std::vector<WeakRef<GameObject>> sources;
    std::vector<WeakRef<GameObject>> controlled;
    std::vector<AssignedGroup> assignments;
    std::vector<ParentChange> parents;
    std::vector<int> animations;
};

std::vector<LastEmission> g_history;

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

std::vector<int> freeBlocks(LevelEditorLayer* editor, std::size_t count) {
    std::vector<int> blocks;
    blocks.reserve(count);
    gd::unordered_set<int> excluded;
    for (std::size_t i = 0; i < count; ++i) {
        int const block = editor->getNextFreeBlockID(excluded);
        if (block <= 0 || block > 9999) break;
        blocks.push_back(block);
        excluded.insert(block);
    }
    return blocks;
}

std::vector<int> freeControls(LevelEditorLayer* editor, std::size_t count) {
    std::unordered_set<int> used;
    if (editor) {
        if (auto* objects = editor->getAllObjects()) {
            for (auto* item : CCArrayExt<CCObject*>(objects)) {
                auto* effect = typeinfo_cast<EffectGameObject*>(item);
                if (effect && effect->m_controlID > 0) used.insert(effect->m_controlID);
            }
        }
    }
    std::vector<int> controls;
    controls.reserve(count);
    for (int candidate = 1; candidate <= 9999 && controls.size() < count; ++candidate) {
        if (used.insert(candidate).second) controls.push_back(candidate);
    }
    return controls;
}

bool assignGroup(
    LevelEditorLayer* editor,
    std::vector<GameObject*> const& objects,
    int group,
    std::vector<AssignedGroup>& assigned,
    bool recreate = true
) {
    for (auto* object : objects) {
        if (hasGroup(object, group)) continue;
        object->addToGroup(group);
        if (!hasGroup(object, group)) return false;
        assigned.push_back({object, group});
    }
    if (recreate) editor->recreateGroups();
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

void rollbackParents(
    LevelEditorLayer* editor,
    std::vector<ParentChange> const& parents
) {
    for (auto it = parents.rbegin(); it != parents.rend(); ++it) {
        if (!it->previous && editor && it->group > 0) {
            editor->removeGroupParent(it->group);
        }
        if (auto object = it->object.lock()) object->m_hasGroupParent = it->previous;
    }
}

void makeGroupParent(
    LevelEditorLayer* editor,
    GameObject* object,
    int group,
    std::vector<ParentChange>& parents
) {
    if (!object || object->m_hasGroupParent) return;
    parents.push_back({object, group, false});
    if (editor && group > 0) editor->setGroupParent(object, group);
    else object->m_hasGroupParent = true;
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

CCArray* changedSourcesArray(
    std::vector<AssignedGroup> const& assigned,
    std::vector<ParentChange> const& parents
);

void removeEmission(LastEmission const& emission, LevelEditorLayer* editor, bool notifyCollab) {
    if (!editor) return;
    auto& collab = paimon::collab::CollabManager::get();
    for (auto const& weak : emission.objects) {
        auto object = weak.lock();
        if (!object || !object->getParent()) continue;
        if (notifyCollab && collab.connected()) collab.sendDeletedObject(object.data());
        editor->removeObject(object.data(), true);
    }
    rollbackParents(editor, emission.parents);
    rollbackAssignments(editor, emission.assignments);
    editor->recreateGroups();
    for (int animation : emission.animations) editor->updateKeyframeOrder(animation);
    if (notifyCollab && collab.connected()) {
        collab.sendUpdatedObjects(changedSourcesArray(emission.assignments, emission.parents));
    }
    editor->dirtifyTriggers();
}

CCArray* changedSourcesArray(
    std::vector<AssignedGroup> const& assigned,
    std::vector<ParentChange> const& parents
) {
    auto* array = assignmentArray(assigned);
    std::unordered_set<GameObject*> seen;
    for (auto* item : CCArrayExt<CCObject*>(array)) {
        if (auto* object = typeinfo_cast<GameObject*>(item)) seen.insert(object);
    }
    for (auto const& entry : parents) {
        if (auto object = entry.object.lock(); object && seen.insert(object.data()).second) {
            array->addObject(object.data());
        }
    }
    return array;
}

// GD moves a group around a single object, its group parent, so the baked path
// has to trace that object: a body that spins turns around it and not around
// the centre of mass the solver sampled.
GameObject* pivotObject(LevelEditorLayer* editor, ResolvedBody const& body, int group) {
    if (auto* parent = editor->tryGetGroupParent(group)) return parent;
    GameObject* best = nullptr;
    float closest = std::numeric_limits<float>::max();
    for (auto* object : body.objects) {
        if (object->m_hasGroupParent) continue;
        float const dx = object->getPositionX() - body.spec.position.x;
        float const dy = object->getPositionY() - body.spec.position.y;
        if (dx * dx + dy * dy >= closest) continue;
        closest = dx * dx + dy * dy;
        best = object;
    }
    if (best) return best;
    return body.objects.empty() ? nullptr : body.objects.front();
}

NativeBodyInput nativeInput(ResolvedBody const& body) {
    NativeBodyInput input;
    input.spec = body.spec;
    input.settings = body.native;
    input.objectCount = body.objects.size();
    input.objectOffsets.reserve(body.visuals.size());
    for (auto const& visual : body.visuals) input.objectOffsets.push_back(visual.offset);
    return input;
}

int objectIDFor(NativeNodeKind kind) {
    switch (kind) {
        case NativeNodeKind::AdvancedFollow: return nativeids::AdvancedFollow;
        case NativeNodeKind::AdvancedFollowEdit: return nativeids::AdvancedFollowEdit;
        case NativeNodeKind::CollisionBlock: return nativeids::CollisionBlock;
        case NativeNodeKind::CollisionTrigger: return nativeids::Collision;
        case NativeNodeKind::Spawn: return nativeids::Spawn;
        case NativeNodeKind::Rotate: return nativeids::Rotate;
    }
    return 0;
}

bool configureNativeNode(GameObject* raw, NativeNode const& node) {
    if (!raw) return false;
    raw->setPosition({node.position.x, node.position.y});
    for (int group : node.groups) raw->addToGroup(group);

    auto* effect = typeinfo_cast<EffectGameObject*>(raw);
    if (effect) {
        effect->m_isTouchTriggered = false;
        effect->m_isSpawnTriggered = node.spawnTriggered;
        effect->m_isMultiTriggered = node.multiTriggered;
        effect->m_controlID = node.controlID;
    }

    switch (node.kind) {
        case NativeNodeKind::AdvancedFollow: {
            auto* follow = typeinfo_cast<AdvancedFollowTriggerObject*>(raw);
            if (!follow) return false;
            follow->m_targetGroupID = node.targetGroup;
            follow->m_centerGroupID = node.centerGroup;
            follow->m_controlID = node.controlID;
            follow->m_targetPlayer1 = node.followPlayer1;
            follow->m_targetPlayer2 = node.followPlayer2;
            follow->m_followCPP = node.followCenter;
            follow->m_startSpeed = node.startSpeed;
            follow->m_startSpeedVariance = 0.f;
            follow->m_startSpeedReference = 0;
            follow->m_startDirection = node.startDirection;
            follow->m_startDirectionVariance = 0.f;
            follow->m_startDirectionReference = 0;
            follow->m_maxSpeed = node.maxSpeed;
            follow->m_maxSpeedVariance = 0.f;
            follow->m_maxRange = node.maxRange;
            follow->m_maxRangeVariance = 0.f;
            follow->m_acceleration = node.acceleration;
            follow->m_accelerationVariance = 0.f;
            follow->m_friction = node.friction;
            follow->m_frictionVariance = 0.f;
            follow->m_nearAcceleration = 0.f;
            follow->m_nearAccelerationVariance = 0.f;
            follow->m_nearDistance = 0.f;
            follow->m_nearDistanceVariance = 0.f;
            follow->m_nearFriction = 0.f;
            follow->m_nearFrictionVariance = 0.f;
            follow->m_steerForce = node.acceleration;
            follow->m_steerForceVariance = 0.f;
            follow->m_delay = 0.f;
            follow->m_delayVariance = 0.f;
            follow->m_ignoreDisabled = true;
            follow->m_rotateDirection = node.rotateToDirection;
            follow->m_followMode = node.followMode;
            follow->m_startMode = node.startMode;
            follow->m_priority = 0;
            return true;
        }
        case NativeNodeKind::AdvancedFollowEdit: {
            auto* edit = typeinfo_cast<AdvancedFollowEditObject*>(raw);
            if (!edit) return false;
            edit->m_targetGroupID = node.targetGroup;
            edit->m_controlID = node.controlID;
            edit->m_targetControlID = node.controlID > 0;
            edit->m_modX = node.modX;
            edit->m_modXVariance = 0.f;
            edit->m_modY = node.modY;
            edit->m_modYVariance = 0.f;
            edit->m_startSpeed = node.startSpeed;
            edit->m_startSpeedVariance = 0.f;
            edit->m_startDirection = node.startDirection;
            edit->m_startDirectionVariance = 0.f;
            edit->m_startSpeedReference = 0;
            edit->m_startDirectionReference = 0;
            edit->m_followMode = node.followMode;
            edit->m_startMode = node.startMode;
            edit->m_redirectDirection = node.redirectDirection;
            edit->m_xOnly = false;
            edit->m_yOnly = false;
            return true;
        }
        case NativeNodeKind::CollisionBlock: {
            if (!effect) return false;
            effect->m_itemID = node.blockID;
            effect->m_isDynamicBlock = node.dynamicBlock;
            raw->setScaleX(std::max(0.01f, node.size.x / 30.f));
            raw->setScaleY(std::max(0.01f, node.size.y / 30.f));
            return true;
        }
        case NativeNodeKind::CollisionTrigger: {
            if (!effect) return false;
            effect->m_itemID = node.blockA;
            effect->m_itemID2 = node.blockB;
            effect->m_targetGroupID = node.targetGroup;
            effect->m_activateGroup = node.activateGroup;
            effect->m_triggerOnExit = node.triggerOnExit;
            effect->m_targetPlayer1 = node.collisionPeer == CollisionPeer::Player1;
            effect->m_targetPlayer2 = node.collisionPeer == CollisionPeer::Player2;
            return true;
        }
        case NativeNodeKind::Spawn: {
            auto* spawn = typeinfo_cast<SpawnTriggerGameObject*>(raw);
            if (!spawn) return false;
            spawn->m_targetGroupID = node.targetGroup;
            spawn->m_spawnDelay = node.delay;
            spawn->m_delayRange = 0.f;
            spawn->m_spawnOrdered = false;
            return true;
        }
        case NativeNodeKind::Rotate: {
            if (!effect) return false;
            effect->m_targetGroupID = node.targetGroup;
            effect->m_centerGroupID = 0;
            effect->m_duration = node.duration;
            effect->m_rotationDegrees = node.rotationDegrees;
            effect->m_times360 = node.times360;
            effect->m_lockObjectRotation = false;
            effect->m_easingType = EasingType::None;
            effect->m_easingRate = 2.f;
            return true;
        }
    }
    return false;
}

bool sameSources(LastEmission const& emission, std::vector<ResolvedBody> const& bodies) {
    std::unordered_set<GameObject*> current;
    for (auto const& body : bodies) {
        current.insert(body.objects.begin(), body.objects.end());
    }
    if (current.size() != emission.sources.size()) return false;
    for (auto const& source : emission.sources) {
        auto object = source.lock();
        if (!object || !current.erase(object.data())) return false;
    }
    return current.empty();
}

bool controlsCurrentSource(
    LastEmission const& emission,
    std::vector<ResolvedBody> const& bodies
) {
    std::unordered_set<GameObject*> current;
    for (auto const& body : bodies) {
        current.insert(body.objects.begin(), body.objects.end());
    }
    return std::ranges::any_of(emission.controlled, [&](WeakRef<GameObject> const& source) {
        auto object = source.lock();
        return object && current.contains(object.data());
    });
}

std::optional<std::size_t> latestEmissionIndex(LevelEditorLayer* editor) {
    for (std::size_t index = g_history.size(); index > 0; --index) {
        auto current = g_history[index - 1].editor.lock();
        if (current && current.data() == editor) return index - 1;
    }
    return std::nullopt;
}

std::optional<std::size_t> matchingEmissionIndex(
    LevelEditorLayer* editor,
    std::vector<ResolvedBody> const& bodies
) {
    // Prefer an exact workspace match. If bodies were added or their motion
    // roles changed, reusing an object controlled by an older graph still
    // means that graph must be replaced to avoid two systems moving it.
    for (std::size_t index = g_history.size(); index > 0; --index) {
        auto current = g_history[index - 1].editor.lock();
        if (current && current.data() == editor && sameSources(g_history[index - 1], bodies)) {
            return index - 1;
        }
    }
    for (std::size_t index = g_history.size(); index > 0; --index) {
        auto current = g_history[index - 1].editor.lock();
        if (current && current.data() == editor &&
            controlsCurrentSource(g_history[index - 1], bodies)) {
            return index - 1;
        }
    }
    return std::nullopt;
}

} // namespace

Result<EmitReport> emitToEditor(
    EditorUI* ui,
    std::vector<ResolvedBody> const& bodies,
    SimulationTrace const& trace,
    LabConfig const& config
) {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    if (bodies.empty()) return Err("No hay cuerpos para compilar.");

    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        return Err("La sesion Collab esta en modo solo lectura.");
    }

    auto* editor = ui->m_editorLayer;
    std::erase_if(g_history, [](LastEmission const& emission) {
        return !emission.editor.lock();
    });
    std::vector<std::size_t> dynamicBodies;
    std::vector<std::size_t> bakedBodies;
    std::vector<std::size_t> reactiveBodies;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].spec.motion != Motion::Dynamic) continue;
        dynamicBodies.push_back(i);
        if (bodies[i].native.backend == PhysicsBackend::Reactive) reactiveBodies.push_back(i);
        else bakedBodies.push_back(i);
    }
    if (dynamicBodies.empty()) return Err("No hay cuerpos dinamicos para compilar.");
    for (auto index : reactiveBodies) {
        if (bodies[index].native.preset == NativePreset::Explosion &&
            bodies[index].objects.size() > 128) {
            return Err("El preset Explosion admite hasta 128 fragmentos por cuerpo.");
        }
    }
    if (!bakedBodies.empty()) {
        if (trace.exhausted) return Err("La simulacion esta incompleta; vuelve a generar la vista previa.");
        if (trace.frames.size() < 2) return Err("La simulacion no tiene trayectoria para keyframes.");
        float previousTime = -1.f;
        for (auto const& frame : trace.frames) {
            if (!std::isfinite(frame.time) || frame.time <= previousTime) {
                return Err("La trayectoria contiene tiempos invalidos.");
            }
            previousTime = frame.time;
            if (frame.poses.size() != bodies.size()) {
                return Err("La trayectoria no coincide con los cuerpos.");
            }
            for (auto const& pose : frame.poses) {
                if (!std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
                    !std::isfinite(pose.angle)) return Err("La trayectoria contiene posiciones invalidas.");
            }
        }
    }
    std::vector<bool> needsTargetGroup(bodies.size(), false);
    for (auto index : bakedBodies) needsTargetGroup[index] = true;
    for (auto index : reactiveBodies) {
        needsTargetGroup[index] = bodies[index].native.preset != NativePreset::Explosion;
    }
    auto const previousIndex = matchingEmissionIndex(editor, bodies);
    LastEmission const* previous = previousIndex ? &g_history[*previousIndex] : nullptr;
    bool const replacingLast = previous != nullptr;

    std::vector<int> preferredGroups(bodies.size(), 0);
    for (auto index : dynamicBodies) {
        if (!needsTargetGroup[index]) continue;
        preferredGroups[index] = bodies[index].preferredGroup;
        if (!replacingLast || preferredGroups[index] <= 0) continue;
        bool const assignedByLast = std::ranges::any_of(
            previous->assignments,
            [&](AssignedGroup const& entry) { return entry.group == preferredGroups[index]; }
        );
        if (assignedByLast) preferredGroups[index] = 0;
    }

    std::vector<NativeBodyInput> nativeInputs;
    nativeInputs.reserve(bodies.size());
    for (auto const& body : bodies) nativeInputs.push_back(nativeInput(body));
    auto const nativeRequirements = estimateNativeRequirements(
        nativeInputs, config.gravity, config.airDrag
    );

    // Nothing moves once every dynamic body fell asleep, so the bake stops
    // there instead of spending a keyframe per sample on a still scene.
    std::size_t samples = trace.frames.size();
    if (!bakedBodies.empty() && trace.settleTime >= 0.f && samples > 2) {
        auto const settled = std::lower_bound(
            trace.frames.begin(), trace.frames.end(), trace.settleTime,
            [](Frame const& frame, float time) { return frame.time < time; }
        );
        samples = std::clamp<std::size_t>(settled - trace.frames.begin() + 1, 2, samples);
    }
    std::size_t newTargets = 0;
    for (auto index : dynamicBodies) {
        newTargets += needsTargetGroup[index] && preferredGroups[index] <= 0;
    }
    std::size_t const reactiveRootGroups = std::ranges::count_if(
        reactiveBodies,
        [&](std::size_t index) { return needsTargetGroup[index]; }
    );
    std::size_t const nativeExtraGroups = reactiveBodies.empty()
        ? 0 : nativeRequirements.groups - reactiveRootGroups;
    std::size_t const requiredGroups = newTargets + bakedBodies.size() + nativeExtraGroups;
    auto groups = freeGroups(editor, requiredGroups);
    if (groups.size() != requiredGroups) {
        return Err("No hay suficientes Group IDs libres para compilar las fisicas.");
    }
    auto blocks = freeBlocks(editor, reactiveBodies.empty() ? 0 : nativeRequirements.blocks);
    if (blocks.size() != (reactiveBodies.empty() ? 0 : nativeRequirements.blocks)) {
        return Err("No hay suficientes Block IDs libres para los sensores nativos.");
    }
    auto controls = freeControls(editor, reactiveBodies.empty() ? 0 : nativeRequirements.controls);
    if (controls.size() != (reactiveBodies.empty() ? 0 : nativeRequirements.controls)) {
        return Err("No hay suficientes Control IDs libres para Advanced Follow.");
    }

    std::size_t groupCursor = 0;
    std::size_t blockCursor = 0;
    std::size_t controlCursor = 0;
    auto nextGroup = [&]() -> int {
        return groupCursor < groups.size() ? groups[groupCursor++] : 0;
    };
    auto nextBlock = [&]() -> int {
        return blockCursor < blocks.size() ? blocks[blockCursor++] : 0;
    };
    auto nextControl = [&]() -> int {
        return controlCursor < controls.size() ? controls[controlCursor++] : 0;
    };
    std::vector<int> targetGroups(bodies.size(), 0);
    std::vector<int> animGroups(bodies.size(), 0);
    for (auto index : dynamicBodies) {
        if (!needsTargetGroup[index]) continue;
        targetGroups[index] = preferredGroups[index] > 0
            ? preferredGroups[index]
            : nextGroup();
    }
    for (auto index : bakedBodies) animGroups[index] = nextGroup();

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    for (auto const& body : bodies) {
        minX = std::min(minX, body.spec.position.x);
        minY = std::min(minY, body.spec.position.y);
    }
    float const triggerX = minX - 180.f;
    float const triggerY = minY - 60.f;

    NativeLayout nativeLayout;
    nativeLayout.bodies.resize(bodies.size());
    nativeLayout.triggerOrigin = {triggerX, triggerY - 60.f};
    if (!reactiveBodies.empty()) nativeLayout.manifestGroup = nextGroup();
    if (!reactiveBodies.empty() && nativeRequirements.needsStaticWorld) {
        nativeLayout.staticWorldBlockID = nextBlock();
    }
    for (auto index : reactiveBodies) {
        auto& ids = nativeLayout.bodies[index];
        ids.rootGroup = targetGroups[index];
        auto const profile = nativeProfile(
            bodies[index].native, bodies[index].spec, config.gravity, config.airDrag
        );
        if (profile.fragmentObjects) {
            std::size_t const fragments = std::max<std::size_t>(1, bodies[index].objects.size());
            for (std::size_t fragment = 0; fragment < fragments; ++fragment) {
                ids.fragmentGroups.push_back(nextGroup());
                ids.fragmentControlIDs.push_back(nextControl());
            }
        } else {
            ids.controlID = nextControl();
        }
        bool const playerSensors = profile.collideWithPlayer &&
            (bodies[index].native.targetPlayer1 || bodies[index].native.targetPlayer2);
        bool const worldSensors = profile.collideWithWorld &&
            nativeLayout.staticWorldBlockID > 0;
        if (playerSensors || worldSensors) {
            for (std::size_t side = 0; side < 4; ++side) {
                ids.sensorBlockIDs[side] = nextBlock();
            }
        }
        if (playerSensors) {
            for (std::size_t side = 0; side < 4; ++side) {
                ids.playerActionGroups[side] = nextGroup();
            }
        }
        if (worldSensors) {
            for (std::size_t side = 0; side < 4; ++side) {
                ids.worldActionGroups[side] = nextGroup();
            }
        }
        if (profile.gravityImpulse > 0.f) ids.gravityGroup = nextGroup();
        if (profile.useAnchor) {
            ids.anchorGroup = nextGroup();
            ids.sensorBlockIDs[0] = nextBlock();
        }
    }
    if (groupCursor != groups.size() || blockCursor != blocks.size() ||
        controlCursor != controls.size()) {
        return Err("El planificador de IDs nativos produjo una reserva inconsistente.");
    }

    auto nativeGraph = reactiveBodies.empty()
        ? TriggerGraph{}
        : buildNativeTriggerGraph(
            nativeInputs, nativeLayout, config.gravity, config.airDrag
        );
    if (!reactiveBodies.empty()) {
        auto const validation = validateNativeTriggerGraph(nativeGraph, nativeInputs);
        if (!validation.ok()) return Err(validation.errors.front());
        for (auto const& warning : validation.warnings) log::warn("[PhysicsStudio] {}", warning);
    }

    std::size_t const bakedObjects = bakedBodies.size() * (samples + 1);
    std::size_t const maximumObjects = bakedObjects + nativeGraph.nodes.size();
    if (maximumObjects > kMaxOutputObjects) {
        return Err("La compilacion produciria mas de 8000 objetos. Reduce calidad, duracion o cuerpos.");
    }

    std::vector<AssignedGroup> assigned;
    for (auto index : dynamicBodies) {
        if (!needsTargetGroup[index] || preferredGroups[index] > 0) continue;
        if (!assignGroup(editor, bodies[index].objects, targetGroups[index], assigned, false)) {
            rollbackAssignments(editor, assigned);
            return Err("Un cuerpo ya usa el maximo de 10 grupos; no se hizo ningun cambio.");
        }
    }
    for (auto const& binding : nativeGraph.bindings) {
        if (binding.body >= bodies.size()) continue;
        std::vector<GameObject*> objects;
        if (binding.object == kNativeAllObjects) {
            objects = bodies[binding.body].objects;
        } else if (binding.object < bodies[binding.body].objects.size()) {
            objects.push_back(bodies[binding.body].objects[binding.object]);
        }
        if (!assignGroup(editor, objects, binding.group, assigned, false)) {
            rollbackAssignments(editor, assigned);
            return Err("Un fragmento ya usa 10 grupos; no se hizo ningun cambio.");
        }
    }
    editor->recreateGroups();

    std::vector<int> animations;
    animations.reserve(bakedBodies.size());
    EmitReport report;
    report.impacts = trace.impacts;
    report.assignedObjects = assigned.size();
    report.groups = requiredGroups;
    report.reactiveBodies = reactiveBodies.size();

    // Every object goes through the editor's own create path. Rebuilding a
    // keyframe from the save string of a loose probe reached
    // GJEffectManager::getColorSprite with no colour channels behind it, and GD
    // crashed there instead of creating the object.
    auto* created = CCArray::create();
    auto abort = [&](std::string message) -> Result<EmitReport> {
        for (auto* item : CCArrayExt<CCObject*>(created)) {
            if (auto* object = typeinfo_cast<GameObject*>(item)) editor->removeObject(object, true);
        }
        rollbackAssignments(editor, assigned);
        return Err(std::move(message));
    };

    std::size_t triggerSlot = 0;
    std::vector<std::pair<GameObject*, int>> bakedPivots;
    for (auto index : bakedBodies) {
        auto* pivot = pivotObject(editor, bodies[index], targetGroups[index]);
        Vec2 arm;
        if (pivot) {
            arm = {
                pivot->getPositionX() - bodies[index].spec.position.x,
                pivot->getPositionY() - bodies[index].spec.position.y,
            };
            if (bodies[index].objects.size() > 1) {
                bakedPivots.emplace_back(pivot, targetGroups[index]);
            }
        }

        auto* animation = editor->createNewKeyframeAnim();
        if (!animation) return abort("GD no pudo reservar una animacion de keyframes.");
        int const animationID = animation->getTag();
        animations.push_back(animationID);

        bool spins = false;
        for (std::size_t sample = 0; sample < samples; ++sample) {
            auto const& pose = trace.frames[sample].poses[index];
            // Solver angles are counter-clockwise; cocos rotation grows clockwise.
            float const degrees = -pose.angle * kRadiansToDegrees;
            spins = spins || std::abs(degrees) > 0.002f;

            float const cosine = std::cos(pose.angle - bodies[index].spec.angle);
            float const sine = std::sin(pose.angle - bodies[index].spec.angle);
            CCPoint const position{
                pose.position.x + arm.x * cosine - arm.y * sine,
                pose.position.y + arm.x * sine + arm.y * cosine,
            };
            auto* keyframe = typeinfo_cast<KeyframeGameObject*>(
                editor->createObject(kKeyframeObject, position, true)
            );
            if (!keyframe) return abort("Esta version de GD no expone el sistema de keyframes.");
            created->addObject(keyframe);

            // The create path drops the object on the editor grid, so the sampled
            // position goes back over it.
            keyframe->setPosition(position);
            keyframe->setRotation(degrees);
            keyframe->setScaleX(1.f);
            keyframe->setScaleY(1.f);
            if (sample == 0) keyframe->addToGroup(animGroups[index]);
            keyframe->m_keyframeGroup = animationID;
            keyframe->m_keyframeIndex = static_cast<int>(sample);
            keyframe->m_targetGroupID = targetGroups[index];
            // GD stores the duration of the segment LEAVING this keyframe.
            // Keep the actual timestamps, including a shorter final segment.
            keyframe->m_duration = sample + 1 < samples
                ? trace.frames[sample + 1].time - trace.frames[sample].time : 0.f;
            keyframe->m_spawnDelay = 0.f;
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
            // The keyframe format is not documented anywhere, so the first one of
            // a bake is logged to be read back against what GD stored.
            if (report.keyframes == 0) {
                log::info("[PhysicsLab] keyframe: {}", std::string(keyframe->getSaveString(editor)));
            }
            ++report.objects;
            ++report.keyframes;
        }

        CCPoint const triggerPosition{
            triggerX,
            triggerY - static_cast<float>(triggerSlot++) * 30.f,
        };
        auto* animTrigger = typeinfo_cast<KeyframeAnimTriggerObject*>(
            editor->createObject(kKeyframeTrigger, triggerPosition, true)
        );
        if (!animTrigger) return abort("Los bindings no reconocieron el trigger de keyframes.");
        created->addObject(animTrigger);

        animTrigger->setPosition(triggerPosition);
        animTrigger->m_targetGroupID = targetGroups[index];
        animTrigger->m_animationID = animGroups[index];
        // The animation lasts what the baked slice lasted. Left at whatever a fresh
        // trigger carries, the whole fall replayed in a fraction of the time.
        animTrigger->m_duration = trace.frames[samples - 1].time - trace.frames.front().time;
        animTrigger->m_centerGroupID = 0;
        animTrigger->m_easingType = EasingType::None;
        animTrigger->m_easingRate = 2.f;
        animTrigger->m_timeMod = 1.f;
        animTrigger->m_positionXMod = 1.f;
        animTrigger->m_positionYMod = 1.f;
        // A body with no spin keeps whatever rotation it already had in the editor.
        animTrigger->m_rotationMod = spins ? 1.f : 0.f;
        animTrigger->m_scaleXMod = 1.f;
        animTrigger->m_scaleYMod = 1.f;
        animTrigger->m_isMultiTriggered = true;
        if (report.triggers == 0) {
            log::info("[PhysicsLab] trigger: {}", std::string(animTrigger->getSaveString(editor)));
        }
        ++report.objects;
        ++report.triggers;
    }

    bool loggedNative = false;
    for (auto const& node : nativeGraph.nodes) {
        int const objectID = objectIDFor(node.kind);
        if (objectID <= 0) return abort("El grafo contiene un tipo nativo desconocido.");
        auto* object = editor->createObject(
            objectID, {node.position.x, node.position.y}, true
        );
        if (!object || !configureNativeNode(object, node)) {
            if (object) editor->removeObject(object, true);
            return abort(fmt::format(
                "GD 2.2081 no reconocio el objeto nativo ID {}. No se guardo una salida parcial.",
                objectID
            ));
        }
        for (int group : node.groups) {
            if (!hasGroup(object, group)) {
                editor->removeObject(object, true);
                return abort("Un trigger generado no pudo recibir todos sus subgrupos.");
            }
        }
        created->addObject(object);
        ++report.objects;
        if (node.kind == NativeNodeKind::CollisionBlock) ++report.collisionBlocks;
        else {
            ++report.triggers;
            if (node.kind == NativeNodeKind::CollisionTrigger) ++report.collisionTriggers;
            if (node.kind == NativeNodeKind::AdvancedFollow ||
                node.kind == NativeNodeKind::AdvancedFollowEdit) {
                ++report.physicsTriggers;
            }
        }
        if (!loggedNative) {
            log::info("[PhysicsStudio] native: {}", std::string(object->getSaveString(editor)));
            loggedNative = true;
        }
    }
    for (auto index : reactiveBodies) {
        if (bodies[index].native.preset == NativePreset::Explosion) {
            report.fragments += bodies[index].objects.size();
        }
    }

    LastEmission next;
    next.editor = editor;
    next.assignments = assigned;
    next.animations = animations;
    next.objects.reserve(created->count());
    for (auto* item : CCArrayExt<CCObject*>(created)) {
        if (auto* object = typeinfo_cast<GameObject*>(item)) next.objects.emplace_back(object);
    }
    for (auto const& body : bodies) {
        for (auto* object : body.objects) next.sources.emplace_back(object);
    }
    for (auto index : dynamicBodies) {
        for (auto* object : bodies[index].objects) next.controlled.emplace_back(object);
    }

    if (replacingLast) {
        removeEmission(g_history[*previousIndex], editor, true);
        g_history.erase(g_history.begin() + static_cast<std::ptrdiff_t>(*previousIndex));
    }
    for (auto const& [object, group] : bakedPivots) {
        makeGroupParent(editor, object, group, next.parents);
    }
    for (auto const& binding : nativeGraph.bindings) {
        if (!binding.makeGroupParent || binding.body >= bodies.size()) continue;
        GameObject* parent = nullptr;
        if (binding.object == kNativeAllObjects) {
            auto const& objects = bodies[binding.body].objects;
            auto const existing = std::ranges::find_if(objects, [](GameObject* object) {
                return object && object->m_hasGroupParent;
            });
            if (existing != objects.end()) parent = *existing;
            else if (!objects.empty()) parent = objects.front();
        } else if (binding.object < bodies[binding.body].objects.size()) {
            parent = bodies[binding.body].objects[binding.object];
        }
        makeGroupParent(editor, parent, binding.group, next.parents);
    }
    g_history.push_back(std::move(next));
    auto const& stored = g_history.back();

    editor->updateObjectColors(created);
    editor->recreateGroups();
    for (int animation : animations) editor->updateKeyframeOrder(animation);
    editor->refreshKeyframeAnims();
    editor->dirtifyTriggers();
    ui->deselectAll();
    ui->selectObjects(created, false);

    if (collab.connected()) {
        collab.sendUpdatedObjects(changedSourcesArray(stored.assignments, stored.parents));
        collab.sendCreatedObjects(created);
    }
    return Ok(report);
}

Result<std::size_t> removeLastEmission(EditorUI* ui) {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    auto const index = latestEmissionIndex(ui->m_editorLayer);
    if (!index || g_history[*index].objects.empty()) {
        return Err("No hay una salida reciente en este editor.");
    }
    auto const& emission = g_history[*index];
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        return Err("La sesion Collab esta en modo solo lectura.");
    }
    std::size_t count = 0;
    for (auto const& object : emission.objects) {
        if (object.lock()) ++count;
    }
    removeEmission(emission, ui->m_editorLayer, true);
    g_history.erase(g_history.begin() + static_cast<std::ptrdiff_t>(*index));
    return Ok(count);
}

} // namespace paimon::editorphysics
