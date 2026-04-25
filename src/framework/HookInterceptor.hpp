#pragma once

#include "PermissionPolicy.hpp"
#include "ModEvents.hpp"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <cstdint>

namespace paimon {

// ── HookResult ──────────────────────────────────────────────────────

enum class HookAction { Allow, Deny, Modify };

struct HookResult {
    HookAction action = HookAction::Allow;
    std::string reason;
    std::vector<uint8_t> modifiedData;  // solo si action == Modify

    static HookResult allow() { return {HookAction::Allow, {}, {}}; }
    static HookResult deny(std::string reason) { return {HookAction::Deny, std::move(reason), {}}; }
    static HookResult modify(std::vector<uint8_t> data, std::string reason = {}) {
        return {HookAction::Modify, std::move(reason), std::move(data)};
    }

    bool isAllowed() const { return action != HookAction::Deny; }
    bool isModified() const { return action == HookAction::Modify; }
};

// ── Contexto pasado a cada hook ─────────────────────────────────────

struct HookContext {
    std::string action;       // "upload", "validate", "security-check"
    int levelID = 0;
    std::string username;
    std::string format;       // "png", "gif", "mp4"
    size_t dataSize = 0;
    std::vector<uint8_t> const* data = nullptr;  // puntero a payload (no copia)
};

// ── Tipos de interceptor ────────────────────────────────────────────

using PreHookFn  = std::function<HookResult(HookContext const&)>;
using PostHookFn = std::function<void(HookContext const&, bool success)>;

// ── HookInterceptor ─────────────────────────────────────────────────
// Interceptores pre/post SOLO para: uploads, seguridad, validacion.
// Inspirado en HookRunner de claw-code (exit code 0=Allow, 2=Deny).
//
// Uso:
//   HookInterceptor::get().addPreHook("upload", [](HookContext const& ctx) {
//       if (ctx.dataSize > 5 * 1024 * 1024) return HookResult::deny("Archivo > 5MB");
//       return HookResult::allow();
//   });
//
//   auto result = HookInterceptor::get().runPreHooks(ctx);
//   if (!result.isAllowed()) { /* bloqueado */ }

class HookInterceptor {
public:
    static HookInterceptor& get() {
        static HookInterceptor instance;
        return instance;
    }

    // Registra un pre-hook para una accion especifica.
    void addPreHook(std::string const& action, PreHookFn hook) {
        std::lock_guard lock(m_mutex);
        m_preHooks[action].push_back(std::move(hook));
    }

    // Registra un post-hook para una accion especifica (observacion).
    void addPostHook(std::string const& action, PostHookFn hook) {
        std::lock_guard lock(m_mutex);
        m_postHooks[action].push_back(std::move(hook));
    }

    // Ejecuta todos los pre-hooks para la accion. Si alguno deniega, retorna Deny.
    // Si alguno modifica data, acumula la ultima modificacion.
    HookResult runPreHooks(HookContext const& ctx) {
        std::unique_lock lock(m_mutex);
        auto it = m_preHooks.find(ctx.action);
        if (it == m_preHooks.end()) return HookResult::allow();

        // Copiar para permitir que hooks se registren durante ejecucion.
        auto hooks = it->second;
        lock.unlock();

        HookResult finalResult = HookResult::allow();
        for (auto const& hook : hooks) {
            auto result = hook(ctx);
            if (result.action == HookAction::Deny) {
                // Publicar evento de denegacion.
                PermissionDeniedEvent ev;
                ev.featureName = "";
                ev.action = ctx.action;
                ev.reason = result.reason;
                ev.post();
                return result;
            }
            if (result.action == HookAction::Modify) {
                finalResult = std::move(result);
            }
        }
        return finalResult;
    }

    // Ejecuta todos los post-hooks para la accion (no pueden bloquear).
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
