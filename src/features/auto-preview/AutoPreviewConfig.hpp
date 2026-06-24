#pragma once

#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <cstdint>
#include <string>

namespace paimon::autopreview::config {

// Master switch. When off, nothing in the feature runs.
inline bool enabled() {
    return geode::Mod::get()->getSettingValue<bool>("auto-preview-enable");
}
// heavy/experimental path: it must download + fully instantiate the level
// offscreen. Forced OFF on mobile regardless of the stored value.
inline bool browserGenEnabled() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return false;
#else
    return enabled() && geode::Mod::get()->getSettingValue<bool>("auto-preview-browser-gen");
#endif
}
// real, community-curated thumbnails.
inline bool badgeEnabled() {
    return geode::Mod::get()->getSettingValue<bool>("auto-preview-badge");
}
inline int previewWidth() {
    auto const q = geode::Mod::get()->getSettingValue<std::string>("auto-preview-quality");
    if (q == "tiny") return 320;
    if (q == "medium") return 640;
    return 480; // "small" (default)
}

inline int previewHeight() {
    return (previewWidth() * 9) / 16;
}inline int maxObjects() {
    auto v = static_cast<int>(geode::Mod::get()->getSettingValue<int64_t>("auto-preview-max-objects"));
    return std::clamp(v, 1000, 500000);
}inline int maxPerSession() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return 0;
#else
    auto v = static_cast<int>(geode::Mod::get()->getSettingValue<int64_t>("auto-preview-max-per-session"));
    return std::clamp(v, 0, 500);
#endif
}
inline int dwellMs() {
    return 600;
}

} // namespace paimon::autopreview::config
