#pragma once

#include "../../core/modules/ModuleRegistry.hpp"

#include <atomic>

namespace paimon::gameplayperf {

inline constexpr char const* kModuleId = "paimbnails.performance.gameplay";
inline constexpr char const* kNativeModeModuleId = "paimbnails.nativeperf.gameplay";
inline constexpr char const* kGlowModuleId = "paimbnails.glowcut.gameplay";
inline constexpr char const* kBackgroundEffectsModuleId = "paimbnails.bgeffects.gameplay";
inline constexpr char const* kGameplayEffectsModuleId = "paimbnails.gameeffects.gameplay";
inline constexpr char const* kPlayerEffectsModuleId = "paimbnails.playereffects.gameplay";
inline constexpr char const* kDynamicVolumeModuleId = "paimbnails.dynamicvolume.gameplay";
inline constexpr char const* kModVisualsModuleId = "paimbnails.modvisuals.gameplay";
inline constexpr char const* kAutoPreviewModuleId = "paimbnails.autopreview.gameplay";
inline constexpr char const* kTransitionsModuleId = "paimbnails.perftransitions.gameplay";
inline constexpr char const* kBackgroundModuleId = "paimbnails.backgroundcut.gameplay";
inline constexpr char const* kGroundModuleId = "paimbnails.groundcut.gameplay";
inline constexpr char const* kDecorationModuleId = "paimbnails.decocut.gameplay";
inline constexpr char const* kGradientsModuleId = "paimbnails.gradientcut.gameplay";
inline constexpr char const* kShadersModuleId = "paimbnails.shadercut.gameplay";
inline constexpr char const* kParticlesModuleId = "paimbnails.particlecut.gameplay";
inline constexpr char const* kLevelEffectsModuleId = "paimbnails.levelcut.gameplay";
inline std::atomic_bool s_active{false};

inline bool isEnabled() {
    return modules::isEnabled(kModuleId);
}

inline bool isActive() {
    return s_active.load(std::memory_order_relaxed);
}

inline bool isOptionEnabled(char const* moduleId) {
    return modules::isEnabled(moduleId);
}

inline bool isOptionActive(char const* moduleId) {
    return isActive() && isOptionEnabled(moduleId);
}

inline void setActive(bool active) {
    s_active.store(active, std::memory_order_relaxed);
}

} // namespace paimon::gameplayperf
