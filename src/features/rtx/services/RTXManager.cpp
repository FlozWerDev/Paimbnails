#include "RTXManager.hpp"

#include "../../../utils/EditorContext.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <fstream>

using namespace geode::prelude;

namespace paimon::rtx {

// Solo si cambia el sentido de un campo guardado.
constexpr int kConfigSchema = 3;

void applyPreset(RTXConfig& cfg, Preset preset) {
    // El preset caro sube filtro a la vez: 8 rayos no bastan solos.
    switch (preset) {
        case Preset::Performance:
            cfg.renderScale = 0.35f; cfg.rayCount = 2; cfg.raySteps = 10;
            cfg.rayDistance = 0.24f; cfg.stepGrowth = 1.35f; cfg.bloomPasses = 3;
            cfg.denoise = 2.60f; cfg.atrousPasses = 2;
            // Temporal bajo: con frameSkip=1 el 0.90 dejaba estelas.
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

void RTXManager::loadConfig() {
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
#if defined(GEODE_IS_MOBILE)
        // Fresh install on a phone: start from Performance so the first enable
        // doesn't melt the GPU (HDR still probes at runtime, LDR fallback).
        applyPreset(m_config, Preset::Performance);
#endif
        return;
    }

    auto rawRes = file::readString(path);
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

    auto getBool = [&](char const* k, bool d)  { return j[k].asBool().unwrapOr(d); };
    auto getInt  = [&](char const* k, int d)   { return j[k].asInt().unwrapOr(d); };
    auto getFlt  = [&](char const* k, float d) {
        return static_cast<float>(j[k].asDouble().unwrapOr(static_cast<double>(d)));
    };

    RTXConfig& c = m_config;
    c.enabled          = getBool("enabled", c.enabled);
    c.intensity        = getFlt("intensity", c.intensity);

    c.preset           = getInt("preset", c.preset);
    c.renderScale      = getFlt("renderScale", c.renderScale);
    c.rayCount         = getInt("rayCount", c.rayCount);
    c.raySteps         = getInt("raySteps", c.raySteps);
    c.rayDistance      = getFlt("rayDistance", c.rayDistance);
    c.stepGrowth       = getFlt("stepGrowth", c.stepGrowth);
    c.adaptive         = getBool("adaptive", c.adaptive);
    c.targetFps        = getInt("targetFps", c.targetFps);
    c.frameSkip        = getInt("frameSkip", c.frameSkip);

    c.hdrRange         = getFlt("hdrRange", c.hdrRange);

    c.giStrength       = getFlt("giStrength", c.giStrength);
    c.giSaturation     = getFlt("giSaturation", c.giSaturation);
    c.lightThreshold   = getFlt("lightThreshold", c.lightThreshold);
    c.lightRange       = getFlt("lightRange", c.lightRange);
    c.bounceFalloff    = getFlt("bounceFalloff", c.bounceFalloff);
    c.normalStrength   = getFlt("normalStrength", c.normalStrength);
    c.thickness        = getFlt("thickness", c.thickness);

    c.aoStrength       = getFlt("aoStrength", c.aoStrength);
    c.aoRadius         = getFlt("aoRadius", c.aoRadius);
    c.aoPower          = getFlt("aoPower", c.aoPower);

    c.reflectStrength  = getFlt("reflectStrength", c.reflectStrength);
    c.reflectRoughness = getFlt("reflectRoughness", c.reflectRoughness);
    c.reflectFresnel   = getFlt("reflectFresnel", c.reflectFresnel);
    c.reflectFade      = getFlt("reflectFade", c.reflectFade);

    c.bloomStrength    = getFlt("bloomStrength", c.bloomStrength);
    c.bloomThreshold   = getFlt("bloomThreshold", c.bloomThreshold);
    c.bloomSoftKnee    = getFlt("bloomSoftKnee", c.bloomSoftKnee);
    c.bloomRadius      = getFlt("bloomRadius", c.bloomRadius);
    c.bloomBlend       = getFlt("bloomBlend", c.bloomBlend);
    c.bloomAnamorphic  = getFlt("bloomAnamorphic", c.bloomAnamorphic);
    c.bloomPasses      = getInt("bloomPasses", c.bloomPasses);

    c.godRayStrength   = getFlt("godRayStrength", c.godRayStrength);
    c.godRayDecay      = getFlt("godRayDecay", c.godRayDecay);
    c.godRayDensity    = getFlt("godRayDensity", c.godRayDensity);
    c.godRayX          = getFlt("godRayX", c.godRayX);
    c.godRayY          = getFlt("godRayY", c.godRayY);

    c.denoise          = getFlt("denoise", c.denoise);
    c.atrousPasses     = getInt("atrousPasses", c.atrousPasses);
    c.temporal         = getFlt("temporal", c.temporal);
    c.ghostClamp       = getBool("ghostClamp", c.ghostClamp);
    c.clampSigma       = getFlt("clampSigma", c.clampSigma);

    c.tonemap          = getInt("tonemap", c.tonemap);
    c.exposure         = getFlt("exposure", c.exposure);
    c.contrast         = getFlt("contrast", c.contrast);
    c.saturation       = getFlt("saturation", c.saturation);
    c.temperature      = getFlt("temperature", c.temperature);
    c.tint             = getFlt("tint", c.tint);
    c.gamma            = getFlt("gamma", c.gamma);

    c.adaptEnabled     = getBool("adaptEnabled", c.adaptEnabled);
    c.adaptKey         = getFlt("adaptKey", c.adaptKey);
    c.adaptSpeed       = getFlt("adaptSpeed", c.adaptSpeed);

    c.chromatic        = getFlt("chromatic", c.chromatic);
    c.vignette         = getFlt("vignette", c.vignette);
    c.grain            = getFlt("grain", c.grain);
    c.sharpen          = getFlt("sharpen", c.sharpen);

    c.inGameplay       = getBool("inGameplay", c.inGameplay);
    c.inEditor         = getBool("inEditor", c.inEditor);
    c.inMenus          = getBool("inMenus", c.inMenus);
    c.skipWhenPaused   = getBool("skipWhenPaused", c.skipWhenPaused);

    sanitize();

    int const schema = getInt("schema", 1);

    // Esquema 1 invertia el filtro: se reaplica el preset guardado.
    if (schema < 2 && c.preset != static_cast<int>(Preset::Custom)) {
        applyPreset(c, static_cast<Preset>(c.preset));
    }

    // Esquema 3 paso a luz lineal: se restaura el look por defecto.
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
    j["enabled"]          = c.enabled;
    j["intensity"]        = c.intensity;

    j["preset"]           = c.preset;
    j["renderScale"]      = c.renderScale;
    j["rayCount"]         = c.rayCount;
    j["raySteps"]         = c.raySteps;
    j["rayDistance"]      = c.rayDistance;
    j["stepGrowth"]       = c.stepGrowth;
    j["adaptive"]         = c.adaptive;
    j["targetFps"]        = c.targetFps;
    j["frameSkip"]        = c.frameSkip;

    j["hdrRange"]         = c.hdrRange;

    j["giStrength"]       = c.giStrength;
    j["giSaturation"]     = c.giSaturation;
    j["lightThreshold"]   = c.lightThreshold;
    j["lightRange"]       = c.lightRange;
    j["bounceFalloff"]    = c.bounceFalloff;
    j["normalStrength"]   = c.normalStrength;
    j["thickness"]        = c.thickness;

    j["aoStrength"]       = c.aoStrength;
    j["aoRadius"]         = c.aoRadius;
    j["aoPower"]          = c.aoPower;

    j["reflectStrength"]  = c.reflectStrength;
    j["reflectRoughness"] = c.reflectRoughness;
    j["reflectFresnel"]   = c.reflectFresnel;
    j["reflectFade"]      = c.reflectFade;

    j["bloomStrength"]    = c.bloomStrength;
    j["bloomThreshold"]   = c.bloomThreshold;
    j["bloomSoftKnee"]    = c.bloomSoftKnee;
    j["bloomRadius"]      = c.bloomRadius;
    j["bloomBlend"]       = c.bloomBlend;
    j["bloomAnamorphic"]  = c.bloomAnamorphic;
    j["bloomPasses"]      = c.bloomPasses;

    j["godRayStrength"]   = c.godRayStrength;
    j["godRayDecay"]      = c.godRayDecay;
    j["godRayDensity"]    = c.godRayDensity;
    j["godRayX"]          = c.godRayX;
    j["godRayY"]          = c.godRayY;

    j["denoise"]          = c.denoise;
    j["atrousPasses"]     = c.atrousPasses;
    j["temporal"]         = c.temporal;
    j["ghostClamp"]       = c.ghostClamp;
    j["clampSigma"]       = c.clampSigma;

    j["tonemap"]          = c.tonemap;
    j["exposure"]         = c.exposure;
    j["contrast"]         = c.contrast;
    j["saturation"]       = c.saturation;
    j["temperature"]      = c.temperature;
    j["tint"]             = c.tint;
    j["gamma"]            = c.gamma;

    j["adaptEnabled"]     = c.adaptEnabled;
    j["adaptKey"]         = c.adaptKey;
    j["adaptSpeed"]       = c.adaptSpeed;

    j["chromatic"]        = c.chromatic;
    j["vignette"]         = c.vignette;
    j["grain"]            = c.grain;
    j["sharpen"]          = c.sharpen;

    j["inGameplay"]       = c.inGameplay;
    j["inEditor"]         = c.inEditor;
    j["inMenus"]          = c.inMenus;
    j["skipWhenPaused"]   = c.skipWhenPaused;

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
    RTXConfig& c = m_config;

    c.intensity        = std::clamp(c.intensity, 0.f, 1.f);
    c.preset           = std::clamp(c.preset, 0, 4);
    c.renderScale      = std::clamp(c.renderScale, 0.20f, 1.f);
    c.rayCount         = std::clamp(c.rayCount, 1, 16);
    c.raySteps         = std::clamp(c.raySteps, 4, 32);
    c.rayDistance      = std::clamp(c.rayDistance, 0.02f, 1.f);
    c.stepGrowth       = std::clamp(c.stepGrowth, 1.f, 1.5f);
    c.targetFps        = std::clamp(c.targetFps, 30, 360);
    c.frameSkip        = std::clamp(c.frameSkip, 0, 3);

    c.hdrRange         = std::clamp(c.hdrRange, 1.f, 16.f);

    c.giStrength       = std::clamp(c.giStrength, 0.f, 4.f);
    c.giSaturation     = std::clamp(c.giSaturation, 0.f, 2.f);
    c.lightThreshold   = std::clamp(c.lightThreshold, 0.f, 1.f);
    c.lightRange       = std::clamp(c.lightRange, 0.01f, 1.f);
    c.bounceFalloff    = std::clamp(c.bounceFalloff, 0.1f, 10.f);
    c.normalStrength   = std::clamp(c.normalStrength, 0.5f, 24.f);
    c.thickness        = std::clamp(c.thickness, 0.01f, 2.f);

    c.aoStrength       = std::clamp(c.aoStrength, 0.f, 1.f);
    c.aoRadius         = std::clamp(c.aoRadius, 0.01f, 1.f);
    c.aoPower          = std::clamp(c.aoPower, 0.2f, 4.f);

    c.reflectStrength  = std::clamp(c.reflectStrength, 0.f, 2.f);
    c.reflectRoughness = std::clamp(c.reflectRoughness, 0.f, 1.f);
    c.reflectFresnel   = std::clamp(c.reflectFresnel, 0.f, 1.f);
    c.reflectFade      = std::clamp(c.reflectFade, 0.01f, 1.f);

    c.bloomStrength    = std::clamp(c.bloomStrength, 0.f, 3.f);
    c.bloomThreshold   = std::clamp(c.bloomThreshold, 0.f, 1.f);
    c.bloomSoftKnee    = std::clamp(c.bloomSoftKnee, 0.f, 1.f);
    c.bloomRadius      = std::clamp(c.bloomRadius, 0.5f, 6.f);
    c.bloomBlend       = std::clamp(c.bloomBlend, 0.f, 1.f);
    c.bloomAnamorphic  = std::clamp(c.bloomAnamorphic, 0.f, 1.f);
    c.bloomPasses      = std::clamp(c.bloomPasses, 1, 5);

    c.godRayStrength   = std::clamp(c.godRayStrength, 0.f, 2.f);
    c.godRayDecay      = std::clamp(c.godRayDecay, 0.5f, 0.995f);
    c.godRayDensity    = std::clamp(c.godRayDensity, 0.05f, 2.f);
    c.godRayX          = std::clamp(c.godRayX, 0.f, 1.f);
    c.godRayY          = std::clamp(c.godRayY, 0.f, 1.f);

    c.denoise          = std::clamp(c.denoise, 0.f, 4.f);
    c.atrousPasses     = std::clamp(c.atrousPasses, 0, 5);
    c.temporal         = std::clamp(c.temporal, 0.f, 0.97f);
    c.clampSigma       = std::clamp(c.clampSigma, 0.f, 3.f);

    c.tonemap          = std::clamp(c.tonemap, 0, 4);
    c.exposure         = std::clamp(c.exposure, -2.f, 2.f);
    c.contrast         = std::clamp(c.contrast, 0.5f, 2.f);
    c.saturation       = std::clamp(c.saturation, 0.f, 2.f);
    c.temperature      = std::clamp(c.temperature, -1.f, 1.f);
    c.tint             = std::clamp(c.tint, -1.f, 1.f);
    c.gamma            = std::clamp(c.gamma, 0.5f, 2.f);

    c.adaptKey         = std::clamp(c.adaptKey, 0.04f, 0.60f);
    c.adaptSpeed       = std::clamp(c.adaptSpeed, 0.1f, 6.f);

    c.chromatic        = std::clamp(c.chromatic, 0.f, 2.f);
    c.vignette         = std::clamp(c.vignette, 0.f, 2.f);
    c.grain            = std::clamp(c.grain, 0.f, 1.f);
    c.sharpen          = std::clamp(c.sharpen, 0.f, 2.f);
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
