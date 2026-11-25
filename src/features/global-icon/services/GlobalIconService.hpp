#pragma once

// links the local player's icon to their global icon in paimon base: the
// vanilla set (ids and colors), the icon gradients and the custom more icons
// textures. files go up one by one to /api/community/assets, then a single
// /api/community/icons call links them.

#include <Geode/Geode.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>

namespace paimon::globalicon {

class GlobalIconService {
public:
    static GlobalIconService& get();

    using ResultCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    // uploads the chosen parts and links them; message is readable on failure.
    void publish(LinkOptions options, ResultCallback cb);
    // stops sharing and frees the uploaded files.
    void clearIcons(ResultCallback cb);
    bool busy() const { return m_busy; }

    // local flag (user intent) so the button can render without hitting the server each time.
    static bool isEnabledLocally();
    static void setEnabledLocally(bool enabled);
    static LinkOptions savedOptions();
    static void saveOptions(LinkOptions options);

    // more icons is loaded and at least one gamemode uses a custom icon.
    static bool hasCustomIcons();
    // the player has saved at least one icon gradient.
    static bool hasGradients();

private:
    GlobalIconService() = default;
    bool m_busy = false;
};

} // namespace paimon::globalicon
