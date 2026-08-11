#pragma once
// Único punto de contacto con la API de MoreIcons (aparte de global-icon).
// Cuando MoreIcons está instalado, registra y aplica los iconos compilados en
// caliente (sin reiniciar); si no lo está, todo devuelve false y el
// IconApplier propio toma el relevo.

#include "../engine/IconCompiler.hpp"

#include <Geode/Geode.hpp>

#include <string>

namespace paimon::icon_maker {

class MoreIconsBridge final {
public:
    static bool available();

    // "paimbicon-<slotId>" — namespaced so it never collides with user icons.
    static std::string registeredName(std::string_view slotId);

    // Registers (or re-registers) the compiled icon with MoreIcons, picking
    // the quality that matches the current content scale factor.
    static geode::Result<> registerIcon(IconProject const& project,
                                        CompiledIcon const& compiled);

    // Selects the icon as the active one for its gamemode. Returns false when
    // MoreIcons is missing or the icon is not registered.
    static bool applyIcon(IconProject const& project);

    // Deselects our icon for `type` if it is the active one.
    static void clearIcon(IconType type, std::string_view slotId);

    // True when MoreIcons has an active icon for `type` that is NOT ours —
    // the own-apply fallback must then keep its hands off.
    static bool hasForeignActive(IconType type);

    // True when the active MoreIcons icon for `type` is one of ours.
    static bool isOurActive(IconType type);

    // Slot id of our active MoreIcons icon for `type`, or empty.
    static std::string activeOursSlotId(IconType type);

private:
    MoreIconsBridge() = delete;
};

}  // namespace paimon::icon_maker
