#pragma once

// Shared types and mappings for the Global Icons feature; depends only on GD's
// IconType enum (no More Icons), so it compiles even when More Icons is absent.

#include <Geode/Geode.hpp>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace paimon::globalicon {

// Global Icon server base URL; override at runtime via the "global-icon-server-url" setting.
inline constexpr std::string_view GLOBAL_ICON_BASE = "https://global-icons.vercel.app";

// Server string <-> GD IconType mapping. (Special = trail, ShipFire = fire.)
inline std::string_view iconTypeToString(IconType type) {
    switch (type) {
        case IconType::Cube:        return "cube";
        case IconType::Ship:        return "ship";
        case IconType::Ball:        return "ball";
        case IconType::Ufo:         return "ufo";
        case IconType::Wave:        return "wave";
        case IconType::Robot:       return "robot";
        case IconType::Spider:      return "spider";
        case IconType::Swing:       return "swing";
        case IconType::Jetpack:     return "jetpack";
        case IconType::DeathEffect: return "death";
        case IconType::Special:     return "trail";
        case IconType::ShipFire:    return "fire";
        default:                    return "";
    }
}

inline std::optional<IconType> iconTypeFromString(std::string_view s) {
    if (s == "cube")    return IconType::Cube;
    if (s == "ship")    return IconType::Ship;
    if (s == "ball")    return IconType::Ball;
    if (s == "ufo")     return IconType::Ufo;
    if (s == "wave")    return IconType::Wave;
    if (s == "robot")   return IconType::Robot;
    if (s == "spider")  return IconType::Spider;
    if (s == "swing")   return IconType::Swing;
    if (s == "jetpack") return IconType::Jetpack;
    if (s == "death")   return IconType::DeathEffect;
    if (s == "trail")   return IconType::Special;
    if (s == "fire")    return IconType::ShipFire;
    return std::nullopt;
}

// Icon types synced by default; death/trail/fire need JSON and are handled later.
inline std::vector<IconType> const& syncableIconTypes() {
    static std::vector<IconType> const types = {
        IconType::Cube, IconType::Ship, IconType::Ball, IconType::Ufo,
        IconType::Wave, IconType::Robot, IconType::Spider, IconType::Swing,
        IconType::Jetpack,
    };
    return types;
}

// A synced icon entry (mirrors server metadata.icons[<type>]).
struct GlobalIconSlot {
    std::string type;       // server type id ("cube", "ship", ...)
    std::string name;
    std::string packID;
    std::string packName;
    int quality = 3;        // 1=SD 2=HD 3=UHD
    int specialID = 0;
    int fireCount = 0;
    std::string pngFile;
    std::string pngUrl;
    std::string plistFile;
    std::string plistUrl;
    std::string jsonFile;
    std::string jsonUrl;
    int64_t bytes = 0;
};

// Per-account metadata document (GET /api/icons/<accountID>).
struct GlobalIconMeta {
    int accountID = 0;
    std::string username;
    bool enabled = false;
    std::string updatedAt;
    std::unordered_map<std::string, GlobalIconSlot> icons; // key = type id
};

} // namespace paimon::globalicon
