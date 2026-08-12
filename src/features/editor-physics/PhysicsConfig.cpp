#include "PhysicsConfig.hpp"

#include <Geode/loader/Mod.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

constexpr char const* kGravity = "editor-physics-gravity";
constexpr char const* kRestitution = "editor-physics-restitution";
constexpr char const* kFriction = "editor-physics-friction";
constexpr char const* kAirDrag = "editor-physics-air-drag";
constexpr char const* kDuration = "editor-physics-duration";
constexpr char const* kVelocityX = "editor-physics-velocity-x";
constexpr char const* kVelocityY = "editor-physics-velocity-y";
constexpr char const* kSpin = "editor-physics-spin";
constexpr char const* kSampleRate = "editor-physics-sample-rate";

LabConfig sanitize(LabConfig config) {
    config.gravity = std::clamp(config.gravity, -2000.f, 2000.f);
    config.restitution = std::clamp(config.restitution, 0.f, 1.f);
    config.friction = std::clamp(config.friction, 0.f, 1.f);
    config.airDrag = std::clamp(config.airDrag, 0.f, 1.f);
    config.duration = std::clamp(config.duration, 0.5f, 10.f);
    config.velocityX = std::clamp(config.velocityX, -1500.f, 1500.f);
    config.velocityY = std::clamp(config.velocityY, -1500.f, 1500.f);
    config.spinDegrees = std::clamp(config.spinDegrees, -720.f, 720.f);
    config.sampleRate = std::clamp(config.sampleRate, 10, 40);
    return config;
}

} // namespace

LabConfig loadConfig() {
    auto* mod = Mod::get();
    LabConfig config;
    if (!mod) return config;
    config.gravity = mod->getSavedValue<float>(kGravity, config.gravity);
    config.restitution = mod->getSavedValue<float>(kRestitution, config.restitution);
    config.friction = mod->getSavedValue<float>(kFriction, config.friction);
    config.airDrag = mod->getSavedValue<float>(kAirDrag, config.airDrag);
    config.duration = mod->getSavedValue<float>(kDuration, config.duration);
    config.velocityX = mod->getSavedValue<float>(kVelocityX, config.velocityX);
    config.velocityY = mod->getSavedValue<float>(kVelocityY, config.velocityY);
    config.spinDegrees = mod->getSavedValue<float>(kSpin, config.spinDegrees);
    config.sampleRate = mod->getSavedValue<int>(kSampleRate, config.sampleRate);
    return sanitize(config);
}

void saveConfig(LabConfig const& rawConfig) {
    auto* mod = Mod::get();
    if (!mod) return;
    auto const config = sanitize(rawConfig);
    mod->setSavedValue(kGravity, config.gravity);
    mod->setSavedValue(kRestitution, config.restitution);
    mod->setSavedValue(kFriction, config.friction);
    mod->setSavedValue(kAirDrag, config.airDrag);
    mod->setSavedValue(kDuration, config.duration);
    mod->setSavedValue(kVelocityX, config.velocityX);
    mod->setSavedValue(kVelocityY, config.velocityY);
    mod->setSavedValue(kSpin, config.spinDegrees);
    mod->setSavedValue(kSampleRate, config.sampleRate);
}

SimulationOptions simulationOptions(LabConfig const& rawConfig) {
    auto const config = sanitize(rawConfig);
    SimulationOptions options;
    options.gravity = {0.f, config.gravity};
    options.duration = config.duration;
    options.airDrag = config.airDrag;
    options.fixedRate = 120;
    options.sampleRate = config.sampleRate;
    options.solverIterations = 5;
    return options;
}

} // namespace paimon::editorphysics
