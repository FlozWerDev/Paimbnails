#pragma once

// shared types and mappings for the global icons feature; depends only on gd's
// icontype enum (no more icons), so it compiles even when more icons is absent.
// the data lives in paimon base (cloudflare d1) and the files in bunny, served
// by the paimbnails worker under /api/community/icons and /community/assets.

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <unordered_map>

namespace paimon::globalicon {

// server string <-> gd icontype mapping. (special = trail, shipfire = fire.)
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

// icon types synced by default; death/trail/fire need json and are handled later.
inline std::vector<IconType> const& syncableIconTypes() {
    static std::vector<IconType> const types = {
        IconType::Cube, IconType::Ship, IconType::Ball, IconType::Ufo,
        IconType::Wave, IconType::Robot, IconType::Spider, IconType::Swing,
        IconType::Jetpack,
    };
    return types;
}

// a synced icon entry (mirrors server metadata.icons[<type>]).
struct GlobalIconSlot {
    std::string type;       // server type id ("cube", "ship", ...)
    std::string name;
    std::string packID;
    std::string packName;
    int quality = 3;        // 1=sd 2=hd 3=uhd
    std::string pngFile;
    std::string pngUrl;
    std::string plistFile;
    std::string plistUrl;
};

// which parts of a player's icon are linked to their global icon.
struct LinkOptions {
    bool vanilla = true;
    bool gradients = false;
    bool custom = false;

    bool any() const { return vanilla || gradients || custom; }
};

// per-account metadata document (get /api/community/icons/<accountid>).
struct GlobalIconMeta {
    int accountID = 0;
    std::string username;
    bool enabled = false;
    LinkOptions share{false, false, false};
    // icon ids and colors: cube..jetpack, trail, deathEffect, color1, color2, glowColor, glow.
    matjson::Value vanilla;
    // icon gradients save format keyed by "icon-gradients-<type>[-p2]"; image
    // points reference uploaded files as "asset:<sha256>".
    matjson::Value gradients;
    std::unordered_map<std::string, std::string> assets; // sha256 -> public url
    std::unordered_map<std::string, GlobalIconSlot> icons; // key = type id
};

} // namespace paimon::globalicon
