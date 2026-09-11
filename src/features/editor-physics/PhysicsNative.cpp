#include "PhysicsNative.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <unordered_set>
#include <utility>

namespace paimon::editorphysics {

namespace {

constexpr float kRadiansToDegrees = 57.295779513082320876f;
constexpr float kMinLoopDelay = 0.02f;
constexpr std::size_t kMaxNativeObjects = 8000;

float clampStrength(float value) {
    return std::clamp(value, 0.25f, 3.f);
}

Vec2 rotate(Vec2 value, float angle) {
    float const c = std::cos(angle);
    float const s = std::sin(angle);
    return {value.x * c - value.y * s, value.x * s + value.y * c};
}

float directionOf(Vec2 velocity) {
    if (std::abs(velocity.x) < 0.0001f && std::abs(velocity.y) < 0.0001f) return 0.f;
    // Advanced Follow uses 0 = up, 90 = right, 180 = down, 270 = left.
    float degrees = std::atan2(velocity.x, velocity.y) * kRadiansToDegrees;
    if (degrees < 0.f) degrees += 360.f;
    return degrees;
}

float lengthOf(Vec2 velocity) {
    return std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
}

std::array<float, 4> sideDirections() {
    // A hit on the left pushes right, a hit on the right pushes left, etc.
    return {90.f, 270.f, 0.f, 180.f};
}

void addManifestGroup(NativeNode& node, int manifestGroup) {
    if (manifestGroup > 0) node.groups.push_back(manifestGroup);
}

Vec2 triggerPosition(NativeLayout const& layout, std::size_t& slot) {
    // Six columns avoid producing a several-screen-tall trigger stack for a
    // body with all four player and world sensors enabled.
    std::size_t const column = slot % 6;
    std::size_t const row = slot / 6;
    ++slot;
    return {
        layout.triggerOrigin.x + static_cast<float>(column) * 30.f,
        layout.triggerOrigin.y - static_cast<float>(row) * 30.f,
    };
}

void addNode(TriggerGraph& graph, NativeLayout const& layout, NativeNode node) {
    // Position-triggered nodes must start on the same player crossing. The
    // display grid is only safe for nodes activated through Spawn/Collision.
    if (node.kind != NativeNodeKind::CollisionBlock && !node.spawnTriggered) {
        node.position.x = layout.triggerOrigin.x;
    }
    addManifestGroup(node, layout.manifestGroup);
    graph.nodes.push_back(std::move(node));
}

bool anyStaticFixtures(std::vector<NativeBodyInput> const& bodies) {
    return std::ranges::any_of(bodies, [](NativeBodyInput const& body) {
        return body.spec.motion == Motion::Static && !body.spec.fixtures.empty();
    });
}

bool usesReactive(NativeBodyInput const& body) {
    return body.spec.motion == Motion::Dynamic &&
        body.settings.backend == PhysicsBackend::Reactive;
}

bool validID(int id) {
    return id > 0 && id <= 9999;
}

} // namespace

NativeBounds nativeBounds(BodySpec const& body) {
    NativeBounds result;
    auto include = [&](float minX, float maxX, float minY, float maxY) {
        if (!result.valid) {
            result = {minX, maxX, minY, maxY, true};
            return;
        }
        result.minX = std::min(result.minX, minX);
        result.maxX = std::max(result.maxX, maxX);
        result.minY = std::min(result.minY, minY);
        result.maxY = std::max(result.maxY, maxY);
    };

    for (auto const& fixture : body.fixtures) {
        Vec2 const center = rotate(fixture.offset, body.angle);
        float halfX = fixture.radius > 0.f ? fixture.radius : fixture.halfSize.x;
        float halfY = fixture.radius > 0.f ? fixture.radius : fixture.halfSize.y;
        if (fixture.radius <= 0.f) {
            float const c = std::abs(std::cos(body.angle));
            float const s = std::abs(std::sin(body.angle));
            float const rotatedX = c * halfX + s * halfY;
            float const rotatedY = s * halfX + c * halfY;
            halfX = rotatedX;
            halfY = rotatedY;
        }
        include(
            body.position.x + center.x - halfX,
            body.position.x + center.x + halfX,
            body.position.y + center.y - halfY,
            body.position.y + center.y + halfY
        );
    }
    if (!result.valid) {
        include(
            body.position.x - 15.f, body.position.x + 15.f,
            body.position.y - 15.f, body.position.y + 15.f
        );
    }
    return result;
}

char const* backendName(PhysicsBackend backend) {
    return backend == PhysicsBackend::Reactive ? "triggers" : "keyframes";
}

char const* presetName(NativePreset preset) {
    switch (preset) {
        case NativePreset::Pushable: return "empujable";
        case NativePreset::Bouncy: return "rebotador";
        case NativePreset::Heavy: return "pesado";
        case NativePreset::Floating: return "flotante";
        case NativePreset::Magnet: return "iman";
        case NativePreset::Pendulum: return "pendulo";
        case NativePreset::Explosion: return "explosion";
    }
    return "empujable";
}

NativePreset cyclePreset(NativePreset preset, int direction) {
    constexpr int count = static_cast<int>(NativePreset::Explosion) + 1;
    int value = static_cast<int>(preset) + (direction < 0 ? -1 : 1);
    if (value < 0) value = count - 1;
    if (value >= count) value = 0;
    return static_cast<NativePreset>(value);
}

NativeProfile nativeProfile(
    NativeBodySettings const& rawSettings,
    BodySpec const& body,
    float worldGravity,
    float airDrag
) {
    float const strength = clampStrength(rawSettings.strength);
    float const gravity = std::abs(worldGravity * body.gravityScale);
    // Mass changes how strongly contacts alter velocity. Square root keeps
    // very large compound bodies usable while still making them feel heavier.
    float const massResponse = 1.f / std::clamp(
        std::sqrt(std::max(0.1f, body.mass)), 0.5f, 4.f
    );
    NativeProfile profile;
    profile.gravityImpulse =
        std::clamp(gravity * profile.tick / kPixelsPerSpeedUnit, 0.f, 8.f) * strength;
    profile.pushImpulse = 28.f * strength * massResponse;
    profile.bounceImpulse = 3.f * strength;
    profile.explosionImpulse = 65.f * strength * massResponse;
    profile.maxSpeed = 110.f * strength;
    profile.friction = std::clamp(
        body.friction * 0.35f + std::max(0.f, airDrag) * 0.5f,
        0.f, 1.f
    );
    profile.collideWithPlayer = true;
    profile.collideWithWorld = true;

    switch (rawSettings.preset) {
        case NativePreset::Pushable:
            break;
        case NativePreset::Bouncy:
            profile.pushImpulse = 38.f * strength * massResponse;
            profile.bounceImpulse = 6.f * strength;
            profile.maxSpeed = 145.f * strength;
            profile.friction *= 0.45f;
            profile.rotateToDirection = true;
            break;
        case NativePreset::Heavy:
            profile.gravityImpulse *= 1.45f;
            profile.pushImpulse = 13.f * strength * massResponse;
            profile.bounceImpulse *= 0.3f;
            profile.maxSpeed = 65.f * strength;
            profile.friction = std::clamp(profile.friction * 1.8f + 0.2f, 0.f, 1.f);
            break;
        case NativePreset::Floating:
            profile.gravityImpulse *= 0.12f;
            profile.pushImpulse = 18.f * strength * massResponse;
            profile.bounceImpulse = 2.f * strength;
            profile.maxSpeed = 48.f * strength;
            profile.friction *= 0.2f;
            break;
        case NativePreset::Magnet:
            profile.gravityImpulse = 0.f;
            profile.pushImpulse = 0.f;
            profile.bounceImpulse = 0.f;
            profile.maxSpeed = 95.f * strength;
            profile.acceleration = 18.f * strength;
            profile.friction = 0.08f;
            profile.collideWithPlayer = false;
            profile.collideWithWorld = false;
            profile.followPlayer = true;
            profile.rotateToDirection = true;
            break;
        case NativePreset::Pendulum:
            profile.gravityImpulse = 0.f;
            profile.pushImpulse = 0.f;
            profile.bounceImpulse = 0.f;
            profile.maxSpeed = 75.f * strength;
            profile.acceleration = 15.f * strength;
            profile.friction = 0.035f;
            profile.collideWithPlayer = false;
            profile.collideWithWorld = false;
            profile.useAnchor = true;
            profile.rotateToDirection = true;
            break;
        case NativePreset::Explosion:
            profile.pushImpulse = 0.f;
            profile.bounceImpulse = 0.f;
            profile.maxSpeed = 180.f * strength;
            profile.friction *= 0.25f;
            profile.collideWithPlayer = false;
            profile.collideWithWorld = false;
            profile.fragmentObjects = true;
            profile.rotateToDirection = true;
            break;
    }
    return profile;
}

NativeRequirements estimateNativeRequirements(
    std::vector<NativeBodyInput> const& bodies,
    float worldGravity,
    float airDrag
) {
    NativeRequirements result;
    bool const hasWorld = anyStaticFixtures(bodies);
    for (auto const& body : bodies) {
        if (!usesReactive(body)) continue;
        auto const profile = nativeProfile(body.settings, body.spec, worldGravity, airDrag);
        std::size_t const motionChannels = profile.fragmentObjects
            ? std::max<std::size_t>(1, body.objectCount)
            : 1;
        if (profile.fragmentObjects) {
            result.groups += motionChannels;
            result.controls += motionChannels;
            result.estimatedObjects += motionChannels;
        } else {
            ++result.groups; // root group; an existing exact group may replace this later
            ++result.controls;
            ++result.estimatedObjects; // Advanced Follow
        }
        bool const playerSensors = profile.collideWithPlayer &&
            (body.settings.targetPlayer1 || body.settings.targetPlayer2);
        if (playerSensors) {
            result.groups += 4;
            result.blocks += 4;
            // 4 sensors, 4 edits and up to 8 player collision registrations.
            result.estimatedObjects += 8;
            if (body.settings.targetPlayer1) result.estimatedObjects += 4;
            if (body.settings.targetPlayer2) result.estimatedObjects += 4;
        }
        if (profile.collideWithWorld && hasWorld) {
            result.groups += 4;
            if (!playerSensors) result.blocks += 4;
            result.estimatedObjects += playerSensors ? 8 : 12;
            result.needsStaticWorld = true;
        }
        if (profile.gravityImpulse > 0.f) {
            ++result.groups;
            // One additive velocity edit per Advanced Follow channel, plus the
            // recursive Spawn and its non-spawn-triggered starter.
            result.estimatedObjects += motionChannels + 2;
        }
        if (profile.useAnchor) {
            ++result.groups;
            ++result.blocks;
            ++result.estimatedObjects;
        }
        if (std::abs(body.spec.angularVelocity) > 0.001f && !profile.rotateToDirection) {
            ++result.estimatedObjects;
        }
    }
    if (result.needsStaticWorld) {
        ++result.blocks;
        for (auto const& body : bodies) {
            if (body.spec.motion == Motion::Static) {
                result.estimatedObjects += std::max<std::size_t>(1, body.spec.fixtures.size());
            }
        }
    }
    return result;
}

TriggerGraph buildNativeTriggerGraph(
    std::vector<NativeBodyInput> const& bodies,
    NativeLayout const& layout,
    float worldGravity,
    float airDrag
) {
    TriggerGraph graph;
    if (layout.bodies.size() != bodies.size()) return graph;
    std::size_t triggerSlot = 0;

    // All fixed fixtures share one Block ID. GD permits several Collision
    // Blocks with the same ID, which keeps every dynamic body at four world
    // collision registrations instead of four per static fixture.
    if (layout.staticWorldBlockID > 0) {
        for (auto const& body : bodies) {
            if (body.spec.motion != Motion::Static) continue;
            if (body.spec.fixtures.empty()) {
                NativeNode block;
                block.kind = NativeNodeKind::CollisionBlock;
                block.position = body.spec.position;
                block.blockID = layout.staticWorldBlockID;
                block.dynamicBlock = false;
                addNode(graph, layout, std::move(block));
                continue;
            }
            for (auto const& fixture : body.spec.fixtures) {
                Vec2 const offset = rotate(fixture.offset, body.spec.angle);
                NativeNode block;
                block.kind = NativeNodeKind::CollisionBlock;
                block.position = {
                    body.spec.position.x + offset.x,
                    body.spec.position.y + offset.y,
                };
                float const width = fixture.radius > 0.f
                    ? fixture.radius * 2.f : fixture.halfSize.x * 2.f;
                float const height = fixture.radius > 0.f
                    ? fixture.radius * 2.f : fixture.halfSize.y * 2.f;
                float const c = std::abs(std::cos(body.spec.angle));
                float const s = std::abs(std::sin(body.spec.angle));
                block.size = {
                    std::max(4.f, c * width + s * height),
                    std::max(4.f, s * width + c * height),
                };
                block.blockID = layout.staticWorldBlockID;
                block.dynamicBlock = false;
                addNode(graph, layout, std::move(block));
            }
        }
    }

    auto const directions = sideDirections();
    for (std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex) {
        auto const& body = bodies[bodyIndex];
        auto const& ids = layout.bodies[bodyIndex];
        if (!usesReactive(body)) continue;
        auto const profile = nativeProfile(body.settings, body.spec, worldGravity, airDrag);

        if (profile.fragmentObjects) {
            std::size_t const fragments = std::min(body.objectCount, ids.fragmentGroups.size());
            for (std::size_t fragment = 0; fragment < fragments; ++fragment) {
                graph.bindings.push_back({bodyIndex, fragment, ids.fragmentGroups[fragment], false});
                Vec2 offset;
                if (fragment < body.objectOffsets.size()) offset = body.objectOffsets[fragment];
                if (std::abs(offset.x) < 0.01f && std::abs(offset.y) < 0.01f) {
                    float const angle = static_cast<float>(fragment) *
                        (2.f * std::numbers::pi_v<float> / static_cast<float>(std::max<std::size_t>(1, fragments)));
                    offset = {std::cos(angle), std::sin(angle)};
                }
                NativeNode follow;
                follow.kind = NativeNodeKind::AdvancedFollow;
                follow.position = triggerPosition(layout, triggerSlot);
                follow.targetGroup = ids.fragmentGroups[fragment];
                follow.controlID = fragment < ids.fragmentControlIDs.size()
                    ? ids.fragmentControlIDs[fragment] : 0;
                follow.followCenter = true;
                follow.startSpeed = profile.explosionImpulse;
                follow.startDirection = directionOf(offset);
                follow.maxSpeed = profile.maxSpeed;
                follow.maxRange = 0.f;
                follow.friction = profile.friction;
                follow.rotateToDirection = profile.rotateToDirection;
                follow.followMode = 1;
                follow.startMode = 1;
                follow.multiTriggered = true;
                addNode(graph, layout, std::move(follow));
            }
        } else {
            graph.bindings.push_back({
                bodyIndex, kNativeAllObjects, ids.rootGroup, body.objectCount > 1
            });
            NativeNode follow;
            follow.kind = NativeNodeKind::AdvancedFollow;
            follow.position = triggerPosition(layout, triggerSlot);
            follow.targetGroup = ids.rootGroup;
            follow.controlID = ids.controlID;
            follow.followCenter = !profile.followPlayer && !profile.useAnchor;
            follow.followPlayer1 = profile.followPlayer && body.settings.targetPlayer1;
            follow.followPlayer2 = profile.followPlayer && !follow.followPlayer1 &&
                body.settings.targetPlayer2;
            follow.centerGroup = profile.useAnchor ? ids.anchorGroup : 0;
            follow.startSpeed = lengthOf(body.spec.velocity) / kPixelsPerSpeedUnit;
            follow.startDirection = directionOf(body.spec.velocity);
            follow.maxSpeed = profile.maxSpeed;
            follow.maxRange = profile.useAnchor ? profile.anchorDistance : 0.f;
            follow.acceleration = profile.acceleration;
            follow.friction = profile.friction;
            follow.rotateToDirection = profile.rotateToDirection;
            follow.followMode = 1;
            follow.startMode = 1;
            follow.multiTriggered = true;
            addNode(graph, layout, std::move(follow));
        }

        if (profile.useAnchor) {
            NativeNode anchor;
            anchor.kind = NativeNodeKind::CollisionBlock;
            anchor.position = {
                body.spec.position.x,
                body.spec.position.y + profile.anchorDistance,
            };
            anchor.size = {4.f, 4.f};
            anchor.blockID = ids.sensorBlockIDs[0];
            anchor.groups.push_back(ids.anchorGroup);
            addNode(graph, layout, std::move(anchor));
        }

        bool const playerSensors = profile.collideWithPlayer &&
            (body.settings.targetPlayer1 || body.settings.targetPlayer2);
        bool const worldSensors = profile.collideWithWorld && layout.staticWorldBlockID > 0;
        if (playerSensors || worldSensors) {
            NativeBounds const bounds = nativeBounds(body.spec);
            float const padding = std::clamp(body.settings.sensorPadding, 2.f, 30.f);
            float const width = std::max(6.f, bounds.maxX - bounds.minX);
            float const height = std::max(6.f, bounds.maxY - bounds.minY);
            float const centerX = (bounds.minX + bounds.maxX) * 0.5f;
            float const centerY = (bounds.minY + bounds.maxY) * 0.5f;
            // The sensors straddle the body's own edge instead of hanging off
            // it: hung outside, the whole padding became the gap the body kept
            // between itself and the floor it was supposed to rest on.
            std::array<Vec2, 4> positions{{
                {bounds.minX, centerY},
                {bounds.maxX, centerY},
                {centerX, bounds.minY},
                {centerX, bounds.maxY},
            }};
            std::array<Vec2, 4> sizes{{
                {padding, std::max(4.f, height - padding)},
                {padding, std::max(4.f, height - padding)},
                {std::max(4.f, width - padding), padding},
                {std::max(4.f, width - padding), padding},
            }};

            for (std::size_t side = 0; side < 4; ++side) {
                NativeNode sensor;
                sensor.kind = NativeNodeKind::CollisionBlock;
                sensor.position = positions[side];
                sensor.size = sizes[side];
                sensor.groups.push_back(ids.rootGroup);
                sensor.blockID = ids.sensorBlockIDs[side];
                sensor.dynamicBlock = true;
                addNode(graph, layout, std::move(sensor));

                if (playerSensors) {
                    NativeNode edit;
                    edit.kind = NativeNodeKind::AdvancedFollowEdit;
                    edit.position = triggerPosition(layout, triggerSlot);
                    edit.groups.push_back(ids.playerActionGroups[side]);
                    edit.targetGroup = ids.rootGroup;
                    edit.controlID = ids.controlID;
                    edit.startSpeed = profile.pushImpulse;
                    edit.startDirection = directions[side];
                    edit.modX = 1.f;
                    edit.modY = 1.f;
                    edit.startMode = 2;
                    edit.spawnTriggered = true;
                    edit.multiTriggered = true;
                    addNode(graph, layout, std::move(edit));

                    auto addPlayerCollision = [&](CollisionPeer peer) {
                        NativeNode collision;
                        collision.kind = NativeNodeKind::CollisionTrigger;
                        collision.position = triggerPosition(layout, triggerSlot);
                        collision.blockA = ids.sensorBlockIDs[side];
                        collision.collisionPeer = peer;
                        collision.targetGroup = ids.playerActionGroups[side];
                        collision.activateGroup = true;
                        collision.multiTriggered = true;
                        addNode(graph, layout, std::move(collision));
                    };
                    if (body.settings.targetPlayer1) addPlayerCollision(CollisionPeer::Player1);
                    if (body.settings.targetPlayer2) addPlayerCollision(CollisionPeer::Player2);
                }

                if (worldSensors) {
                    NativeNode edit;
                    edit.kind = NativeNodeKind::AdvancedFollowEdit;
                    edit.position = triggerPosition(layout, triggerSlot);
                    edit.groups.push_back(ids.worldActionGroups[side]);
                    edit.targetGroup = ids.rootGroup;
                    edit.controlID = ids.controlID;
                    edit.startSpeed = profile.bounceImpulse;
                    edit.startDirection = directions[side];
                    edit.modX = (side == 0 || side == 1) ? -body.spec.restitution : 1.f;
                    edit.modY = (side == 2 || side == 3) ? -body.spec.restitution : 1.f;
                    edit.redirectDirection = false;
                    edit.startMode = 2;
                    edit.spawnTriggered = true;
                    edit.multiTriggered = true;
                    addNode(graph, layout, std::move(edit));

                    NativeNode collision;
                    collision.kind = NativeNodeKind::CollisionTrigger;
                    collision.position = triggerPosition(layout, triggerSlot);
                    collision.blockA = ids.sensorBlockIDs[side];
                    collision.blockB = layout.staticWorldBlockID;
                    collision.collisionPeer = CollisionPeer::Block;
                    collision.targetGroup = ids.worldActionGroups[side];
                    collision.activateGroup = true;
                    collision.multiTriggered = true;
                    addNode(graph, layout, std::move(collision));
                }
            }
        }

        if (profile.gravityImpulse > 0.f) {
            auto addGravity = [&](int targetGroup, int controlID) {
                NativeNode gravity;
                gravity.kind = NativeNodeKind::AdvancedFollowEdit;
                gravity.position = triggerPosition(layout, triggerSlot);
                gravity.groups.push_back(ids.gravityGroup);
                gravity.targetGroup = targetGroup;
                gravity.controlID = controlID;
                gravity.startSpeed = profile.gravityImpulse;
                gravity.startDirection = worldGravity * body.spec.gravityScale < 0.f
                    ? 180.f : 0.f;
                gravity.modX = 1.f;
                gravity.modY = 1.f;
                gravity.startMode = 2;
                gravity.spawnTriggered = true;
                gravity.multiTriggered = true;
                addNode(graph, layout, std::move(gravity));
            };
            if (profile.fragmentObjects) {
                std::size_t const fragments = std::min(
                    ids.fragmentGroups.size(), ids.fragmentControlIDs.size()
                );
                for (std::size_t fragment = 0; fragment < fragments; ++fragment) {
                    addGravity(
                        ids.fragmentGroups[fragment], ids.fragmentControlIDs[fragment]
                    );
                }
            } else {
                addGravity(ids.rootGroup, ids.controlID);
            }

            NativeNode loop;
            loop.kind = NativeNodeKind::Spawn;
            loop.position = triggerPosition(layout, triggerSlot);
            loop.groups.push_back(ids.gravityGroup);
            loop.targetGroup = ids.gravityGroup;
            loop.delay = profile.tick;
            loop.spawnTriggered = true;
            loop.multiTriggered = true;
            addNode(graph, layout, std::move(loop));

            NativeNode starter;
            starter.kind = NativeNodeKind::Spawn;
            starter.position = triggerPosition(layout, triggerSlot);
            starter.targetGroup = ids.gravityGroup;
            starter.delay = profile.tick;
            starter.multiTriggered = true;
            addNode(graph, layout, std::move(starter));
        }

        if (std::abs(body.spec.angularVelocity) > 0.001f && !profile.rotateToDirection) {
            constexpr float duration = 600.f;
            // Solver angles grow counter-clockwise; GD/Cocos rotations grow clockwise.
            float const rotations = -body.spec.angularVelocity * duration /
                (2.f * std::numbers::pi_v<float>);
            NativeNode rotateNode;
            rotateNode.kind = NativeNodeKind::Rotate;
            rotateNode.position = triggerPosition(layout, triggerSlot);
            rotateNode.targetGroup = ids.rootGroup;
            rotateNode.duration = duration;
            rotateNode.times360 = static_cast<int>(std::trunc(rotations));
            rotateNode.rotationDegrees = (rotations - static_cast<float>(rotateNode.times360)) * 360.f;
            rotateNode.multiTriggered = true;
            addNode(graph, layout, std::move(rotateNode));
        }
    }
    return graph;
}

GraphValidation validateNativeTriggerGraph(
    TriggerGraph const& graph,
    std::vector<NativeBodyInput> const& bodies
) {
    GraphValidation result;
    if (graph.nodes.empty()) {
        result.errors.emplace_back("El grafo reactivo no contiene objetos.");
        return result;
    }
    if (graph.nodes.size() > kMaxNativeObjects) {
        result.errors.emplace_back("El grafo reactivo supera el limite de 8000 objetos.");
    }

    std::unordered_set<int> blocks;
    std::unordered_set<int> boundGroups;
    std::unordered_set<int> spawnableGroups;
    for (auto const& binding : graph.bindings) {
        if (binding.body >= bodies.size()) {
            result.errors.emplace_back("Una asignacion apunta a un cuerpo inexistente.");
            continue;
        }
        if (binding.object != kNativeAllObjects &&
            binding.object >= bodies[binding.body].objectCount) {
            result.errors.emplace_back("Una asignacion apunta a un fragmento inexistente.");
        }
        if (!validID(binding.group)) {
            result.errors.emplace_back("Una asignacion usa un Group ID fuera de 1..9999.");
        } else {
            boundGroups.insert(binding.group);
        }
    }
    for (std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex) {
        auto const& body = bodies[bodyIndex];
        if (!usesReactive(body)) continue;
        if (body.objectCount == 0) {
            result.errors.emplace_back("Un cuerpo reactivo no contiene objetos visuales.");
            continue;
        }
        if (body.settings.preset == NativePreset::Explosion) {
            std::vector<bool> covered(body.objectCount, false);
            for (auto const& binding : graph.bindings) {
                if (binding.body == bodyIndex && binding.object != kNativeAllObjects &&
                    binding.object < covered.size()) {
                    covered[binding.object] = true;
                }
            }
            if (std::ranges::find(covered, false) != covered.end()) {
                result.errors.emplace_back("Explosion no asigno un subgrupo a cada fragmento.");
            }
        } else {
            bool const hasRoot = std::ranges::any_of(
                graph.bindings,
                [&](NativeBinding const& binding) {
                    return binding.body == bodyIndex && binding.object == kNativeAllObjects;
                }
            );
            if (!hasRoot) {
                result.errors.emplace_back("Un cuerpo reactivo no tiene asignacion de grupo raiz.");
            }
        }
    }

    for (auto const& node : graph.nodes) {
        if (node.spawnTriggered) {
            for (int group : node.groups) {
                if (validID(group)) spawnableGroups.insert(group);
            }
        }
        if (node.groups.size() > 10) {
            result.errors.emplace_back("Un objeto generado necesita mas de 10 grupos.");
        }
        std::set<int> uniqueGroups;
        for (int group : node.groups) {
            if (!validID(group)) {
                result.errors.emplace_back("Un objeto generado usa un Group ID fuera de 1..9999.");
            }
            if (!uniqueGroups.insert(group).second) {
                result.errors.emplace_back("Un objeto generado repite el mismo Group ID.");
            }
        }
        switch (node.kind) {
            case NativeNodeKind::CollisionBlock:
                if (!validID(node.blockID)) {
                    result.errors.emplace_back("Un Collision Block no tiene Block ID valido.");
                } else {
                    blocks.insert(node.blockID);
                }
                if (node.size.x <= 0.f || node.size.y <= 0.f) {
                    result.errors.emplace_back("Un Collision Block tiene tamano nulo.");
                }
                break;
            case NativeNodeKind::CollisionTrigger:
                if (!validID(node.blockA)) {
                    result.errors.emplace_back("Un Collision Trigger no tiene Block A valido.");
                }
                if (node.collisionPeer == CollisionPeer::Block && !validID(node.blockB)) {
                    result.errors.emplace_back("Un Collision Trigger no tiene Block B valido.");
                }
                if (!validID(node.targetGroup)) {
                    result.errors.emplace_back("Un Collision Trigger no tiene grupo de accion valido.");
                }
                break;
            case NativeNodeKind::AdvancedFollow:
                if (!validID(node.targetGroup) || !validID(node.controlID)) {
                    result.errors.emplace_back("Un Advanced Follow no tiene Target/Control ID valido.");
                }
                if (node.followMode < 0 || node.followMode > 2 ||
                    node.startMode < 0 || node.startMode > 2) {
                    result.errors.emplace_back("Un Advanced Follow usa un modo nativo invalido.");
                }
                if (!node.followCenter && !node.followPlayer1 && !node.followPlayer2 &&
                    !validID(node.centerGroup)) {
                    result.errors.emplace_back("Un Advanced Follow no tiene objetivo nativo.");
                }
                break;
            case NativeNodeKind::AdvancedFollowEdit:
                if (!validID(node.targetGroup) || !validID(node.controlID) ||
                    node.startMode < 0 || node.startMode > 2 ||
                    !node.spawnTriggered || !node.multiTriggered) {
                    result.errors.emplace_back("Un Edit Physics no es reactivable mediante Spawn.");
                }
                break;
            case NativeNodeKind::Spawn:
                if (!validID(node.targetGroup)) {
                    result.errors.emplace_back("Un Spawn no tiene Target Group valido.");
                }
                if (std::ranges::find(node.groups, node.targetGroup) != node.groups.end() &&
                    node.delay < kMinLoopDelay) {
                    result.errors.emplace_back("Se detecto un bucle Spawn sin espera segura.");
                }
                break;
            case NativeNodeKind::Rotate:
                if (!validID(node.targetGroup) || node.duration <= 0.f) {
                    result.errors.emplace_back("Un Rotate nativo tiene parametros invalidos.");
                }
                break;
        }
    }

    for (auto const& node : graph.nodes) {
        if (node.kind != NativeNodeKind::CollisionTrigger) continue;
        if (!blocks.contains(node.blockA)) {
            result.errors.emplace_back("Un Collision Trigger referencia un Block A no generado.");
        }
        if (node.collisionPeer == CollisionPeer::Block && !blocks.contains(node.blockB)) {
            result.errors.emplace_back("Un Collision Trigger referencia un Block B no generado.");
        }
        if (!spawnableGroups.contains(node.targetGroup)) {
            result.errors.emplace_back("Un Collision Trigger apunta a un subgrupo sin accion Spawn.");
        }
    }
    for (auto const& node : graph.nodes) {
        if (node.kind == NativeNodeKind::Spawn &&
            !spawnableGroups.contains(node.targetGroup)) {
            result.errors.emplace_back("Un Spawn apunta a un subgrupo sin triggers reactivables.");
        }
    }
    for (auto const& node : graph.nodes) {
        if (node.kind == NativeNodeKind::AdvancedFollow && !boundGroups.contains(node.targetGroup)) {
            result.errors.emplace_back("Un Advanced Follow apunta a un grupo sin cuerpo visual.");
        }
    }
    if (graph.nodes.size() > 1500) {
        result.warnings.emplace_back("La salida reactiva es grande y puede ser costosa en movil.");
    }
    return result;
}

} // namespace paimon::editorphysics
