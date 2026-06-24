#pragma once
#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <string>
#include <vector>

// Persisted configuration for GJScoreCell visual FX (Mod saved values, keys prefixed "scorecell-fx-*").
namespace paimon::scorecell {

// ── Icon-color gradient ─────────────────────────────────────────────
// Draws a gradient behind every leaderboard cell built from that player's
// own icon colors (GJUserScore::m_color1 / m_color2).

inline bool gradientEnabled() {
    return geode::Mod::get()->getSavedValue<bool>("scorecell-fx-gradient", false);
}
inline void setGradientEnabled(bool v) {
    geode::Mod::get()->setSavedValue<bool>("scorecell-fx-gradient", v);
}

// Animated gradient effect: "none" | "rotate" | "pulse" | "shift" | "slide".
inline std::string gradientEffect() {
    return geode::Mod::get()->getSavedValue<std::string>("scorecell-fx-gradient-effect", "shift");
}
inline void setGradientEffect(std::string const& v) {
    geode::Mod::get()->setSavedValue<std::string>("scorecell-fx-gradient-effect", v);
}

inline float gradientSpeed() {
    return std::clamp(geode::Mod::get()->getSavedValue<float>("scorecell-fx-gradient-speed", 1.0f), 0.1f, 5.0f);
}
inline void setGradientSpeed(float v) {
    geode::Mod::get()->setSavedValue<float>("scorecell-fx-gradient-speed", std::clamp(v, 0.1f, 5.0f));
}

// 0..255 alpha of the gradient layer.
inline int gradientOpacity() {
    return std::clamp(geode::Mod::get()->getSavedValue<int>("scorecell-fx-gradient-opacity", 130), 0, 255);
}
inline void setGradientOpacity(int v) {
    geode::Mod::get()->setSavedValue<int>("scorecell-fx-gradient-opacity", std::clamp(v, 0, 255));
}

// ── Hover animation (desktop only) ──────────────────────────────────
// A fluid animation that plays while the mouse is over a cell.

inline bool hoverEnabled() {
    return geode::Mod::get()->getSavedValue<bool>("scorecell-fx-hover", true);
}
inline void setHoverEnabled(bool v) {
    geode::Mod::get()->setSavedValue<bool>("scorecell-fx-hover", v);
}

// "scale" | "glow" | "lift" | "tilt" | "shine"
inline std::string hoverType() {
    return geode::Mod::get()->getSavedValue<std::string>("scorecell-fx-hover-type", "glow");
}
inline void setHoverType(std::string const& v) {
    geode::Mod::get()->setSavedValue<std::string>("scorecell-fx-hover-type", v);
}

// 0..1 strength of the hover animation.
inline float hoverIntensity() {
    return std::clamp(geode::Mod::get()->getSavedValue<float>("scorecell-fx-hover-intensity", 0.6f), 0.f, 1.f);
}
inline void setHoverIntensity(float v) {
    geode::Mod::get()->setSavedValue<float>("scorecell-fx-hover-intensity", std::clamp(v, 0.f, 1.f));
}

// ── Entrance animation ──────────────────────────────────────────────
// Played once when a cell's profile banner appears.

// "none" | "fade" | "slide" | "pop" | "bounce"
inline std::string entranceType() {
    return geode::Mod::get()->getSavedValue<std::string>("scorecell-fx-entrance", "slide");
}
inline void setEntranceType(std::string const& v) {
    geode::Mod::get()->setSavedValue<std::string>("scorecell-fx-entrance", v);
}

// ── Catalogs ────────────────────────────────────────────────────────

inline std::vector<std::string> const& gradientEffects() {
    static std::vector<std::string> const list = {"none", "rotate", "pulse", "shift", "slide"};
    return list;
}
inline std::vector<std::string> const& hoverTypes() {
    static std::vector<std::string> const list = {"scale", "glow", "lift", "tilt", "shine"};
    return list;
}
inline std::vector<std::string> const& entranceTypes() {
    static std::vector<std::string> const list = {"none", "fade", "slide", "pop", "bounce"};
    return list;
}

inline std::string normalizeHoverType(std::string const& v) {
    auto const& l = hoverTypes();
    return std::find(l.begin(), l.end(), v) != l.end() ? v : std::string("glow");
}
inline std::string normalizeEntranceType(std::string const& v) {
    auto const& l = entranceTypes();
    return std::find(l.begin(), l.end(), v) != l.end() ? v : std::string("none");
}

inline void migrateAnimatedLeaderboardFx() {}
inline void migrateGlassLeaderboardFx() {}
inline void migrateSmoothLeaderboardFx() {}

} // namespace paimon::scorecell
