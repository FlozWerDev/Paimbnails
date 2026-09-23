#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <string_view>

namespace paimon::hooks {

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

inline void veryLatePost(auto& self, std::string_view method) {
    std::string const fn{method};
    (void)self.setHookPriorityPost(fn, geode::Priority::VeryLate);
}

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

} // namespace paimon::hooks
