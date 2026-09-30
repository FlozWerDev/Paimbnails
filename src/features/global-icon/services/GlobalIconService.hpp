#pragma once

// uploads the local player's active custom icons to the global icon server:
// reads them via more icons and base64-encodes png/plist for /api/icons/sync.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::globalicon {

class GlobalIconService {
public:
    static GlobalIconService& get();

    using ResultCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    void uploadActiveIcons(int accountID, std::string const& username, ResultCallback cb);
    // disable/clear the player's icons on the server (icons: []).
    void clearIcons(int accountID, std::string const& username, ResultCallback cb);

    // local flag (user intent) so the button can render without hitting the server each time.
    static bool isEnabledLocally();
    static void setEnabledLocally(bool enabled);

    // turns a raw sync failure ("http 401: {...}") into something a player can
    // act on, instead of a truncated response body.
    static std::string describeSyncError(std::string const& response);

private:
    GlobalIconService() = default;
};

} // namespace paimon::globalicon
