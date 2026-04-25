#pragma once

#include "FeatureSpec.hpp"
#include "../core/Settings.hpp"
#include <string>

namespace paimon {

// Determina el tier del usuario actual basado en sus flags de moderacion.
inline PermissionTier currentUserTier() {
    if (settings::moderation::isVerifiedAdmin())     return PermissionTier::Admin;
    if (settings::moderation::isVerifiedModerator())  return PermissionTier::Moderator;
    if (settings::moderation::isVerifiedVip())        return PermissionTier::Contributor;
    return PermissionTier::User;
}

struct AuthResult {
    bool allowed = false;
    std::string reason;

    explicit operator bool() const { return allowed; }

    static AuthResult allow() { return {true, {}}; }
    static AuthResult deny(std::string msg) { return {false, std::move(msg)}; }
};

class PermissionPolicy {
public:
    static PermissionPolicy& get() {
        static PermissionPolicy instance;
        return instance;
    }

    // Verifica si el tier del usuario cumple el tier requerido.
    AuthResult authorize(PermissionTier required) const {
        PermissionTier user = currentUserTier();
        if (static_cast<int>(user) >= static_cast<int>(required)) {
            return AuthResult::allow();
        }
        return AuthResult::deny(
            "Se requiere tier " + tierName(required) +
            ", tier actual: " + tierName(user)
        );
    }

    // Autoriza contra el tier declarado por un feature registrado.
    AuthResult authorizeFeature(std::string const& featureName) const;

    static std::string tierName(PermissionTier tier) {
        switch (tier) {
            case PermissionTier::Viewer:      return "Viewer";
            case PermissionTier::User:        return "User";
            case PermissionTier::Contributor: return "Contributor";
            case PermissionTier::Moderator:   return "Moderator";
            case PermissionTier::Admin:       return "Admin";
        }
        return "Unknown";
    }

private:
    PermissionPolicy() = default;
    ~PermissionPolicy() = default;
    PermissionPolicy(PermissionPolicy const&) = delete;
    PermissionPolicy& operator=(PermissionPolicy const&) = delete;
};

} // namespace paimon
