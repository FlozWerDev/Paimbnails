#pragma once

// settings snapshot so hooks never read mod.json mid-frame. the module toggle
// is backed by icon-gradients-enabled; the cache keeps the inverse disabled bit.

#include "GradientTypes.hpp"
#include "../../core/modules/ModuleRegistry.hpp"

namespace paimon::icon_gradients {

constexpr char const* kModuleId = "paimbnails.icongradients.global";

// master module toggle; every hook entry point checks this first.
inline bool moduleEnabled() {
    return paimon::modules::isEnabled(kModuleId);
}

constexpr char const* kSettingEnabled = "icon-gradients-enabled";
constexpr char const* kSettingFlip2P = "icon-gradients-flip-2p";
constexpr char const* kSettingSeparate2P = "icon-gradients-separate-2p";
constexpr char const* kSettingDisable2P = "icon-gradients-disable-2p";
constexpr char const* kSettingMenu = "icon-gradients-menu";
constexpr char const* kSettingHideOnMove = "icon-gradients-hide-on-move";
constexpr char const* kSettingPointOpacity = "icon-gradients-point-opacity";
constexpr char const* kSettingPointScale = "icon-gradients-point-scale";
constexpr char const* kSettingDisableKeys = "icon-gradients-disable-keys";
constexpr char const* kSettingMoveStep = "icon-gradients-move-step";
constexpr char const* kSettingIncreaseTolerance = "icon-gradients-increase-tolerance";

class GradientCache {

protected:

    IconType m_lastSelected = IconType::Cube;

    GradientConfig m_copiedConfig;

    bool m_disabled = false;
    bool m_menuGradients = false;
    bool m_p2disabled = false;
    bool m_p2separate = false;
    bool m_p2flip = false;

public:

    bool m_increaseLineTolerance = false;

    static GradientCache& get();

    static void setModDisabled(bool);

    static bool isModDisabled();

    static void setMenuGradientsEnabled(bool);

    static bool isMenuGradientsEnabled();

    // separate only sticks while neither the module nor the 2p doll is disabled.
    static void set2PDisabled(bool);

    static bool is2PDisabled();

    static void set2PSeparate(bool);

    static bool is2PSeparate();

    static void set2PFlip(bool);

    static bool is2PFlip();

    static IconType getLastSelected();

    static void setLastSelected(IconType);

    static GradientConfig getCopiedConfig();

    static void setCopiedConfig(GradientConfig);

};

} // namespace paimon::icon_gradients
