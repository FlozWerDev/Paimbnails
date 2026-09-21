#include "FrameInterpolator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/GJMGLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

using namespace geode::prelude;

namespace paimon::frameinterp {

namespace {

// GD 2.2 simula a 240 pasos por segundo. Solo entra en el calculo del retraso,
// asi que un mod que cambie el tickrate desajusta la latencia, no el suavizado.
constexpr double kPhysicsStep = 1.0 / 240.0;

// Encima de esto ya no es movimiento sino un salto (teletransporte,
// checkpoint...): se dibuja tal cual ese frame, sin interpolar.
constexpr float kSnapSpeed = 4000.f;
constexpr float kSnapSpin  = 2000.f;
constexpr float kSnapZoom  = 4.f;

constexpr float kEpsilon = 0.0005f;

constexpr uint8_t kPos   = 1;
constexpr uint8_t kRot   = 2;
constexpr uint8_t kScale = 4;

Transform readTransform(CCNode* node) {
    Transform out;
    out.pos    = node->getPosition();
    out.rotX   = node->getRotationX();
    out.rotY   = node->getRotationY();
    out.scaleX = node->getScaleX();
    out.scaleY = node->getScaleY();
    return out;
}

void writeTransform(CCNode* node, Transform const& state, uint8_t mask) {
    if (mask & kPos) node->setPosition(state.pos);
    if (mask & kRot) {
        node->setRotationX(state.rotX);
        node->setRotationY(state.rotY);
    }
    if (mask & kScale) {
        node->setScaleX(state.scaleX);
        node->setScaleY(state.scaleY);
    }
}

Transform mix(Transform const& from, Transform const& to, float t) {
    Transform out;
    out.pos.x  = from.pos.x  + (to.pos.x  - from.pos.x)  * t;
    out.pos.y  = from.pos.y  + (to.pos.y  - from.pos.y)  * t;
    out.rotX   = from.rotX   + (to.rotX   - from.rotX)   * t;
    out.rotY   = from.rotY   + (to.rotY   - from.rotY)   * t;
    out.scaleX = from.scaleX + (to.scaleX - from.scaleX) * t;
    out.scaleY = from.scaleY + (to.scaleY - from.scaleY) * t;
    return out;
}

bool jumped(Transform const& from, Transform const& to, double span) {
    float const window = static_cast<float>(span > 0.0 ? span : kPhysicsStep);
    return std::abs(to.pos.x - from.pos.x)   > kSnapSpeed * window
        || std::abs(to.pos.y - from.pos.y)   > kSnapSpeed * window
        || std::abs(to.rotX - from.rotX)     > kSnapSpin * window
        || std::abs(to.rotY - from.rotY)     > kSnapSpin * window
        || std::abs(to.scaleX - from.scaleX) > kSnapZoom * window
        || std::abs(to.scaleY - from.scaleY) > kSnapZoom * window;
}

bool moved(float a, float b) {
    return std::abs(a - b) > kEpsilon;
}

} // namespace

double latencyLag(int latency) {
    switch (static_cast<Latency>(latency)) {
        case Latency::Smooth:   return 1.0;
        case Latency::Balanced: return 0.5;
        case Latency::Instant:  return 0.0;
    }
    return 1.0;
}

FrameInterpolator& FrameInterpolator::get() {
    static FrameInterpolator instance;
    return instance;
}

std::filesystem::path FrameInterpolator::configPath() const {
    return Mod::get()->getSaveDir() / "frame_interp.json";
}

void FrameInterpolator::init() {
    if (m_loaded) return;
    loadConfig();
    m_loaded = true;
    log::info("[PaimonInterp] Config lista (activo={}, latencia={})",
              m_config.enabled, m_config.latency);
}

void FrameInterpolator::loadConfig() {
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto rawRes = file::readString(path);
    if (!rawRes) {
        log::warn("[PaimonInterp] No se pudo leer la config: {}", rawRes.unwrapErr());
        return;
    }

    auto res = matjson::parse(rawRes.unwrap());
    if (res.isErr()) {
        log::warn("[PaimonInterp] JSON de config invalido: {}", res.unwrapErr());
        return;
    }
    auto j = res.unwrap();

    auto getBool = [&](char const* k, bool d)  { return j[k].asBool().unwrapOr(d); };
    auto getInt  = [&](char const* k, int d)   { return j[k].asInt().unwrapOr(d); };
    auto getFlt  = [&](char const* k, float d) {
        return static_cast<float>(j[k].asDouble().unwrapOr(static_cast<double>(d)));
    };

    FrameInterpConfig& c = m_config;
    c.enabled       = getBool("enabled", c.enabled);
    c.camera        = getBool("camera", c.camera);
    c.scenery       = getBool("scenery", c.scenery);
    c.players       = getBool("players", c.players);
    c.movingObjects = getBool("movingObjects", c.movingObjects);

    c.latency       = getInt("latency", c.latency);
    c.strength      = getFlt("strength", c.strength);
    c.objectLimit   = getInt("objectLimit", c.objectLimit);

    c.inGameplay    = getBool("inGameplay", c.inGameplay);
    c.inEditor      = getBool("inEditor", c.inEditor);

    sanitize();
}

void FrameInterpolator::saveConfig() {
    sanitize();

    FrameInterpConfig const& c = m_config;
    matjson::Value j;
    j["enabled"]       = c.enabled;
    j["camera"]        = c.camera;
    j["scenery"]       = c.scenery;
    j["players"]       = c.players;
    j["movingObjects"] = c.movingObjects;

    j["latency"]       = c.latency;
    j["strength"]      = c.strength;
    j["objectLimit"]   = c.objectLimit;

    j["inGameplay"]    = c.inGameplay;
    j["inEditor"]      = c.inEditor;

    auto path = configPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("[PaimonInterp] No se pudo escribir la config en {}",
                   geode::utils::string::pathToString(path));
        return;
    }
    auto txt = j.dump();
    out.write(txt.data(), static_cast<std::streamsize>(txt.size()));
}

void FrameInterpolator::resetToDefaults() {
    bool const wasEnabled = m_config.enabled;
    m_config = FrameInterpConfig{};
    m_config.enabled = wasEnabled;
    saveConfig();
    reset();
}

void FrameInterpolator::sanitize() {
    FrameInterpConfig& c = m_config;
    c.latency     = std::clamp(c.latency, 0, 2);
    c.strength    = std::clamp(c.strength, 0.f, 1.f);
    c.objectLimit = std::clamp(c.objectLimit, 0, 2000);
}

bool FrameInterpolator::isEnabled() const {
    return m_config.enabled;
}

void FrameInterpolator::setEnabled(bool enabled) {
    if (m_config.enabled == enabled) return;
    m_config.enabled = enabled;
    if (!enabled) reset();
    saveConfig();
}

void FrameInterpolator::reset() {
    m_layer = nullptr;
    m_tracked.clear();
    m_objects.clear();
    m_pending.clear();
    m_stepPending = false;
    m_stepped = 0.0;
    m_leftover = 0.0;
    m_span = 0.0;
    m_active = false;
    m_alpha = 1.f;
    m_stepsPerFrame = 0.f;
    m_trackedCount = 0;
}

void FrameInterpolator::onStepped(double stepped, double leftover) {
    if (!m_config.enabled) return;
    m_stepped = stepped;
    m_leftover = leftover;
    m_stepPending = true;
}

bool FrameInterpolator::shouldRun(GJBaseGameLayer* layer) const {
    if (!layer || !m_config.enabled) return false;
    if (m_config.strength <= 0.001f) return false;
    if (!m_config.camera && !m_config.scenery && !m_config.players && !m_config.movingObjects) {
        return false;
    }

    auto* play = PlayLayer::get();
    if (play && static_cast<GJBaseGameLayer*>(play) == layer) return m_config.inGameplay;
    return m_config.inEditor;
}

void FrameInterpolator::syncTracks(GJBaseGameLayer* layer) {
    m_wanted.clear();
    auto add = [this](CCNode* node) {
        if (node) m_wanted.push_back(node);
    };

    if (m_config.camera) add(layer->m_objectLayer);
    if (m_config.scenery) {
        add(layer->m_background);
        for (auto* ground : {layer->m_groundLayer, layer->m_groundLayer2}) {
            if (!ground) continue;
            add(ground);
            add(ground->m_ground1Sprite);
            add(ground->m_ground2Sprite);
        }
        if (auto* mg = layer->m_middleground) {
            add(mg);
            add(mg->m_ground1Sprite);
            add(mg->m_ground2Sprite);
        }
    }
    if (m_config.players) {
        add(layer->m_player1);
        add(layer->m_player2);
    }

    if (m_wanted.size() == m_tracked.size()) {
        size_t i = 0;
        while (i < m_wanted.size() && m_tracked[i].node == m_wanted[i]) i++;
        if (i == m_wanted.size()) return;
    }

    m_tracked.clear();
    m_tracked.reserve(m_wanted.size());
    for (auto* node : m_wanted) {
        Slot slot;
        slot.node = node;
        m_tracked.push_back(slot);
    }
}

void FrameInterpolator::syncObjects(GJBaseGameLayer* layer, bool advanced) {
    auto* moving = layer->m_objectsToMove;
    if (!moving) return;

    int seen = 0;
    for (auto* object : CCArrayExt<GameObject*>(moving)) {
        if (!object) continue;
        if (seen >= m_config.objectLimit) break;
        seen++;

        auto& slot = m_objects[object];
        if (advanced && slot.hasCur) {
            slot.prev = slot.cur;
            slot.hasPrev = true;
        }
        slot.cur = readTransform(object);
        slot.hasCur = true;
        slot.stamp = m_frame;
        if (slot.hasPrev) applyNode(object, slot.cur, slot.prev);
    }

    // Un objeto que deja de moverse se queda en el mapa; se barre cada tanto
    // para que una sesion larga no lo deje creciendo.
    if (m_frame % 600 != 0) return;
    for (auto it = m_objects.begin(); it != m_objects.end();) {
        it = (m_frame - it->second.stamp > 600) ? m_objects.erase(it) : std::next(it);
    }
}

void FrameInterpolator::applyNode(CCNode* node, Transform const& authoritative,
                                  Transform const& prev) {
    if (jumped(prev, authoritative, m_span)) return;

    Transform const target = mix(prev, authoritative, m_alpha);

    uint8_t mask = 0;
    if (moved(target.pos.x, authoritative.pos.x) || moved(target.pos.y, authoritative.pos.y)) {
        mask |= kPos;
    }
    if (moved(target.rotX, authoritative.rotX) || moved(target.rotY, authoritative.rotY)) {
        mask |= kRot;
    }
    if (moved(target.scaleX, authoritative.scaleX) || moved(target.scaleY, authoritative.scaleY)) {
        mask |= kScale;
    }
    if (mask == 0) return;

    m_pending.push_back({node, authoritative, mask});
    writeTransform(node, target, mask);
    m_trackedCount++;
}

void FrameInterpolator::beginVisit(GJBaseGameLayer* layer) {
    m_active = false;
    m_trackedCount = 0;
    m_pending.clear();

    if (!shouldRun(layer)) {
        if (m_layer) reset();
        return;
    }

    if (layer != m_layer) {
        reset();
        m_layer = layer;
    }

    // Sin update no hay paso nuevo: en pausa, cargando o con el scheduler
    // parado se dibuja el estado autentico y no se toca nada.
    if (!m_stepPending) return;
    m_stepPending = false;
    m_frame++;

    bool const advanced = m_stepped > 0.0;
    if (advanced) m_span = m_stepped;
    m_stepsPerFrame = m_stepsPerFrame * 0.85f + static_cast<float>(m_stepped / kPhysicsStep) * 0.15f;

    // Se dibuja t_B + sobrante menos el retraso, como fraccion del tramo entre
    // fotos. Salirse de [0, 1] no es error: la recta prev-cur es la velocidad.
    double const span = m_span > 0.0 ? m_span : kPhysicsStep;
    double const raw = 1.0 + (m_leftover - kPhysicsStep * latencyLag(m_config.latency)) / span;
    double const eased = 1.0 + (std::clamp(raw, -0.5, 1.5) - 1.0)
                             * static_cast<double>(m_config.strength);
    m_alpha = static_cast<float>(eased);

    syncTracks(layer);
    for (auto& slot : m_tracked) {
        if (advanced && slot.hasCur) {
            slot.prev = slot.cur;
            slot.hasPrev = true;
        }
        slot.cur = readTransform(slot.node);
        slot.hasCur = true;
        if (slot.hasPrev) applyNode(slot.node, slot.cur, slot.prev);
    }

    if (m_config.movingObjects) syncObjects(layer, advanced);

    m_active = !m_pending.empty();
}

void FrameInterpolator::endVisit() {
    for (auto it = m_pending.rbegin(); it != m_pending.rend(); ++it) {
        writeTransform(it->node, it->state, it->mask);
    }
    m_pending.clear();
}

} // namespace paimon::frameinterp
