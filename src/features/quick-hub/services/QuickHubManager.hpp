#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>

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

    // ── Configuracion persistente ──

    // Devuelve los IDs de las opciones activas en orden
    std::vector<std::string> getActiveOptions() const;

    // Guarda la nueva configuracion de opciones activas
    void setActiveOptions(std::vector<std::string> const& options);

    // Resetea a la configuracion por defecto
    void resetToDefault();

    // ── Estado del radial ──

    bool isRadialOpen() const { return m_radialOpen; }
    void setRadialOpen(bool open) { m_radialOpen = open; }

private:
    QuickHubManager() = default;
    bool m_radialOpen = false;

    // Key para guardar en saved values
    static constexpr char const* kSavedKey = "quick-hub-radial-order";
};

} // namespace paimon::quickhub
