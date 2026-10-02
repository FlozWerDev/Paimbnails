#include "RTXManager.hpp"

#include "../../../utils/EditorContext.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <type_traits>
#include <variant>

using namespace geode::prelude;

namespace paimon::rtx {

// only when a saved field flips meaning.
constexpr int kConfigSchema = 3;

void applyPreset(RTXConfig& cfg, Preset preset) {
    // pricey presets raise filter too: 8 rays alone aren't enough.
    switch (preset) {
        case Preset::Performance:
            cfg.renderScale = 0.35f; cfg.rayCount = 2; cfg.raySteps = 10;
            cfg.rayDistance = 0.24f; cfg.stepGrowth = 1.35f; cfg.bloomPasses = 3;
            cfg.denoise = 2.60f; cfg.atrousPasses = 2;
            // low temporal: with frameskip=1, 0.90 left trails.
            cfg.temporal = 0.85f; cfg.frameSkip = 1;
            break;
        case Preset::Balanced:
            cfg.renderScale = 0.50f; cfg.rayCount = 3; cfg.raySteps = 14;
            cfg.rayDistance = 0.28f; cfg.stepGrowth = 1.28f; cfg.bloomPasses = 4;
            cfg.denoise = 2.00f; cfg.atrousPasses = 3;
            cfg.temporal = 0.88f; cfg.frameSkip = 0;
            break;
        case Preset::Quality:
            cfg.renderScale = 0.65f; cfg.rayCount = 5; cfg.raySteps = 18;
            cfg.rayDistance = 0.32f; cfg.stepGrowth = 1.22f; cfg.bloomPasses = 5;
            cfg.denoise = 1.60f; cfg.atrousPasses = 3;
            cfg.temporal = 0.86f; cfg.frameSkip = 0;
            break;
        case Preset::Ultra:
            cfg.renderScale = 0.85f; cfg.rayCount = 8; cfg.raySteps = 24;
            cfg.rayDistance = 0.38f; cfg.stepGrowth = 1.18f; cfg.bloomPasses = 5;
            cfg.denoise = 1.20f; cfg.atrousPasses = 4;
            cfg.temporal = 0.84f; cfg.frameSkip = 0;
            break;
        case Preset::Custom:
            break;
    }
    cfg.preset = static_cast<int>(preset);
}

char const* presetName(int preset) {
    switch (static_cast<Preset>(preset)) {
        case Preset::Performance: return "Rendimiento";
        case Preset::Balanced:    return "Equilibrado";
        case Preset::Quality:     return "Calidad";
        case Preset::Ultra:       return "Ultra";
        case Preset::Custom:      return "Personalizado";
    }
    return "Equilibrado";
}

char const* tonemapName(int tonemap) {
    switch (static_cast<Tonemap>(tonemap)) {
        case Tonemap::None:       return "Ninguno";
        case Tonemap::Reinhard:   return "Reinhard";
        case Tonemap::ACES:       return "ACES";
        case Tonemap::Filmic:     return "Filmico";
        case Tonemap::Uncharted2: return "Uncharted 2";
    }
    return "ACES";
}

RTXManager& RTXManager::get() {
    static RTXManager instance;
    return instance;
}

std::filesystem::path RTXManager::configPath() const {
    return Mod::get()->getSaveDir() / "rtx_config.json";
}

void RTXManager::init() {
    if (m_loaded) return;
    loadConfig();
    m_loaded = true;
    log::info("[PaimonRTX] Config lista (activo={}, preset={})",
              m_config.enabled, presetName(m_config.preset));
}

// one row per persisted field; load/save/sanitize all walk this table.
struct RtxField {
    char const* key;
    std::variant<bool RTXConfig::*, int RTXConfig::*, float RTXConfig::*> member;
    float lo = 0.f;
    float hi = 0.f;
};

RtxField const kRtxFields[] = {
    {"enabled",         &RTXConfig::enabled},
    {"intensity",       &RTXConfig::intensity,       0.f,   1.f},

    {"preset",          &RTXConfig::preset,          0.f,   4.f},
    {"renderScale",     &RTXConfig::renderScale,     0.20f, 1.f},
    {"rayCount",        &RTXConfig::rayCount,        1.f,  16.f},
    {"raySteps",        &RTXConfig::raySteps,        4.f,  32.f},
    {"rayDistance",     &RTXConfig::rayDistance,     0.02f, 1.f},
    {"stepGrowth",      &RTXConfig::stepGrowth,      1.f,   1.5f},
    {"adaptive",        &RTXConfig::adaptive},
    {"targetFps",       &RTXConfig::targetFps,      30.f, 360.f},
    {"frameSkip",       &RTXConfig::frameSkip,       0.f,   3.f},

    {"hdrRange",        &RTXConfig::hdrRange,        1.f,  16.f},

    {"giStrength",      &RTXConfig::giStrength,      0.f,   4.f},
    {"giSaturation",    &RTXConfig::giSaturation,    0.f,   2.f},
    {"lightThreshold",  &RTXConfig::lightThreshold,  0.f,   1.f},
    {"lightRange",      &RTXConfig::lightRange,      0.01f, 1.f},
    {"bounceFalloff",   &RTXConfig::bounceFalloff,   0.1f, 10.f},
    {"normalStrength",  &RTXConfig::normalStrength,  0.5f, 24.f},
    {"thickness",       &RTXConfig::thickness,       0.01f, 2.f},

    {"aoStrength",      &RTXConfig::aoStrength,      0.f,   1.f},
    {"aoRadius",        &RTXConfig::aoRadius,        0.01f, 1.f},
    {"aoPower",         &RTXConfig::aoPower,         0.2f,  4.f},

    {"reflectStrength", &RTXConfig::reflectStrength, 0.f,   2.f},
    {"reflectRoughness",&RTXConfig::reflectRoughness,0.f,   1.f},
    {"reflectFresnel",  &RTXConfig::reflectFresnel,  0.f,   1.f},
    {"reflectFade",     &RTXConfig::reflectFade,     0.01f, 1.f},

    {"bloomStrength",   &RTXConfig::bloomStrength,   0.f,   3.f},
    {"bloomThreshold",  &RTXConfig::bloomThreshold,  0.f,   1.f},
    {"bloomSoftKnee",   &RTXConfig::bloomSoftKnee,   0.f,   1.f},
    {"bloomRadius",     &RTXConfig::bloomRadius,     0.5f,  6.f},
    {"bloomBlend",      &RTXConfig::bloomBlend,      0.f,   1.f},
    {"bloomAnamorphic", &RTXConfig::bloomAnamorphic, 0.f,   1.f},
    {"bloomPasses",     &RTXConfig::bloomPasses,     1.f,   5.f},

    {"godRayStrength",  &RTXConfig::godRayStrength,  0.f,   2.f},
    {"godRayDecay",     &RTXConfig::godRayDecay,     0.5f,  0.995f},
    {"godRayDensity",   &RTXConfig::godRayDensity,   0.05f, 2.f},
    {"godRayX",         &RTXConfig::godRayX,         0.f,   1.f},
    {"godRayY",         &RTXConfig::godRayY,         0.f,   1.f},

    {"denoise",         &RTXConfig::denoise,         0.f,   4.f},
    {"atrousPasses",    &RTXConfig::atrousPasses,    0.f,   5.f},
    {"temporal",        &RTXConfig::temporal,        0.f,   0.97f},
    {"ghostClamp",      &RTXConfig::ghostClamp},
    {"clampSigma",      &RTXConfig::clampSigma,      0.f,   3.f},

    {"tonemap",         &RTXConfig::tonemap,         0.f,   4.f},
    {"exposure",        &RTXConfig::exposure,       -2.f,   2.f},
    {"contrast",        &RTXConfig::contrast,        0.5f,  2.f},
    {"saturation",      &RTXConfig::saturation,      0.f,   2.f},
    {"temperature",     &RTXConfig::temperature,    -1.f,   1.f},
    {"tint",            &RTXConfig::tint,           -1.f,   1.f},
    {"gamma",           &RTXConfig::gamma,           0.5f,  2.f},

    {"adaptEnabled",    &RTXConfig::adaptEnabled},
    {"adaptKey",        &RTXConfig::adaptKey,        0.04f, 0.60f},
    {"adaptSpeed",      &RTXConfig::adaptSpeed,      0.1f,  6.f},

    {"chromatic",       &RTXConfig::chromatic,       0.f,   2.f},
    {"vignette",        &RTXConfig::vignette,        0.f,   2.f},
    {"grain",           &RTXConfig::grain,           0.f,   1.f},
    {"sharpen",         &RTXConfig::sharpen,         0.f,   2.f},

    {"inGameplay",      &RTXConfig::inGameplay},
    {"inEditor",        &RTXConfig::inEditor},
    {"inMenus",         &RTXConfig::inMenus},
    {"skipWhenPaused",  &RTXConfig::skipWhenPaused},
};

void loadFields(RTXConfig& c, matjson::Value& j) {
    for (auto const& f : kRtxFields) {
        std::visit([&]<typename M>(M RTXConfig::* ptr) {
            if constexpr (std::is_same_v<M, bool>) {
                c.*ptr = j[f.key].asBool().unwrapOr(c.*ptr);
            } else if constexpr (std::is_same_v<M, int>) {
                c.*ptr = j[f.key].asInt().unwrapOr(c.*ptr);
            } else {
                c.*ptr = static_cast<float>(j[f.key].asDouble().unwrapOr(static_cast<double>(c.*ptr)));
            }
        }, f.member);
    }
}

void saveFields(RTXConfig const& c, matjson::Value& j) {
    for (auto const& f : kRtxFields) {
        std::visit([&]<typename M>(M RTXConfig::* ptr) { j[f.key] = c.*ptr; }, f.member);
    }
}

void sanitizeFields(RTXConfig& c) {
    RTXConfig const defaults{};
    for (auto const& f : kRtxFields) {
        std::visit([&]<typename M>(M RTXConfig::* ptr) {
            if constexpr (std::is_same_v<M, bool>) {
                return;
            } else if constexpr (std::is_same_v<M, int>) {
                c.*ptr = std::clamp(c.*ptr, static_cast<int>(f.lo), static_cast<int>(f.hi));
            } else {
                c.*ptr = std::isfinite(c.*ptr) ? std::clamp(c.*ptr, f.lo, f.hi) : defaults.*ptr;
            }
        }, f.member);
    }
}

void RTXManager::loadConfig() {
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
#if defined(GEODE_IS_MOBILE)
        // fresh phone install: start at performance so first enable doesn't melt the gpu.
        applyPreset(m_config, Preset::Performance);
#endif
        return;
    }

    auto rawRes = geode::utils::file::readString(path);
    if (!rawRes) {
        log::warn("[PaimonRTX] No se pudo leer la config: {}", rawRes.unwrapErr());
        return;
    }

    auto res = matjson::parse(rawRes.unwrap());
    if (res.isErr()) {
        log::warn("[PaimonRTX] JSON de config invalido: {}", res.unwrapErr());
        return;
    }
    auto j = res.unwrap();

    auto getInt  = [&](char const* k, int d)   { return j[k].asInt().unwrapOr(d); };

    loadFields(m_config, j);

    sanitize();

    int const schema = getInt("schema", 1);

    RTXConfig& c = m_config;

    // schema 1 inverted the filter: reapply the saved preset.
    if (schema < 2 && c.preset != static_cast<int>(Preset::Custom)) {
        applyPreset(c, static_cast<Preset>(c.preset));
    }

    // schema 3 moved to linear light: restore the default look.
    if (schema < 3) {
        RTXConfig const fresh{};
        c.giStrength      = fresh.giStrength;
        c.aoStrength      = fresh.aoStrength;
        c.reflectStrength = fresh.reflectStrength;
        c.bloomStrength   = fresh.bloomStrength;
        c.bloomThreshold  = fresh.bloomThreshold;
        c.bloomRadius     = fresh.bloomRadius;
        c.godRayStrength  = fresh.godRayStrength;
        c.exposure        = fresh.exposure;
        c.contrast        = fresh.contrast;
        c.saturation      = fresh.saturation;
        c.temperature     = fresh.temperature;
        c.gamma           = fresh.gamma;
        c.chromatic       = fresh.chromatic;
        c.vignette        = fresh.vignette;
        c.grain           = fresh.grain;
        c.sharpen         = fresh.sharpen;
    }

    if (schema < kConfigSchema) {
        saveConfig();
        log::info("[PaimonRTX] Config migrada del esquema {} al {} (preset {})",
                  schema, kConfigSchema, presetName(c.preset));
    }
}

void RTXManager::saveConfig() {
    sanitize();

    RTXConfig const& c = m_config;
    matjson::Value j;
    j["schema"]           = kConfigSchema;
    saveFields(c, j);

    auto path = configPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("[PaimonRTX] No se pudo escribir la config en {}",
                   geode::utils::string::pathToString(path));
        return;
    }
    auto txt = j.dump();
    out.write(txt.data(), static_cast<std::streamsize>(txt.size()));
}

void RTXManager::resetToDefaults() {
    bool const wasEnabled = m_config.enabled;
    m_config = RTXConfig{};
    m_config.enabled = wasEnabled;
    saveConfig();
}

void RTXManager::sanitize() {
    sanitizeFields(m_config);
}

bool RTXManager::isEnabled() const {
    return m_config.enabled;
}

void RTXManager::setEnabled(bool enabled) {
    if (m_config.enabled == enabled) return;
    m_config.enabled = enabled;
    saveConfig();
}

bool RTXManager::shouldRender() const {
    if (!m_config.enabled) return false;
    if (m_config.intensity <= 0.001f) return false;

    if (paimon::isEditorScene()) return m_config.inEditor;

    if (auto* pl = PlayLayer::get()) {
        if (m_config.skipWhenPaused && pl->m_isPaused) return false;
        return m_config.inGameplay;
    }

    return m_config.inMenus;
}

} // namespace paimon::rtx
