#pragma once

#include "../engine/IconCompiler.hpp"

#include <Geode/Geode.hpp>

#include <string>

namespace paimon::icon_maker {

class MoreIconsBridge final {
public:
    static bool available();

    // "paimbicon-<slotid>" — namespaced so it never collides with user icons.
    static std::string registeredName(std::string_view slotId);

    // registers (or re-registers) the compiled icon with moreicons, picking
    // the quality that matches the current content scale factor.
    static geode::Result<> registerIcon(IconProject const& project,
                                        CompiledIcon const& compiled);

    // selects the icon as the active one for its gamemode. returns false when
    // moreicons is missing or the icon is not registered.
    static bool applyIcon(IconProject const& project);

    // deselects our icon for `type` if it is the active one.
    static void clearIcon(IconType type, std::string_view slotId);

    // true when moreicons has an active icon for `type` that is not ours —
    // the own-apply fallback must then keep its hands off.
    static bool hasForeignActive(IconType type);

    // true when the active moreicons icon for `type` is one of ours.
    static bool isOurActive(IconType type);

    // slot id of our active moreicons icon for `type`, or empty.
    static std::string activeOursSlotId(IconType type);

private:
    MoreIconsBridge() = delete;
};

}  // namespace paimon::icon_maker
