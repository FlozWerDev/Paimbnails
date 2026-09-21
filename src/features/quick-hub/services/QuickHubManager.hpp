#pragma once
#include <Geode/Geode.hpp>
#include "../data/QuickHubCategories.hpp"
#include <string>
#include <vector>
#include <optional>

namespace paimon::quickhub {

class QuickHubManager {
public:
    static QuickHubManager& get() {
        static QuickHubManager instance;
        return instance;
    }


    std::vector<std::string> getActiveOptions() const;

    void setActiveOptions(std::vector<std::string> const& options);

    void resetToDefault();

    std::vector<CustomQuickButton> getCustomButtons() const;
    std::optional<CustomQuickButton> getCustomButton(std::string const& id) const;
    std::vector<RadialOptionDef> getAllRadialOptions() const;
    bool saveCustomButton(CustomQuickButton const& button);
    bool deleteCustomButton(std::string const& id);
    std::string makeUniqueCustomId(std::string const& suggestedName);

    static bool isHoldCtrlEnabled();
    static void setHoldCtrlEnabled(bool enabled);

    // El radial es una herramienta de navegacion: nunca debe aparecer sobre
    // gameplay real ni durante el playtest del editor.
    static bool canOpenInCurrentContext();

    static void abortActiveHold();

private:
    QuickHubManager() = default;
    static void writeCustomButtons(std::vector<CustomQuickButton> const& all);

    static constexpr char const* kSavedKey = "quick-hub-radial-order";
    static constexpr char const* kHoldCtrlKey = "quick-hub-hold-ctrl-enabled";
};

} // namespace paimon::quickhub
