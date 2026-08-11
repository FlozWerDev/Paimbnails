#pragma once

// Uploads the local player's active custom icons to the Global Icon server:
// reads them via More Icons and base64-encodes PNG/plist for /api/icons/sync.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::globalicon {

class GlobalIconService {
public:
    static GlobalIconService& get();

    using ResultCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    // Upload the local player's active custom icons (own profile).
    void uploadActiveIcons(int accountID, std::string const& username, ResultCallback cb);
    // Disable/clear the player's icons on the server (icons: []).
    void clearIcons(int accountID, std::string const& username, ResultCallback cb);

    // Local flag (user intent) so the button can render without hitting the server each time.
    static bool isEnabledLocally();
    static void setEnabledLocally(bool enabled);

    // Turns a raw sync failure ("HTTP 401: {...}") into something a player can
    // act on, instead of a truncated response body.
    static std::string describeSyncError(std::string const& response);

private:
    GlobalIconService() = default;
};

} // namespace paimon::globalicon
