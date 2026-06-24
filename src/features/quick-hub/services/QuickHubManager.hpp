#pragma once
#include <Geode/Geode.hpp>
#include "../data/QuickHubCategories.hpp"
#include <string>
#include <vector>
#include <optional>

namespace paimon::quickhub {

// Singleton que gestiona el estado del Quick Hub Radial:
// - Configuracion persistente (orden de opciones, opciones activas)
// - Estado del hold de Ctrl (timer, barra de progreso)
class QuickHubManager {
public:
    static QuickHubManager& get() {
        static QuickHubManager instance;
        return instance;
    }

    // Configuracion persistente

    // Devuelve los IDs de las opciones activas en orden
    std::vector<std::string> getActiveOptions() const;

    // Guarda la nueva configuracion de opciones activas
    void setActiveOptions(std::vector<std::string> const& options);

    // Resetea a la configuracion por defecto
    void resetToDefault();

    // Botones personalizados (capturados de la UI del juego)
    std::vector<CustomQuickButton> getCustomButtons() const;
    std::optional<CustomQuickButton> getCustomButton(std::string const& id) const;
    std::vector<RadialOptionDef> getAllRadialOptions() const;
    bool saveCustomButton(CustomQuickButton const& button);
    std::string makeUniqueCustomId(std::string const& suggestedName);

    // Estado del radial

    bool isRadialOpen() const { return m_radialOpen; }
    void setRadialOpen(bool open) { m_radialOpen = open; }

    // Mantener Ctrl para abrir el menu radial (Quick Hub).
    static bool isHoldCtrlEnabled();
    static void setHoldCtrlEnabled(bool enabled);

    // Cancela un hold de Ctrl en curso y cierra el radial si estaba abierto.
    static void abortActiveHold();

private:
    QuickHubManager() = default;
    bool m_radialOpen = false;

    // Key para guardar en saved values
    static constexpr char const* kSavedKey = "quick-hub-radial-order";
    static constexpr char const* kHoldCtrlKey = "quick-hub-hold-ctrl-enabled";
};

} // namespace paimon::quickhub
