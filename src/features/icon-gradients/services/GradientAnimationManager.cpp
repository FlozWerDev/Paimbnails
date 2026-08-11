#include "GradientAnimationManager.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <algorithm>
#include <array>

using namespace geode::prelude;

namespace paimon::icon_gradients {

namespace {

constexpr char const* kEnabledKey = "gradient-animation-enabled";
constexpr char const* kTypeKey = "gradient-animation-type";
constexpr char const* kSpeedKey = "gradient-animation-speed";
constexpr char const* kIntensityKey = "gradient-animation-intensity";
constexpr char const* kReverseKey = "gradient-animation-reverse";
constexpr char const* kCustomKey = "gradient-animation-custom";

GradientAnimationType validType(int value) {
    if (value < static_cast<int>(GradientAnimationType::Flow)
        || value > static_cast<int>(GradientAnimationType::Custom)) {
        return GradientAnimationType::Flow;
    }
    return static_cast<GradientAnimationType>(value);
}

GradientMotion validMotion(int value) {
    if (value < 0 || value >= kGradientMotionCount) return GradientMotion::SlideX;
    return static_cast<GradientMotion>(value);
}

GradientWave validWave(int value) {
    if (value < 0 || value >= kGradientWaveCount) return GradientWave::Smooth;
    return static_cast<GradientWave>(value);
}

GradientAnimationLayer clampLayer(GradientAnimationLayer layer) {
    layer.motion = validMotion(static_cast<int>(layer.motion));
    layer.wave = validWave(static_cast<int>(layer.wave));
    layer.amount = std::clamp(layer.amount, 0.f, 1.f);
    layer.speed = std::clamp(layer.speed, kLayerSpeedMin, kLayerSpeedMax);
    layer.phase = std::clamp(layer.phase, 0.f, 1.f);
    return layer;
}

} // namespace

GradientAnimationManager& GradientAnimationManager::get() {
    static GradientAnimationManager instance;
    return instance;
}

GradientAnimationManager::GradientAnimationManager() {
    load();
}

void GradientAnimationManager::load() {
    auto mod = Mod::get();
    m_config.type = validType(static_cast<int>(
        mod->getSavedValue<int64_t>(kTypeKey, static_cast<int64_t>(GradientAnimationType::Flow))
    ));
    m_config.speed = std::clamp(
        static_cast<float>(mod->getSavedValue<double>(kSpeedKey, 1.0)), 0.1f, 4.f
    );
    m_config.intensity = std::clamp(
        static_cast<float>(mod->getSavedValue<double>(kIntensityKey, 0.6)), 0.f, 1.f
    );
    m_config.reverse = mod->getSavedValue<bool>(kReverseKey, false);

    m_config.custom.clear();

    matjson::Value stored = mod->getSavedValue<matjson::Value>(kCustomKey);
    if (!stored.isArray()) return;

    for (matjson::Value const& entry : stored) {
        if (m_config.custom.size() >= kMaxCustomLayers) break;
        if (!entry.isObject()) continue;

        m_config.custom.push_back(clampLayer({
            validMotion(static_cast<int>(entry["motion"].asInt().unwrapOr(0))),
            validWave(static_cast<int>(entry["wave"].asInt().unwrapOr(0))),
            static_cast<float>(entry["amount"].asDouble().unwrapOr(0.5)),
            static_cast<float>(entry["speed"].asDouble().unwrapOr(1.0)),
            static_cast<float>(entry["phase"].asDouble().unwrapOr(0.0)),
        }));
    }
}

void GradientAnimationManager::save() {
    auto mod = Mod::get();
    mod->setSavedValue<int64_t>(kTypeKey, static_cast<int64_t>(m_config.type));
    mod->setSavedValue<double>(kSpeedKey, m_config.speed);
    mod->setSavedValue<double>(kIntensityKey, m_config.intensity);
    mod->setSavedValue<bool>(kReverseKey, m_config.reverse);
    paimon::requestDeferredModSave();
}

void GradientAnimationManager::saveCustom() {
    matjson::Value stored = matjson::Value::array();

    for (GradientAnimationLayer const& layer : m_config.custom) {
        matjson::Value entry = matjson::Value{};
        entry["motion"] = static_cast<int>(layer.motion);
        entry["wave"] = static_cast<int>(layer.wave);
        entry["amount"] = layer.amount;
        entry["speed"] = layer.speed;
        entry["phase"] = layer.phase;
        stored.push(entry);
    }

    Mod::get()->setSavedValue(kCustomKey, stored);
    paimon::requestDeferredModSave();
}

GradientAnimationConfig const& GradientAnimationManager::config() const {
    return m_config;
}

bool GradientAnimationManager::isEnabled() const {
    return Mod::get()->getSavedValue<bool>(kEnabledKey, true);
}

void GradientAnimationManager::setEnabled(bool enabled) {
    Mod::get()->setSavedValue<bool>(kEnabledKey, enabled);
    paimon::requestDeferredModSave();
    refreshPrograms();
}

void GradientAnimationManager::setType(GradientAnimationType type) {
    m_config.type = validType(static_cast<int>(type));
    save();
    refreshPrograms();
}

void GradientAnimationManager::setSpeed(float speed) {
    m_config.speed = std::clamp(speed, 0.1f, 4.f);
    save();
    refreshPrograms();
}

void GradientAnimationManager::setIntensity(float intensity) {
    m_config.intensity = std::clamp(intensity, 0.f, 1.f);
    save();
    refreshPrograms();
}

void GradientAnimationManager::setReverse(bool reverse) {
    m_config.reverse = reverse;
    save();
    refreshPrograms();
}

void GradientAnimationManager::reset() {
    // The custom stack survives: it's work the user built by hand, and this
    // button is labelled "Reset", not "Delete my animation". The editor has its
    // own Clear for that.
    auto custom = std::move(m_config.custom);
    m_config = {};
    m_config.custom = std::move(custom);

    Mod::get()->setSavedValue<bool>(kEnabledKey, true);
    save();
    refreshPrograms();
}

std::vector<GradientAnimationLayer> const& GradientAnimationManager::customLayers() const {
    return m_config.custom;
}

bool GradientAnimationManager::addCustomLayer() {
    if (m_config.custom.size() >= kMaxCustomLayers) return false;

    m_config.custom.push_back({});
    saveCustom();
    refreshPrograms();
    return true;
}

bool GradientAnimationManager::duplicateCustomLayer(size_t index) {
    if (index >= m_config.custom.size()) return false;
    if (m_config.custom.size() >= kMaxCustomLayers) return false;

    m_config.custom.insert(
        m_config.custom.begin() + static_cast<ptrdiff_t>(index) + 1,
        m_config.custom[index]
    );
    saveCustom();
    refreshPrograms();
    return true;
}

void GradientAnimationManager::updateCustomLayer(size_t index, GradientAnimationLayer const& layer) {
    if (index >= m_config.custom.size()) return;

    m_config.custom[index] = clampLayer(layer);
    saveCustom();
    refreshPrograms();
}

void GradientAnimationManager::removeCustomLayer(size_t index) {
    if (index >= m_config.custom.size()) return;

    m_config.custom.erase(m_config.custom.begin() + static_cast<ptrdiff_t>(index));
    saveCustom();
    refreshPrograms();
}

size_t GradientAnimationManager::moveCustomLayer(size_t index, int delta) {
    if (index >= m_config.custom.size()) return index;

    auto target = static_cast<ptrdiff_t>(index) + delta;
    if (target < 0 || target >= static_cast<ptrdiff_t>(m_config.custom.size())) return index;

    std::swap(m_config.custom[index], m_config.custom[static_cast<size_t>(target)]);
    saveCustom();
    refreshPrograms();
    return static_cast<size_t>(target);
}

void GradientAnimationManager::setCustomLayers(std::vector<GradientAnimationLayer> layers) {
    if (layers.size() > kMaxCustomLayers) layers.resize(kMaxCustomLayers);

    m_config.custom.clear();
    for (GradientAnimationLayer const& layer : layers) {
        m_config.custom.push_back(clampLayer(layer));
    }

    saveCustom();
    refreshPrograms();
}

void GradientAnimationManager::clearCustomLayers() {
    m_config.custom.clear();
    saveCustom();
    refreshPrograms();
}

void GradientAnimationManager::track(CCGLProgram* program) {
    if (!program) return;
    m_programs.emplace(program);
    apply(program);
}

void GradientAnimationManager::refreshPrograms() {
    for (auto program : m_programs) {
        apply(program);
    }
}

void GradientAnimationManager::apply(CCGLProgram* program) const {
    if (!program) return;

    program->use();

    // Use raw GL uniform locations like GradientUtils::applyGradient does.
    // getUniformLocationForName + setUniformLocationWith* is cocos2d's
    // internal-index API: it only knows the builtin uniforms (CC_MVPMatrix,
    // CC_Time, ...) and writes into the program's uniform array, so setting a
    // custom uniform through it can clobber a different slot and render the
    // gradient blank/white.
    auto programId = program->getProgram();

    auto typeLoc = glGetUniformLocation(programId, "u_animType");
    auto speedLoc = glGetUniformLocation(programId, "u_animSpeed");
    auto intensityLoc = glGetUniformLocation(programId, "u_animIntensity");
    auto directionLoc = glGetUniformLocation(programId, "u_animDirection");

    if (typeLoc >= 0) {
        glUniform1i(
            typeLoc,
            paimon::modules::isEnabled(kAnimationModuleId)
                ? static_cast<int>(m_config.type)
                : 0
        );
    }
    if (speedLoc >= 0) {
        glUniform1f(speedLoc, m_config.speed);
    }
    if (intensityLoc >= 0) {
        glUniform1f(intensityLoc, m_config.intensity);
    }
    if (directionLoc >= 0) {
        glUniform1f(directionLoc, m_config.reverse ? -1.f : 1.f);
    }

    auto countLoc = glGetUniformLocation(programId, "u_customCount");
    auto layersLoc = glGetUniformLocation(programId, "u_customLayers");
    auto phaseLoc = glGetUniformLocation(programId, "u_customPhase");

    auto count = std::min(m_config.custom.size(), kMaxCustomLayers);

    if (countLoc >= 0) {
        glUniform1i(countLoc, static_cast<int>(count));
    }

    if (layersLoc >= 0 || phaseLoc >= 0) {
        std::array<GLfloat, kMaxCustomLayers * 4> layers{};
        std::array<GLfloat, kMaxCustomLayers> phases{};

        for (size_t i = 0; i < count; ++i) {
            auto const& layer = m_config.custom[i];
            layers[i * 4 + 0] = static_cast<GLfloat>(static_cast<int>(layer.motion));
            layers[i * 4 + 1] = static_cast<GLfloat>(static_cast<int>(layer.wave));
            layers[i * 4 + 2] = layer.amount;
            layers[i * 4 + 3] = layer.speed;
            phases[i] = layer.phase;
        }

        if (layersLoc >= 0) {
            glUniform4fv(layersLoc, static_cast<GLsizei>(kMaxCustomLayers), layers.data());
        }
        if (phaseLoc >= 0) {
            glUniform1fv(phaseLoc, static_cast<GLsizei>(kMaxCustomLayers), phases.data());
        }
    }
}

char const* GradientAnimationManager::nameFor(GradientAnimationType type) {
    switch (type) {
        case GradientAnimationType::Flow: return "Flow";
        case GradientAnimationType::Pulse: return "Pulse";
        case GradientAnimationType::Spin: return "Spin";
        case GradientAnimationType::Orbit: return "Orbit";
        case GradientAnimationType::Swing: return "Swing";
        case GradientAnimationType::Custom: return "Custom";
    }
    return "Flow";
}

char const* GradientAnimationManager::descriptionFor(GradientAnimationType type) {
    switch (type) {
        case GradientAnimationType::Flow: return "Slides the colors smoothly from side to side.";
        case GradientAnimationType::Pulse: return "Makes the gradient breathe in and out.";
        case GradientAnimationType::Spin: return "Rotates the gradient around the icon.";
        case GradientAnimationType::Orbit: return "Moves the colors in a circular path.";
        case GradientAnimationType::Swing: return "Rocks the gradient back and forth.";
        case GradientAnimationType::Custom:
            return "Your own animation, built from up to 4 stacked movements.";
    }
    return "Slides the colors smoothly from side to side.";
}

char const* GradientAnimationManager::nameFor(GradientMotion motion) {
    switch (motion) {
        case GradientMotion::SlideX: return "Slide X";
        case GradientMotion::SlideY: return "Slide Y";
        case GradientMotion::Zoom: return "Zoom";
        case GradientMotion::Rotate: return "Rotate";
        case GradientMotion::Orbit: return "Orbit";
        case GradientMotion::RippleX: return "Ripple X";
        case GradientMotion::RippleY: return "Ripple Y";
        case GradientMotion::Twist: return "Twist";
    }
    return "Slide X";
}

char const* GradientAnimationManager::descriptionFor(GradientMotion motion) {
    switch (motion) {
        case GradientMotion::SlideX:
            return "Pushes the colors left and right.";
        case GradientMotion::SlideY:
            return "Pushes the colors up and down.";
        case GradientMotion::Zoom:
            return "Pulls the colors towards the center and lets them out again.";
        case GradientMotion::Rotate:
            return "Turns the whole gradient around the center of the icon.";
        case GradientMotion::Orbit:
            return "Walks the colors around a small circle without turning them.";
        case GradientMotion::RippleX:
            return "Bends the colors sideways in a wavy line.";
        case GradientMotion::RippleY:
            return "Bends the colors up and down in a wavy line.";
        case GradientMotion::Twist:
            return "Rotates the edges more than the center, like a whirlpool.";
    }
    return "Pushes the colors left and right.";
}

char const* GradientAnimationManager::nameFor(GradientWave wave) {
    switch (wave) {
        case GradientWave::Smooth: return "Smooth";
        case GradientWave::Even: return "Even";
        case GradientWave::Loop: return "Loop";
        case GradientWave::Snap: return "Snap";
        case GradientWave::Bounce: return "Bounce";
        case GradientWave::Random: return "Random";
    }
    return "Smooth";
}

char const* GradientAnimationManager::descriptionFor(GradientWave wave) {
    switch (wave) {
        case GradientWave::Smooth:
            return "Slows down at both ends. The calmest option.";
        case GradientWave::Even:
            return "Same speed all the way, with a sharp turn at each end.";
        case GradientWave::Loop:
            return "Always the same direction and starts over. Seamless with "
                   "Rotate, Orbit and Twist; the others jump back.";
        case GradientWave::Snap:
            return "Jumps between the two ends without travelling.";
        case GradientWave::Bounce:
            return "Goes from nothing to full and back, never to the other side.";
        case GradientWave::Random:
            return "Drifts between random values. Good for glitchy looks.";
    }
    return "Slows down at both ends. The calmest option.";
}

std::vector<GradientAnimationPreset> const& GradientAnimationManager::customPresets() {
    using M = GradientMotion;
    using W = GradientWave;

    static std::vector<GradientAnimationPreset> const presets = {
        {"Wobble", "A lazy sway with a little tilt.", {
            {M::SlideX, W::Smooth, 0.40f, 1.20f, 0.00f},
            {M::Rotate, W::Smooth, 0.25f, 0.80f, 0.25f},
        }},
        {"Heartbeat", "Two-step throb, like a pulse.", {
            {M::Zoom, W::Bounce, 0.55f, 1.60f, 0.00f},
        }},
        {"Swirl", "Endless turn with the colors drifting around.", {
            {M::Rotate, W::Loop, 1.00f, 0.50f, 0.00f},
            {M::Orbit, W::Smooth, 0.30f, 0.70f, 0.50f},
        }},
        {"Glitch", "Nervous jitter in both directions.", {
            {M::SlideX, W::Random, 0.35f, 4.00f, 0.00f},
            {M::SlideY, W::Random, 0.25f, 3.20f, 0.50f},
        }},
        {"Liquid", "Slow waves crossing each other.", {
            {M::RippleX, W::Smooth, 0.60f, 0.90f, 0.00f},
            {M::RippleY, W::Smooth, 0.60f, 1.10f, 0.50f},
        }},
        {"Vortex", "A spiral pull that breathes.", {
            {M::Twist, W::Loop, 0.90f, 0.60f, 0.00f},
            {M::Zoom, W::Smooth, 0.30f, 0.50f, 0.75f},
        }},
    };

    return presets;
}

} // namespace paimon::icon_gradients
