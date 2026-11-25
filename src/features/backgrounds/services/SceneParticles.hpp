#pragma once

#include "SceneJson.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace paimon::wallpaper {

// wallpaper Engine particle system reduced to what a 2D background can show:
// box/sphere emitters, the common random initializers, movement (gravity and
// drag), turbulence, alpha fades, oscillations and size changes. Everything is
// expressed in the emitter's local scene units. Cocos-free.
struct SceneParticleDesc {
    struct Range { float min = 0.f, max = 0.f; };
    struct Vec { float x = 0.f, y = 0.f, z = 0.f; };
    struct Oscillation {
        bool enabled = false;
        Range frequency{0.f, 0.f};
        Range scale{0.f, 0.f};
        Vec mask{1.f, 1.f, 0.f};
    };

    bool sphere = false;
    Vec origin{};
    Vec distanceMin{};
    Vec distanceMax{};
    Vec directions{1.f, 1.f, 1.f};
    Vec sign{};
    float rate = 10.f;
    int maxCount = 64;
    float startTime = 0.f;

    Range lifetime{1.f, 1.f};
    Range size{20.f, 20.f};
    float sizeExponent = 1.f;
    Vec velocityMin{}, velocityMax{};
    Range turbulentSpeed{0.f, 0.f};
    Vec colorMin{1.f, 1.f, 1.f}, colorMax{1.f, 1.f, 1.f};
    Range alpha{1.f, 1.f};
    Range rotation{0.f, 0.f};
    Range angularVelocity{0.f, 0.f};
    bool spins = false;

    Vec gravity{};
    float drag = 0.f;
    bool alphaFade = false;
    float fadeIn = 0.f;
    float fadeOut = 1.f;
    bool turbulence = false;
    Range turbulenceSpeed{0.f, 0.f};
    float turbulenceScale = 0.01f;
    float turbulenceTimescale = 1.f;
    Oscillation oscillateAlpha, oscillateSize, oscillatePosition;
    bool sizeChange = false;
    float sizeStartTime = 0.f, sizeEndTime = 1.f, sizeStartValue = 1.f, sizeEndValue = 1.f;

    // sprite sheets: one frame per particle (randomframe) or the whole sheet
    // played over the particle's life, sequencemultiplier times.
    bool randomFrame = false;
    float sequenceMultiplier = 1.f;

    // ropetrail / spritetrail renderers: quads stretched along the velocity.
    bool trail = false;
    float trailLength = 0.08f;

    // tint from instanceoverride / the object colour (0..1).
    Vec tint{1.f, 1.f, 1.f};
    float alphaScale = 1.f;
    // largest particle the system can draw, in local units (mip selection).
    float maxSize() const { return std::max(size.min, size.max) * (sizeChange ? std::max({sizeStartValue, sizeEndValue, 1.f}) : 1.f); }
};

namespace scene_particles_detail {
using namespace scenejson;

inline SceneParticleDesc::Range rangeOf(matjson::Value const& node, char const* minKey, char const* maxKey,
    SceneParticleDesc::Range fallback) {
    SceneParticleDesc::Range r = fallback;
    if (node.contains(minKey)) r.min = static_cast<float>(numberOf(node[minKey], fallback.min));
    if (node.contains(maxKey)) r.max = static_cast<float>(numberOf(node[maxKey], fallback.max));
    else if (node.contains(minKey)) r.max = std::max(r.max, r.min);
    return r;
}

inline SceneParticleDesc::Vec vecOfP(matjson::Value const& node, char const* key, SceneParticleDesc::Vec fallback) {
    if (!node.contains(key)) return fallback;
    auto const v = vecOf(node[key], {fallback.x, fallback.y, fallback.z});
    return {v.x, v.y, v.z};
}

inline float overrideOf(matjson::Value const& overrides, char const* key) {
    if (!overrides.isObject() || !overrides.contains(key)) return 1.f;
    float const value = static_cast<float>(numberOf(overrides[key], 1.0));
    return std::isfinite(value) && value >= 0.f ? value : 1.f;
}
} // namespace scene_particles_detail

// `overrides` is the scene object's "instanceoverride" block (may be null).
inline SceneParticleDesc parseSceneParticles(matjson::Value const& system, matjson::Value const& overrides) {
    using namespace scene_particles_detail;
    SceneParticleDesc d;
    if (!system.isObject()) return d;

    d.maxCount = static_cast<int>(numberOf(system["maxcount"], 64.0));
    d.randomFrame = lowerOf(stringOf(system["animationmode"])) == "randomframe";
    d.sequenceMultiplier = std::clamp(static_cast<float>(numberOf(system["sequencemultiplier"], 1.0)), 0.01f, 100.f);
    d.startTime = static_cast<float>(numberOf(system["starttime"], 0.0));

    if (system.contains("emitter") && system["emitter"].isArray()) {
        auto const& emitters = system["emitter"].asArray().unwrap();
        if (!emitters.empty()) {
            auto const& e = emitters.front();
            d.sphere = lowerOf(stringOf(e["name"])) == "sphererandom";
            d.origin = vecOfP(e, "origin", {});
            d.distanceMin = vecOfP(e, "distancemin", {});
            d.distanceMax = vecOfP(e, "distancemax", {});
            d.directions = vecOfP(e, "directions", {1.f, 1.f, 1.f});
            d.sign = vecOfP(e, "sign", {});
            d.rate = static_cast<float>(numberOf(e["rate"], 10.0));
        }
    }

    if (system.contains("initializer") && system["initializer"].isArray()) {
        for (auto const& init : system["initializer"].asArray().unwrap()) {
            auto const name = lowerOf(stringOf(init["name"]));
            if (name == "lifetimerandom") d.lifetime = rangeOf(init, "min", "max", {1.f, 1.f});
            else if (name == "sizerandom") {
                d.size = rangeOf(init, "min", "max", {20.f, 20.f});
                d.sizeExponent = static_cast<float>(numberOf(init["exponent"], 1.0));
            } else if (name == "velocityrandom") {
                d.velocityMin = vecOfP(init, "min", {-32.f, -32.f, 0.f});
                d.velocityMax = vecOfP(init, "max", {32.f, 32.f, 0.f});
            } else if (name == "turbulentvelocityrandom") {
                d.turbulentSpeed = rangeOf(init, "speedmin", "speedmax", {100.f, 250.f});
            } else if (name == "colorrandom") {
                auto const lo = colorOf(init["min"], {1.f, 1.f, 1.f});
                auto const hi = init.contains("max") ? colorOf(init["max"], {1.f, 1.f, 1.f}) : lo;
                d.colorMin = {lo.x, lo.y, lo.z};
                d.colorMax = {hi.x, hi.y, hi.z};
            } else if (name == "alpharandom") {
                d.alpha = rangeOf(init, "min", "max", {0.05f, 1.f});
            } else if (name == "rotationrandom") {
                auto const lo = vecOfP(init, "min", {});
                auto const hi = vecOfP(init, "max", {0.f, 0.f, 6.2831853f});
                d.rotation = {lo.z, hi.z};
            } else if (name == "angularvelocityrandom") {
                auto const lo = vecOfP(init, "min", {0.f, 0.f, -5.f});
                auto const hi = vecOfP(init, "max", {0.f, 0.f, 5.f});
                d.angularVelocity = {lo.z, hi.z};
            }
        }
    }

    if (system.contains("operator") && system["operator"].isArray()) {
        for (auto const& op : system["operator"].asArray().unwrap()) {
            auto const name = lowerOf(stringOf(op["name"]));
            if (name == "movement") {
                d.gravity = vecOfP(op, "gravity", {});
                d.drag = static_cast<float>(numberOf(op["drag"], 0.0));
            } else if (name == "alphafade") {
                d.alphaFade = true;
                d.fadeIn = static_cast<float>(numberOf(op["fadeintime"], 0.5));
                d.fadeOut = static_cast<float>(numberOf(op["fadeouttime"], 0.5));
            } else if (name == "angularmovement") {
                d.spins = true;
            } else if (name == "turbulence") {
                d.turbulence = true;
                d.turbulenceSpeed = rangeOf(op, "speedmin", "speedmax", {500.f, 1000.f});
                d.turbulenceScale = static_cast<float>(numberOf(op["scale"], 0.01));
                d.turbulenceTimescale = static_cast<float>(numberOf(op["timescale"], 1.0));
            } else if (name == "oscillatealpha" || name == "oscillatesize" || name == "oscillateposition") {
                auto& osc = name == "oscillatealpha" ? d.oscillateAlpha
                    : name == "oscillatesize" ? d.oscillateSize : d.oscillatePosition;
                osc.enabled = true;
                osc.frequency = rangeOf(op, "frequencymin", "frequencymax", {0.f, 10.f});
                osc.scale = rangeOf(op, "scalemin", "scalemax", {0.f, 1.f});
                osc.mask = vecOfP(op, "mask", {1.f, 1.f, 0.f});
            } else if (name == "sizechange") {
                d.sizeChange = true;
                d.sizeStartTime = static_cast<float>(numberOf(op["starttime"], 0.0));
                d.sizeEndTime = static_cast<float>(numberOf(op["endtime"], 1.0));
                d.sizeStartValue = static_cast<float>(numberOf(op["startvalue"], 1.0));
                d.sizeEndValue = static_cast<float>(numberOf(op["endvalue"], 0.0));
            }
        }
    }

    if (system.contains("renderer") && system["renderer"].isArray()) {
        for (auto const& renderer : system["renderer"].asArray().unwrap()) {
            auto const name = lowerOf(stringOf(renderer["name"]));
            if (name == "ropetrail" || name == "spritetrail") {
                d.trail = true;
                d.trailLength = std::clamp(static_cast<float>(numberOf(renderer["length"], 0.08)), 0.01f, 1.f);
            }
        }
    }

    // instance overrides scale the authored preset.
    float const count = overrideOf(overrides, "count");
    float const rate = overrideOf(overrides, "rate");
    float const lifetime = overrideOf(overrides, "lifetime");
    float const size = overrideOf(overrides, "size");
    float const speed = overrideOf(overrides, "speed");
    float const alpha = overrideOf(overrides, "alpha");
    d.maxCount = std::clamp(static_cast<int>(std::lround(d.maxCount * count)), 1, 3000);
    d.rate = std::clamp(d.rate * rate, 0.f, 5000.f);
    d.lifetime.min *= lifetime;
    d.lifetime.max *= lifetime;
    d.size.min *= size;
    d.size.max *= size;
    for (auto* v : {&d.velocityMin, &d.velocityMax}) { v->x *= speed; v->y *= speed; v->z *= speed; }
    d.turbulentSpeed.min *= speed;
    d.turbulentSpeed.max *= speed;
    d.alphaScale = alpha;
    if (overrides.isObject()) {
        if (overrides.contains("colorn")) {
            auto const c = colorOf(overrides["colorn"]);
            d.tint = {c.x, c.y, c.z};
        } else if (overrides.contains("color")) {
            auto const c = colorOf(overrides["color"]);
            d.tint = {c.x, c.y, c.z};
        }
    }
    if (d.lifetime.min <= 0.01f && d.lifetime.max <= 0.01f) d.lifetime = {1.f, 1.f};
    d.startTime = std::clamp(d.startTime, 0.f, 30.f);
    return d;
}

// simulation state; deterministic for a given seed.
class SceneParticleSim {
public:
    struct Particle {
        float x = 0.f, y = 0.f, vx = 0.f, vy = 0.f;
        float age = 0.f, life = 1.f;
        float size = 1.f;
        float r = 1.f, g = 1.f, b = 1.f, alpha = 1.f;
        float rotation = 0.f, spin = 0.f;
        float phase = 0.f;
        float alphaFrequency = 0.f, alphaAmount = 0.f;
        float sizeFrequency = 0.f, sizeAmount = 0.f;
        float positionFrequency = 0.f, positionAmount = 0.f;
        float turbulence = 0.f;
        float frameSeed = 0.f;
    };

    // what one particle draws this frame (local units).
    struct Sprite {
        float x, y, size, rotation, r, g, b, alpha, vx, vy, age;
        // 0..1 through the particle's life, and a fixed per-particle random.
        float life, seed;
    };

    explicit SceneParticleSim(SceneParticleDesc desc, uint32_t seed = 0x9e3779b9u)
        : m_desc(desc), m_seed(seed ? seed : 1u) {
        m_particles.reserve(static_cast<size_t>(m_desc.maxCount));
    }

    SceneParticleDesc const& desc() const { return m_desc; }
    size_t count() const { return m_particles.size(); }

    // runs the authored start time so the system opens already populated.
    void prewarm() {
        float remaining = m_desc.startTime;
        int steps = 0;
        while (remaining > 0.f && steps++ < 900) {
            float const dt = std::min(remaining, 1.f / 30.f);
            step(dt);
            remaining -= dt;
        }
    }

    void step(float dt) {
        if (!(dt > 0.f) || !std::isfinite(dt)) return;
        dt = std::min(dt, 0.1f);
        m_time += dt;
        auto const& d = m_desc;

        for (size_t i = 0; i < m_particles.size();) {
            auto& p = m_particles[i];
            p.age += dt;
            if (p.age >= p.life) {
                p = m_particles.back();
                m_particles.pop_back();
                continue;
            }
            p.vx += d.gravity.x * dt;
            p.vy += d.gravity.y * dt;
            if (d.drag > 0.f) {
                float const keep = std::max(0.f, 1.f - d.drag * dt);
                p.vx *= keep;
                p.vy *= keep;
            }
            if (d.turbulence && p.turbulence > 0.f) {
                float const s = d.turbulenceScale * 6.2831853f;
                float const t = m_time * d.turbulenceTimescale;
                float const ax = std::sin(p.y * s + t + p.phase) + 0.5f * std::sin(p.x * s * 1.7f - t * 1.3f);
                float const ay = std::cos(p.x * s - t * 0.8f + p.phase) + 0.5f * std::cos(p.y * s * 1.3f + t * 1.1f);
                p.vx += ax * p.turbulence * dt * 0.5f;
                p.vy += ay * p.turbulence * dt * 0.5f;
            }
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (d.spins) p.rotation += p.spin * dt;
            ++i;
        }

        if (d.rate > 0.f) {
            m_emitCarry += d.rate * dt;
            int budget = static_cast<int>(m_emitCarry);
            m_emitCarry -= static_cast<float>(budget);
            while (budget-- > 0 && static_cast<int>(m_particles.size()) < d.maxCount) spawn();
            if (static_cast<int>(m_particles.size()) >= d.maxCount) m_emitCarry = 0.f;
        }
    }

    template <class Fn>
    void forEachSprite(Fn&& fn) const {
        auto const& d = m_desc;
        for (auto const& p : m_particles) {
            float const t = p.life > 0.f ? std::clamp(p.age / p.life, 0.f, 1.f) : 1.f;
            float alpha = p.alpha;
            if (d.alphaFade) {
                if (d.fadeIn > 0.f && t < d.fadeIn) alpha *= t / d.fadeIn;
                if (d.fadeOut < 1.f && t > d.fadeOut) alpha *= std::max(0.f, 1.f - (t - d.fadeOut) / (1.f - d.fadeOut));
            }
            float size = p.size;
            if (d.sizeChange) {
                float const span = std::max(1e-4f, d.sizeEndTime - d.sizeStartTime);
                float const k = std::clamp((t - d.sizeStartTime) / span, 0.f, 1.f);
                size *= d.sizeStartValue + (d.sizeEndValue - d.sizeStartValue) * k;
            }
            float x = p.x, y = p.y;
            float const tau = 6.2831853f;
            if (d.oscillateAlpha.enabled) {
                float const wave = 0.5f + 0.5f * std::sin(tau * p.alphaFrequency * p.age + p.phase);
                alpha *= 1.f - std::clamp(p.alphaAmount, 0.f, 1.f) * wave;
            }
            if (d.oscillateSize.enabled) {
                float const wave = 0.5f + 0.5f * std::sin(tau * p.sizeFrequency * p.age + p.phase * 1.3f);
                size *= std::max(0.f, 1.f - std::clamp(p.sizeAmount, 0.f, 1.f) * wave);
            }
            if (d.oscillatePosition.enabled) {
                float const wave = std::sin(tau * p.positionFrequency * p.age + p.phase * 0.7f);
                x += wave * p.positionAmount * d.oscillatePosition.mask.x;
                y += std::cos(tau * p.positionFrequency * p.age + p.phase) * p.positionAmount * d.oscillatePosition.mask.y;
            }
            alpha = std::clamp(alpha * d.alphaScale, 0.f, 1.f);
            if (alpha <= 0.002f || size <= 0.01f) continue;
            fn(Sprite{x, y, size, p.rotation, p.r * d.tint.x, p.g * d.tint.y, p.b * d.tint.z, alpha, p.vx, p.vy, p.age, t, p.frameSeed});
        }
    }

private:
    float random() {
        // xorshift32: cheap, deterministic, good enough for particles.
        m_seed ^= m_seed << 13;
        m_seed ^= m_seed >> 17;
        m_seed ^= m_seed << 5;
        return static_cast<float>(m_seed & 0xFFFFFF) / static_cast<float>(0x1000000);
    }
    float between(SceneParticleDesc::Range r) { return r.min + (r.max - r.min) * random(); }
    float between(float a, float b) { return a + (b - a) * random(); }

    void spawn() {
        auto const& d = m_desc;
        Particle p;
        float px = 0.f, py = 0.f;
        if (d.sphere) {
            float const radius = between(d.distanceMin.x, std::max(d.distanceMin.x, d.distanceMax.x));
            float const angle = random() * 6.2831853f;
            px = std::cos(angle) * radius * d.directions.x;
            py = std::sin(angle) * radius * d.directions.y;
        } else {
            px = between(-d.distanceMax.x, d.distanceMax.x);
            py = between(-d.distanceMax.y, d.distanceMax.y);
        }
        if (d.sign.x > 0.f) px = std::abs(px); else if (d.sign.x < 0.f) px = -std::abs(px);
        if (d.sign.y > 0.f) py = std::abs(py); else if (d.sign.y < 0.f) py = -std::abs(py);
        p.x = d.origin.x + px;
        p.y = d.origin.y + py;
        p.vx = between(d.velocityMin.x, d.velocityMax.x);
        p.vy = between(d.velocityMin.y, d.velocityMax.y);
        if (d.turbulentSpeed.max > 0.f) {
            float const speed = between(d.turbulentSpeed);
            float const angle = random() * 6.2831853f;
            p.vx += std::cos(angle) * speed;
            p.vy += std::sin(angle) * speed;
        }
        p.life = std::max(0.05f, between(d.lifetime));
        float const sizeT = d.sizeExponent != 1.f && d.sizeExponent > 0.f
            ? std::pow(random(), d.sizeExponent) : random();
        p.size = d.size.min + (d.size.max - d.size.min) * sizeT;
        float const colorT = random();
        p.r = d.colorMin.x + (d.colorMax.x - d.colorMin.x) * colorT;
        p.g = d.colorMin.y + (d.colorMax.y - d.colorMin.y) * colorT;
        p.b = d.colorMin.z + (d.colorMax.z - d.colorMin.z) * colorT;
        p.alpha = std::clamp(between(d.alpha), 0.f, 1.f);
        p.rotation = between(d.rotation);
        p.spin = between(d.angularVelocity);
        p.phase = random() * 6.2831853f;
        if (d.oscillateAlpha.enabled) {
            p.alphaFrequency = between(d.oscillateAlpha.frequency);
            p.alphaAmount = between(d.oscillateAlpha.scale);
        }
        if (d.oscillateSize.enabled) {
            p.sizeFrequency = between(d.oscillateSize.frequency);
            p.sizeAmount = between(d.oscillateSize.scale);
        }
        if (d.oscillatePosition.enabled) {
            p.positionFrequency = between(d.oscillatePosition.frequency);
            p.positionAmount = between(d.oscillatePosition.scale);
        }
        if (d.turbulence) p.turbulence = between(d.turbulenceSpeed);
        p.frameSeed = random();
        m_particles.push_back(p);
    }

    SceneParticleDesc m_desc;
    std::vector<Particle> m_particles;
    uint32_t m_seed;
    float m_emitCarry = 0.f;
    float m_time = 0.f;
};

} // namespace paimon::wallpaper
