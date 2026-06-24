#pragma once

// Hook-priority conventions aligned with skills/geode/SKILL.md (sections 6, 153).
// Goal: run after geode.node-ids without stomping other mods via Priority::Last.

#include <Geode/Geode.hpp>
#include <string>
#include <string_view>

namespace paimon::hooks {

inline void afterNodeIdsOrLate(auto& self, std::string_view method) {
    std::string const fn{method};
    if (!self.setHookPriorityAfterPost(fn, "geode.node-ids")) {
        (void)self.setHookPriorityPost(fn, geode::Priority::Late);
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
    if (!self.setHookPriorityAfterPost(fn, "flozwer.paimbnails2")) {
        (void)self.setHookPriorityPost(fn, geode::Priority::VeryLate);
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