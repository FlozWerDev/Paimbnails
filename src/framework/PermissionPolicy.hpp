#pragma once

#include "../core/Settings.hpp"
#include <string>
#include <utility>

namespace paimon {

enum class PermissionTier : int {
    Viewer = 0,
    User = 1,
    Contributor = 2,
    Moderator = 3,
    Admin = 4,
};

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
