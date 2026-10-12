#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace paimon::hooks {

inline bool setPostPriority(auto& self, std::string_view method, int32_t priority) {
    auto result = self.setHookPriorityPost(method, priority);
    if (result) return true;

    std::string available;
    for (auto* hook : self.getAllHooks()) {
        if (!available.empty()) available += ", ";
        available += hook->getDisplayName();
    }
    geode::log::error("[Hooks] Could not set priority for {}: {}. Registered hooks: {}",
        method, result.unwrapErr(), available.empty() ? "none" : available);
    return false;
}

inline void logRegisteredHooks() {
    auto* mod = geode::Mod::get();
    auto hooks = mod->getHooks();
    auto* files = cocos2d::CCFileUtils::sharedFileUtils();
    geode::log::debug("[Hooks] Geode {}, {} hooks, {} texture packs",
        geode::Loader::get()->getVersion().toVString(), hooks.size(),
        files ? files->getTexturePackCount() : 0);
    for (auto* hook : hooks) {
        geode::log::debug("[Hooks] {} -> {}, priority={}, enabled={}",
            hook->getModifyClassName().value_or("manual"), hook->getDisplayName(),
            hook->getPriority(), hook->isEnabled());
    }
}

inline void afterNodeIdsOrLate(auto& self, std::string_view method) {
    if (!setPostPriority(self, method, geode::Priority::Late)) return;
    if (!self.setHookPriorityAfterPost(method, "geode.node-ids")) {
        geode::log::warn(
            "[Paimbnails] setHookPriorityAfterPost({}, geode.node-ids) failed; using Late",
            method
        );
    }
}

inline void veryLatePost(auto& self, std::string_view method) {
    (void)setPostPriority(self, method, geode::Priority::VeryLate);
}

inline void afterModOrElseNodeIdsLate(
    auto& self, std::string_view method, std::string_view afterModId
) {
    if (!setPostPriority(self, method, geode::Priority::Late)) return;
    if (self.setHookPriorityAfterPost(method, afterModId)) {
        return;
    }
    afterNodeIdsOrLate(self, method);
}

} // namespace paimon::hooks
