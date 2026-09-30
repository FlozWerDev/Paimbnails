#include "NativePreview.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace paimon::editorphysics {

namespace {

// a collision trigger fires when the sensor enters the block, not once per
// frame while it stays inside, so a contact has to be remembered.
struct NativeState {
    Vec2 position;
    Vec2 velocity;
    float angle = 0.f;
    float gravityTimer = 0.f;
    std::array<bool, 4> touching{};
};

struct Rect {
    float minX = 0.f;
    float maxX = 0.f;
    float minY = 0.f;
    float maxY = 0.f;
};

Vec2 rotate(Vec2 value, float angle) {
    float const c = std::cos(angle);
    float const s = std::sin(angle);
    return {value.x * c - value.y * s, value.x * s + value.y * c};
}

bool overlaps(Rect const& a, Rect const& b) {
    return a.minX < b.maxX && a.maxX > b.minX && a.minY < b.maxY && a.maxY > b.minY;
}

Rect around(Vec2 center, Vec2 size) {
    return {
        center.x - size.x * 0.5f,
        center.x + size.x * 0.5f,
        center.y - size.y * 0.5f,
        center.y + size.y * 0.5f,
    };
}

// every fixed capture turns into an axis-aligned collision block, so a slope
// stops a reactive body with the full square it spans and not with its face.
std::vector<Rect> worldBlocks(std::vector<BodySpec> const& bodies) {
    std::vector<Rect> blocks;
    for (auto const& body : bodies) {
        if (body.motion != Motion::Static) continue;
        for (auto const& fixture : body.fixtures) {
            Vec2 const offset = rotate(fixture.offset, body.angle);
            float const width = fixture.radius > 0.f
                ? fixture.radius * 2.f : fixture.halfSize.x * 2.f;
            float const height = fixture.radius > 0.f
                ? fixture.radius * 2.f : fixture.halfSize.y * 2.f;
            float const c = std::abs(std::cos(body.angle));
            float const s = std::abs(std::sin(body.angle));
            blocks.push_back(around(
                {body.position.x + offset.x, body.position.y + offset.y},
                {
                    std::max(4.f, c * width + s * height),
                    std::max(4.f, s * width + c * height),
                }
            ));
        }
    }
    return blocks;
}

std::array<Rect, 4> sensorsOf(BodySpec const& body, NativeBodySettings const& settings) {
    auto const bounds = nativeBounds(body);
    float const padding = std::clamp(settings.sensorPadding, 2.f, 30.f);
    float const width = std::max(6.f, bounds.maxX - bounds.minX);
    float const height = std::max(6.f, bounds.maxY - bounds.minY);
    float const centerX = (bounds.minX + bounds.maxX) * 0.5f;
    float const centerY = (bounds.minY + bounds.maxY) * 0.5f;
    return {{
        around({bounds.minX, centerY}, {padding, std::max(4.f, height - padding)}),
        around({bounds.maxX, centerY}, {padding, std::max(4.f, height - padding)}),
        around({centerX, bounds.minY}, {std::max(4.f, width - padding), padding}),
        around({centerX, bounds.maxY}, {std::max(4.f, width - padding), padding}),
    }};
}

void applyWorldHit(
    NativeState& state,
    std::size_t side,
    NativeProfile const& profile,
    float restitution
) {
    // a brief rebound push moves each sensor back out of its collision block.
    float const kick = profile.bounceImpulse * kPixelsPerSpeedUnit;
    switch (side) {
        case 0: state.velocity.x = -restitution * state.velocity.x + kick; break;
        case 1: state.velocity.x = -restitution * state.velocity.x - kick; break;
        case 2: state.velocity.y = -restitution * state.velocity.y + kick; break;
        default: state.velocity.y = -restitution * state.velocity.y - kick; break;
    }
}

std::size_t advance(
    NativeState& state,
    BodySpec const& body,
    NativeProfile const& profile,
    std::array<Rect, 4> const& sensors,
    std::vector<Rect> const& blocks,
    float gravitySign,
    float dt
) {
    // gravity arrives as a spawn loop, so the speed only changes on the tick and
    // the fall comes out as straight segments instead of the solver's curve.
    if (profile.gravityImpulse > 0.f) {
        state.gravityTimer += dt;
        while (state.gravityTimer >= profile.tick) {
            state.gravityTimer -= profile.tick;
            state.velocity.y += gravitySign * profile.gravityImpulse * kPixelsPerSpeedUnit;
        }
    }
    // advanced follow friction is read here as the share of the speed it takes
    // away in a second. gd's own scale for it is only known from playing.
    if (profile.friction > 0.f) {
        float const keep = std::exp(-profile.friction * dt);
        state.velocity = {state.velocity.x * keep, state.velocity.y * keep};
    }

    float const limit = profile.maxSpeed * kPixelsPerSpeedUnit;
    if (limit > 0.f) {
        float const speed = std::sqrt(
            state.velocity.x * state.velocity.x + state.velocity.y * state.velocity.y
        );
        if (speed > limit) {
            state.velocity = {
                state.velocity.x * limit / speed,
                state.velocity.y * limit / speed,
            };
        }
    }

    state.position.x += state.velocity.x * dt;
    state.position.y += state.velocity.y * dt;

    Vec2 const shift{
        state.position.x - body.position.x,
        state.position.y - body.position.y,
    };
    std::size_t hits = 0;
    for (std::size_t side = 0; side < sensors.size(); ++side) {
        Rect const sensor{
            sensors[side].minX + shift.x,
            sensors[side].maxX + shift.x,
            sensors[side].minY + shift.y,
            sensors[side].maxY + shift.y,
        };
        bool const touching = std::ranges::any_of(blocks, [&](Rect const& block) {
            return overlaps(sensor, block);
        });
        if (touching && !state.touching[side]) {
            applyWorldHit(state, side, profile, body.restitution);
            ++hits;
        }
        state.touching[side] = touching;
    }

    if (profile.rotateToDirection) {
        if (std::abs(state.velocity.x) > 0.01f || std::abs(state.velocity.y) > 0.01f) {
            state.angle = std::atan2(state.velocity.y, state.velocity.x);
        }
    } else {
        state.angle += body.angularVelocity * dt;
    }
    return hits;
}

} // namespace

bool nativeNeedsPlayer(NativeBodySettings const& settings, BodySpec const& body, float gravity) {
    auto const profile = nativeProfile(settings, body, gravity);
    return profile.followPlayer || profile.useAnchor || profile.fragmentObjects;
}

SimulationTrace simulateWorkspace(
    std::vector<BodySpec> const& bodies,
    std::vector<NativeBodySettings> const& settings,
    SimulationOptions const& options
) {
    std::vector<std::size_t> reactive;
    auto specs = bodies;
    for (std::size_t i = 0; i < specs.size() && i < settings.size(); ++i) {
        if (specs[i].motion != Motion::Dynamic ||
            settings[i].backend != PhysicsBackend::Reactive) continue;
        reactive.push_back(i);
        // in the level this body only collides with the player and with the
        // fixed captures, so it must not push the baked ones around here.
        specs[i].motion = Motion::Static;
        specs[i].velocity = {};
        specs[i].angularVelocity = 0.f;
        specs[i].category = 0u;
        specs[i].mask = 0u;
    }

    auto trace = simulate(specs, options);
    if (trace.exhausted || reactive.empty() || trace.frames.size() < 2) return trace;

    auto const blocks = worldBlocks(bodies);
    float const fixedStep = 1.f / static_cast<float>(std::clamp(options.fixedRate, 30, 480));
    for (auto index : reactive) {
        auto const& body = bodies[index];
        auto const profile = nativeProfile(
            settings[index], body, options.gravity.y, options.airDrag
        );
        if (profile.followPlayer || profile.useAnchor || profile.fragmentObjects) continue;

        auto const sensors = sensorsOf(body, settings[index]);
        float const gravitySign = options.gravity.y * body.gravityScale < 0.f ? -1.f : 1.f;
        NativeState state;
        state.position = body.position;
        state.velocity = body.velocity;
        state.angle = body.angle;

        float time = 0.f;
        for (std::size_t frame = 1; frame < trace.frames.size(); ++frame) {
            float const target = trace.frames[frame].time;
            while (time + 0.000001f < target) {
                float const dt = std::min(fixedStep, target - time);
                trace.impacts += advance(
                    state, body, profile, sensors, blocks, gravitySign, dt
                );
                time += dt;
            }
            trace.frames[frame].poses[index] = {state.position, state.angle};
        }
    }
    return trace;
}

} // namespace paimon::editorphysics
