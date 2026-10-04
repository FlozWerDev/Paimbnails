#include "GradientAnimationManager.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/JsonHelper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

using namespace geode::prelude;

namespace paimon::icon_gradients {

namespace {

constexpr char const* kEnabledKey = "gradient-animation-enabled";
constexpr char const* kTypeKey = "gradient-animation-type";
constexpr char const* kSpeedKey = "gradient-animation-speed";
constexpr char const* kIntensityKey = "gradient-animation-intensity";
constexpr char const* kReverseKey = "gradient-animation-reverse";
constexpr char const* kCustomKey = "gradient-animation-custom";
constexpr char const* kPhaseKey = "gradient-animation-phase";
constexpr char const* kPingPongKey = "gradient-animation-pingpong";
constexpr char const* kEasingKey = "gradient-animation-easing";

float bounded(double value, float fallback, float low, float high) {
    return std::isfinite(value)
        ? static_cast<float>(std::clamp(value, static_cast<double>(low), static_cast<double>(high)))
        : fallback;
}

GradientAnimationType validType(int64_t value) {
    if (value < static_cast<int>(GradientAnimationType::Flow)
        || value > kLastAnimationType) {
        return GradientAnimationType::Flow;
    }
    return static_cast<GradientAnimationType>(value);
}

GradientEasing validEasing(int64_t value) {
    if (value < 0 || value >= kGradientEasingCount) return GradientEasing::Smooth;
    return static_cast<GradientEasing>(value);
}

// the easing choice maps onto the per-layer wave of a recipe. shader-native
// types never reach this.
GradientWave waveForEasing(GradientEasing easing, bool pingPong) {
    if (pingPong) return GradientWave::Bounce;
    switch (easing) {
        case GradientEasing::Smooth: return GradientWave::Smooth;
        case GradientEasing::Linear: return GradientWave::Even;
        case GradientEasing::Snap: return GradientWave::Snap;
        case GradientEasing::Soft: return GradientWave::Smooth;
        case GradientEasing::Sharp: return GradientWave::Even;
    }
    return GradientWave::Smooth;
}

GradientMotion validMotion(int64_t value) {
    if (value < 0 || value >= kGradientMotionCount) return GradientMotion::SlideX;
    return static_cast<GradientMotion>(value);
}

GradientWave validWave(int64_t value) {
    if (value < 0 || value >= kGradientWaveCount) return GradientWave::Smooth;
    return static_cast<GradientWave>(value);
}

GradientAnimationLayer clampLayer(GradientAnimationLayer layer) {
    layer.motion = validMotion(static_cast<int>(layer.motion));
    layer.wave = validWave(static_cast<int>(layer.wave));
    layer.amount = bounded(layer.amount, 0.5f, 0.f, 1.f);
    layer.speed = bounded(layer.speed, 1.f, kLayerSpeedMin, kLayerSpeedMax);
    layer.phase = bounded(layer.phase, 0.f, 0.f, 1.f);
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
    m_config.type = validType(paimon::json::integerOr<int64_t>(
        mod->getSavedValue<matjson::Value>(kTypeKey), static_cast<int64_t>(GradientAnimationType::Flow)
    ));
    m_config.speed = bounded(
        mod->getSavedValue<double>(kSpeedKey, 1.0), 1.f, 0.1f, 4.f
    );
    m_config.intensity = bounded(
        mod->getSavedValue<double>(kIntensityKey, 0.6), 0.6f, 0.f, 1.f
    );
    m_config.reverse = mod->getSavedValue<bool>(kReverseKey, false);
    m_config.phaseOffset = bounded(
        mod->getSavedValue<double>(kPhaseKey, 0.0), 0.f, 0.f, 1.f
    );
    m_config.pingPong = mod->getSavedValue<bool>(kPingPongKey, false);
    m_config.easing = validEasing(paimon::json::integerOr<int64_t>(
        mod->getSavedValue<matjson::Value>(kEasingKey), 0
    ));

    m_config.custom.clear();

    matjson::Value stored = mod->getSavedValue<matjson::Value>(kCustomKey);
    if (!stored.isArray()) return;

    for (matjson::Value const& entry : stored) {
        if (m_config.custom.size() >= kMaxCustomLayers) break;
        if (!entry.isObject()) continue;

        m_config.custom.push_back(clampLayer({
            validMotion(paimon::json::integerOr<int64_t>(entry["motion"])),
            validWave(paimon::json::integerOr<int64_t>(entry["wave"])),
            bounded(entry["amount"].asDouble().unwrapOr(0.5), 0.5f, 0.f, 1.f),
            bounded(entry["speed"].asDouble().unwrapOr(1.0), 1.f, kLayerSpeedMin, kLayerSpeedMax),
            bounded(entry["phase"].asDouble().unwrapOr(0.0), 0.f, 0.f, 1.f),
        }));
    }
}

void GradientAnimationManager::save() {
    auto mod = Mod::get();
    mod->setSavedValue<int64_t>(kTypeKey, static_cast<int64_t>(m_config.type));
    mod->setSavedValue<double>(kSpeedKey, m_config.speed);
    mod->setSavedValue<double>(kIntensityKey, m_config.intensity);
    mod->setSavedValue<bool>(kReverseKey, m_config.reverse);
    mod->setSavedValue<double>(kPhaseKey, m_config.phaseOffset);
    mod->setSavedValue<bool>(kPingPongKey, m_config.pingPong);
    mod->setSavedValue<int64_t>(kEasingKey, static_cast<int64_t>(m_config.easing));
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
    m_config.speed = bounded(speed, 1.f, 0.1f, 4.f);
    save();
    refreshPrograms();
}

void GradientAnimationManager::setIntensity(float intensity) {
    m_config.intensity = bounded(intensity, 0.6f, 0.f, 1.f);
    save();
    refreshPrograms();
}

void GradientAnimationManager::setReverse(bool reverse) {
    m_config.reverse = reverse;
    save();
    refreshPrograms();
}

void GradientAnimationManager::setPhaseOffset(float phase) {
    m_config.phaseOffset = bounded(phase, 0.f, 0.f, 1.f);
    save();
    refreshPrograms();
}

void GradientAnimationManager::setPingPong(bool pingPong) {
    m_config.pingPong = pingPong;
    save();
    refreshPrograms();
}

void GradientAnimationManager::setEasing(GradientEasing easing) {
    m_config.easing = validEasing(static_cast<int>(easing));
    save();
    refreshPrograms();
}

void GradientAnimationManager::randomize() {
    static std::mt19937 rng{std::random_device{}()};

    // custom needs a hand-built stack to show anything, so keep it out of the roll.
    std::vector<GradientAnimationType> pool;
    for (auto type : builtInTypes()) {
        if (type != GradientAnimationType::Custom) pool.push_back(type);
    }

    std::uniform_int_distribution<size_t> typePick(0, pool.size() - 1);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    std::uniform_real_distribution<float> speed(0.4f, 2.2f);
    std::uniform_int_distribution<int> easePick(0, kGradientEasingCount - 1);

    m_config.type = pool[typePick(rng)];
    m_config.speed = bounded(speed(rng), 1.f, 0.1f, 4.f);
    m_config.intensity = bounded(0.35f + unit(rng) * 0.6f, 0.6f, 0.f, 1.f);
    m_config.reverse = unit(rng) < 0.5f;
    m_config.phaseOffset = unit(rng);
    m_config.pingPong = unit(rng) < 0.4f;
    m_config.easing = static_cast<GradientEasing>(easePick(rng));

    save();
    refreshPrograms();
}

void GradientAnimationManager::reset() {
    // keep the custom stack when resetting playback settings.
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

    auto target = static_cast<int64_t>(index) + delta;
    if (target < 0 || target >= static_cast<int64_t>(m_config.custom.size())) return index;

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
    if (!program || m_stopped || paimon::isRuntimeShuttingDown()) return;
    auto found = m_programs.find(program);
    if (found != m_programs.end() && !found->second.lock()) m_programs.erase(found);
    m_programs.try_emplace(program, program);
    apply(program);
}

void GradientAnimationManager::refreshPrograms() {
    if (m_stopped || paimon::isRuntimeShuttingDown()) return;
    for (auto it = m_programs.begin(); it != m_programs.end();) {
        if (auto program = it->second.lock()) {
            apply(program.data());
            ++it;
        } else {
            it = m_programs.erase(it);
        }
    }
}

void GradientAnimationManager::shutdown() {
    m_stopped = true;
    m_programs.clear();
}

void GradientAnimationManager::apply(CCGLProgram* program) const {
    if (!program) return;

    program->use();

    // raw gl locations like applygradient: the getuniformlocationforname api only
    // knows the builtin uniforms, so a custom one through it can clobber another slot.
    auto programId = program->getProgram();

    auto typeLoc = glGetUniformLocation(programId, "u_animType");
    auto speedLoc = glGetUniformLocation(programId, "u_animSpeed");
    auto intensityLoc = glGetUniformLocation(programId, "u_animIntensity");
    auto directionLoc = glGetUniformLocation(programId, "u_animDirection");

    bool enabled = paimon::modules::isEnabled(kAnimationModuleId);
    int rawType = static_cast<int>(m_config.type);
    bool recipe = rawType >= kFirstBuiltRecipeType;

    // recipe presets have no shader branch, so they ride the custom-layer engine
    // (branch 6) with a generated stack. the first six types stay shader-native.
    std::vector<GradientAnimationLayer> feed =
        recipe ? recipeFor(m_config.type) : m_config.custom;

    if (recipe) {
        for (auto& layer : feed) {
            // loop and random carry the preset's identity (rainbow, glitch,
            // twinkle); only reshape the plain travel waves.
            bool shaping = layer.wave != GradientWave::Loop
                && layer.wave != GradientWave::Random;
            if (shaping) layer.wave = waveForEasing(m_config.easing, m_config.pingPong);
            layer.phase = std::fmod(layer.phase + m_config.phaseOffset, 1.f);
        }
    }

    if (typeLoc >= 0) {
        int shaderType = enabled ? (recipe ? static_cast<int>(GradientAnimationType::Custom) : rawType) : 0;
        glUniform1i(typeLoc, shaderType);
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

    auto count = std::min(feed.size(), kMaxCustomLayers);

    if (countLoc >= 0) {
        glUniform1i(countLoc, static_cast<int>(count));
    }

    if (layersLoc >= 0 || phaseLoc >= 0) {
        std::array<GLfloat, kMaxCustomLayers * 4> layers{};
        std::array<GLfloat, kMaxCustomLayers> phases{};

        for (size_t i = 0; i < count; ++i) {
            auto const& layer = feed[i];
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
        case GradientAnimationType::Wave: return "Wave";
        case GradientAnimationType::Breathe: return "Breathe";
        case GradientAnimationType::Rainbow: return "Rainbow";
        case GradientAnimationType::Shimmer: return "Shimmer";
        case GradientAnimationType::Bounce: return "Bounce";
        case GradientAnimationType::Zigzag: return "Zigzag";
        case GradientAnimationType::Heartbeat: return "Heartbeat";
        case GradientAnimationType::Strobe: return "Strobe";
        case GradientAnimationType::PingPong: return "Ping-pong";
        case GradientAnimationType::Spiral: return "Spiral";
        case GradientAnimationType::Twinkle: return "Twinkle";
        case GradientAnimationType::GlitchFx: return "Glitch";
        case GradientAnimationType::Ripple: return "Ripple";
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
        case GradientAnimationType::Wave:
            return "Rolls the colors in a soft horizontal wave.";
        case GradientAnimationType::Breathe:
            return "Slow zoom in and out, calm and even.";
        case GradientAnimationType::Rainbow:
            return "Keeps turning in one direction so the colors cycle forever.";
        case GradientAnimationType::Shimmer:
            return "A quick sheen sweep sliding across the icon.";
        case GradientAnimationType::Bounce:
            return "Drops and springs back like it is bouncing.";
        case GradientAnimationType::Zigzag:
            return "Snaps side to side in sharp zigzag steps.";
        case GradientAnimationType::Heartbeat:
            return "Two quick throbs, then a rest. Like a pulse.";
        case GradientAnimationType::Strobe:
            return "Hard on-off jumps for a flashing look.";
        case GradientAnimationType::PingPong:
            return "Slides one way, then back, without wrapping around.";
        case GradientAnimationType::Spiral:
            return "A whirlpool twist that keeps spinning.";
        case GradientAnimationType::Twinkle:
            return "Tiny random sparkle, barely moving.";
        case GradientAnimationType::GlitchFx:
            return "Nervous jitter in both directions, like a glitch.";
        case GradientAnimationType::Ripple:
            return "Crossing ripples, like a drop hitting water.";
    }
    return "Slides the colors smoothly from side to side.";
}

char const* GradientAnimationManager::nameFor(GradientEasing easing) {
    switch (easing) {
        case GradientEasing::Smooth: return "Smooth";
        case GradientEasing::Linear: return "Linear";
        case GradientEasing::Snap: return "Snap";
        case GradientEasing::Soft: return "Soft";
        case GradientEasing::Sharp: return "Sharp";
    }
    return "Smooth";
}

std::vector<GradientAnimationType> const& GradientAnimationManager::builtInTypes() {
    static std::vector<GradientAnimationType> const types = {
        GradientAnimationType::Flow,
        GradientAnimationType::Pulse,
        GradientAnimationType::Spin,
        GradientAnimationType::Orbit,
        GradientAnimationType::Swing,
        GradientAnimationType::Wave,
        GradientAnimationType::Breathe,
        GradientAnimationType::Rainbow,
        GradientAnimationType::Shimmer,
        GradientAnimationType::Bounce,
        GradientAnimationType::Zigzag,
        GradientAnimationType::Heartbeat,
        GradientAnimationType::Strobe,
        GradientAnimationType::PingPong,
        GradientAnimationType::Spiral,
        GradientAnimationType::Twinkle,
        GradientAnimationType::GlitchFx,
        GradientAnimationType::Ripple,
        GradientAnimationType::Custom,
    };
    return types;
}

std::vector<GradientAnimationLayer> GradientAnimationManager::recipeFor(GradientAnimationType type) {
    using M = GradientMotion;
    using W = GradientWave;

    switch (type) {
        case GradientAnimationType::Wave:
            return {{M::RippleX, W::Smooth, 0.75f, 0.9f, 0.f}};
        case GradientAnimationType::Breathe:
            return {{M::Zoom, W::Smooth, 0.6f, 0.55f, 0.f}};
        case GradientAnimationType::Rainbow:
            return {{M::Rotate, W::Loop, 1.f, 0.6f, 0.f}};
        case GradientAnimationType::Shimmer:
            return {{M::SlideX, W::Loop, 0.5f, 1.8f, 0.f}};
        case GradientAnimationType::Bounce:
            return {{M::SlideY, W::Bounce, 0.7f, 1.4f, 0.f}};
        case GradientAnimationType::Zigzag:
            return {
                {M::SlideX, W::Snap, 0.55f, 1.6f, 0.f},
                {M::SlideY, W::Snap, 0.35f, 1.6f, 0.5f},
            };
        case GradientAnimationType::Heartbeat:
            return {{M::Zoom, W::Bounce, 0.6f, 1.7f, 0.f}};
        case GradientAnimationType::Strobe:
            return {{M::Zoom, W::Snap, 0.5f, 2.4f, 0.f}};
        case GradientAnimationType::PingPong:
            return {{M::SlideX, W::Even, 0.7f, 1.f, 0.f}};
        case GradientAnimationType::Spiral:
            return {
                {M::Twist, W::Loop, 0.9f, 0.7f, 0.f},
                {M::Zoom, W::Smooth, 0.3f, 0.5f, 0.5f},
            };
        case GradientAnimationType::Twinkle:
            return {{M::Zoom, W::Random, 0.3f, 2.6f, 0.f}};
        case GradientAnimationType::GlitchFx:
            return {
                {M::SlideX, W::Random, 0.4f, 4.f, 0.f},
                {M::SlideY, W::Random, 0.3f, 3.4f, 0.5f},
            };
        case GradientAnimationType::Ripple:
            return {
                {M::RippleX, W::Smooth, 0.6f, 1.f, 0.f},
                {M::RippleY, W::Smooth, 0.6f, 1.2f, 0.5f},
            };
        default:
            return {};
    }
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
