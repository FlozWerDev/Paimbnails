#pragma once

#include "UnifiedAuthFlow.hpp"
#include <functional>
#include <string>
#include <string_view>

class FLAlertLayer;

namespace paimon::modauth {

using SessionRoles = paimon::auth::SessionRoles;

void startOrComplete();
void showPanel();
FLAlertLayer* createUnifiedPanel();

void clearVerifiedSession();
void setVerifiedSession(std::string const& username, int accountID, bool moderator, bool admin);
void setVerifiedSession(std::string const& username, int accountID, bool moderator, bool admin, bool vip);
void setVerifiedSession(std::string const& username, int accountID, SessionRoles const& roles);

// Volatile server-verified role & permission queries (never trust persisted disk flags).
bool isAuthenticated();
bool isVerified(bool admin = false);
bool isVerifiedVip();
bool isVerifiedHelper();
bool isVerifiedIdea();
SessionRoles currentRoles();
bool hasPermission(std::string_view permission);

// Live refresh from GET /api/auth/session and server-side revocation via POST /api/auth/logout.
void refreshSession(std::function<void(SessionRoles const&)> callback = {});
void logout(std::function<void(bool)> callback = {});

// Empty/zero unless the volatile moderator session is still verified. Callers
// must never use GD fields as moderation identity.
std::string verifiedUsername();
int verifiedAccountID();

} // namespace paimon::modauth
