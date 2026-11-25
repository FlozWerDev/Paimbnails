#pragma once

// single entry point for settings: gset/sset read and write declared
// settings (falling back to saved values), gsaved/ssaved use saved values.
// Geode builds delegate to GeodeBackend; standalone builds use the backend
// registered via setBackend() (or an in-memory default).

#include "IModBackend.hpp"
#include "StandaloneBackend.hpp"

#ifdef PAIM_USE_GEODE
#include "GeodeBackend.hpp"
#include "../core/Settings.hpp" // reuses paimon::settings::internal version counter
#endif

#include <atomic>
#include <cstdint>
#include <string_view>
#include <utility>

namespace paimon::platform {

namespace detail {

inline std::atomic<IModBackend*>& backendOverride() {
    static std::atomic<IModBackend*> s_override{nullptr};
    return s_override;
}

inline std::atomic<std::uint64_t>& standaloneVersion() {
    static std::atomic<std::uint64_t> s_version{0};
    return s_version;
}

} // namespace detail

// active store: injected test double, Geode mod storage, or memory.
inline IModBackend& activeBackend() {
    if (IModBackend* custom = detail::backendOverride().load(std::memory_order_acquire)) {
        return *custom;
    }
#ifdef PAIM_USE_GEODE
    return GeodeBackend::instance();
#else
    return StandaloneBackend::defaultInstance();
#endif
}

// swaps the backend (standalone hosts, test doubles). Restore with resetBackend().
inline void setBackend(IModBackend& backend) {
    detail::backendOverride().store(&backend, std::memory_order_release);
}

inline void resetBackend() {
    detail::backendOverride().store(nullptr, std::memory_order_release);
}

// bumps the settings generation so hot-path caches re-read. Uses the
// existing paimon::settings::internal counter in Geode builds.
inline void invalidateSettingsCache() {
#ifdef PAIM_USE_GEODE
    paimon::settings::internal::invalidateSettingsCache();
#else
    detail::standaloneVersion().fetch_add(1, std::memory_order_relaxed);
#endif
}

inline std::uint64_t settingsVersion() {
#ifdef PAIM_USE_GEODE
    return paimon::settings::internal::g_settingsVersion.load(std::memory_order_relaxed);
#else
    return detail::standaloneVersion().load(std::memory_order_relaxed);
#endif
}

// declared setting when present, saved value otherwise.
template <typename T>
T gset(std::string_view key) {
    static_assert(kSupportedValue<T>, "paimon::platform: unsupported setting type");
    IModBackend& backend = activeBackend();
    if (backend.hasSetting(key)) return backend.getSetting<T>(key);
    return backend.getSaved<T>(key, T{});
}

template <typename T>
void sset(std::string_view key, T const& value) {
    static_assert(kSupportedValue<T>, "paimon::platform: unsupported setting type");
    IModBackend& backend = activeBackend();
    if (backend.hasSetting(key)) backend.setSetting<T>(key, value);
    else backend.setSaved<T>(key, value);
    invalidateSettingsCache();
}

template <typename T>
T gsaved(std::string_view key, T def) {
    static_assert(kSupportedValue<T>, "paimon::platform: unsupported saved type");
    return activeBackend().getSaved<T>(key, std::move(def));
}

template <typename T>
void ssaved(std::string_view key, T const& value) {
    static_assert(kSupportedValue<T>, "paimon::platform: unsupported saved type");
    activeBackend().setSaved<T>(key, value);
    invalidateSettingsCache();
}

} // namespace paimon::platform
