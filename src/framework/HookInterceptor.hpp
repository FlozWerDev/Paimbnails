#pragma once

#include "PermissionPolicy.hpp"
#include "EventBus.hpp"
#include "ModEvents.hpp"
#include <string>
#include <vector>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace paimon {

enum class HookAction { Allow, Deny };

struct HookResult {
    HookAction action = HookAction::Allow;
    std::string reason;

    static HookResult allow() { return {HookAction::Allow, {}}; }
    static HookResult deny(std::string reason) { return {HookAction::Deny, std::move(reason)}; }

    bool isAllowed() const { return action != HookAction::Deny; }
};

// Context passed to each hook.
struct HookContext {
    std::string action;       // "upload", "validate", "security-check"
    int levelID = 0;
    std::string username;
    std::string format;       // "png", "gif", "mp4"
    size_t dataSize = 0;
    std::vector<uint8_t> const* data = nullptr;  // payload pointer (not copied)
};

using PreHookFn  = std::function<HookResult(HookContext const&)>;
using PostHookFn = std::function<void(HookContext const&, bool success)>;

// HookInterceptor: pre/post interceptors ONLY for uploads, security, validation.
//
// Usage:
//   HookInterceptor::get().addPreHook("upload", [](HookContext const& ctx) {
//       if (ctx.dataSize > 5 * 1024 * 1024) return HookResult::deny("File > 5MB");
//       return HookResult::allow();
//   });
//
//   auto result = HookInterceptor::get().runPreHooks(ctx);
//   if (!result.isAllowed()) { /* blocked */ }

class HookInterceptor {
public:
    static HookInterceptor& get() {
        static HookInterceptor instance;
        return instance;
    }

    void addPreHook(std::string const& action, PreHookFn hook) {
        std::lock_guard lock(m_mutex);
        m_preHooks[action].push_back(std::move(hook));
    }

    void addPostHook(std::string const& action, PostHookFn hook) {
        std::lock_guard lock(m_mutex);
        m_postHooks[action].push_back(std::move(hook));
    }

    // Run all pre-hooks for the action. Returns Deny if any denies.
    HookResult runPreHooks(HookContext const& ctx) {
        std::unique_lock lock(m_mutex);
        auto it = m_preHooks.find(ctx.action);
        if (it == m_preHooks.end()) return HookResult::allow();

        // Copy so hooks can register during execution.
        auto hooks = it->second;
        lock.unlock();

        for (auto const& hook : hooks) {
            auto result = hook(ctx);
            if (result.action == HookAction::Deny) {
                EventBus::get().publish(PermissionDeniedEvent{
                    "", ctx.action, result.reason
                });
                return result;
            }
        }
        return HookResult::allow();
    }

    HookResult runPreHooks(
        HookContext& ctx,
        std::initializer_list<std::string_view> actions
    ) {
        for (auto action : actions) {
            ctx.action = action;
            auto result = runPreHooks(ctx);
            if (!result.isAllowed()) return result;
        }
        return HookResult::allow();
    }

    // Run all post-hooks for the action (cannot block).
    void runPostHooks(HookContext const& ctx, bool success) {
        std::unique_lock lock(m_mutex);
        auto it = m_postHooks.find(ctx.action);
        if (it == m_postHooks.end()) return;

        auto hooks = it->second;
        lock.unlock();

        for (auto const& hook : hooks) {
            hook(ctx, success);
        }
    }

private:
    HookInterceptor() = default;
    ~HookInterceptor() = default;
    HookInterceptor(HookInterceptor const&) = delete;
    HookInterceptor& operator=(HookInterceptor const&) = delete;

    std::mutex m_mutex;
    std::unordered_map<std::string, std::vector<PreHookFn>> m_preHooks;
    std::unordered_map<std::string, std::vector<PostHookFn>> m_postHooks;
};

} // namespace paimon
