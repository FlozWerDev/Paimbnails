#pragma once

// Hook-priority conventions aligned with skills/geode/SKILL.md (sections 6, 153).
// Goal: run after geode.node-ids without stomping other mods via Priority::Last.

#include <Geode/Geode.hpp>
#include <Geode/utils/VMTHookManager.hpp>
#include <string>
#include <string_view>

namespace paimon::hooks {

template <auto Function, class Instance>
inline void addInheritedHook(Instance* instance, std::string_view method) {
    auto result = geode::VMTHookManager::get().addHook<Function>(instance, std::string(method));
    if (!result) {
        geode::log::warn(
            "[Paimbnails] Failed to hook inherited {}: {}",
            method,
            result.unwrapErr()
        );
    }
}

inline void afterNodeIdsOrLate(auto& self, std::string_view method) {
    std::string const fn{method};
    (void)self.setHookPriorityPost(fn, geode::Priority::Late);
    if (!self.setHookPriorityAfterPost(fn, "geode.node-ids")) {
        geode::log::warn(
            "[Paimbnails] setHookPriorityAfterPost({}, geode.node-ids) failed; using Late",
            fn
        );
    }
}

/// UI applied after the rest of Paimbnails' hooks on the same layer (beat
/// shaders, menu layout, etc.). Tries to chain after this mod, else VeryLate.
inline void afterAllPaimonUiOrVeryLate(auto& self, std::string_view method) {
    std::string const fn{method};
    (void)self.setHookPriorityPost(fn, geode::Priority::VeryLate);
    if (!self.setHookPriorityAfterPost(fn, "flozwer.paimbnails2")) {
        geode::log::warn(
            "[Paimbnails] setHookPriorityAfterPost({}, flozwer.paimbnails2) failed; using VeryLate",
            fn
        );
    }
}

/// Post-hook after `afterModId` if that mod is loaded, else after node-ids.
inline void afterModOrElseNodeIdsLate(
    auto& self, std::string_view method, std::string_view afterModId
) {
    std::string const fn{method};
    (void)self.setHookPriorityPost(fn, geode::Priority::Late);
    if (self.setHookPriorityAfterPost(fn, afterModId)) {
        return;
    }
    afterNodeIdsOrLate(self, method);
}

inline void warnIfPriorityFailed(bool ok, std::string_view label) {
    if (!ok) {
        geode::log::warn("[Paimbnails] Failed to set hook priority for {}", label);
    }
}

} // namespace paimon::hooks
