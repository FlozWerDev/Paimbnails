#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <string_view>

namespace paimon::hooks {

inline void afterNodeIdsOrLate(auto& self, std::string_view method) {
    (void)self.setHookPriorityPost(method, geode::Priority::Late);
    if (!self.setHookPriorityAfterPost(method, "geode.node-ids")) {
        geode::log::warn(
            "[Paimbnails] setHookPriorityAfterPost({}, geode.node-ids) failed; using Late",
            method
        );
    }
}

inline void veryLatePost(auto& self, std::string_view method) {
    (void)self.setHookPriorityPost(method, geode::Priority::VeryLate);
}

inline void afterModOrElseNodeIdsLate(
    auto& self, std::string_view method, std::string_view afterModId
) {
    (void)self.setHookPriorityPost(method, geode::Priority::Late);
    if (self.setHookPriorityAfterPost(method, afterModId)) {
        return;
    }
    afterNodeIdsOrLate(self, method);
}

} // namespace paimon::hooks
