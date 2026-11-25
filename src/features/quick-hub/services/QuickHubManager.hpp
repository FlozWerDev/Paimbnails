#pragma once
#include <Geode/Geode.hpp>
#include "../data/QuickHubCategories.hpp"
#include "../../../utils/ExtendedKeybind.hpp"
#include <matjson.hpp>
#include <string>
#include <vector>
#include <optional>

namespace paimon::quickhub {

// one captured button <-> its saved json object; missing keys load as defaults.
matjson::Value customQuickButtonToJson(CustomQuickButton const& button);
CustomQuickButton customQuickButtonFromJson(matjson::Value const& value);

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

    // a single keyboard key or mouse button; the legacy enabled setting is preserved.
    static paimon::keybinds::ExtendedKeybind holdButton();
    static void setHoldButton(paimon::keybinds::ExtendedKeybind const& button);
    static std::vector<paimon::keybinds::ExtendedKeybind> const& holdButtonOptions();

    // seconds the selected button (or touch) must be held to open the radial.
    static float holdSeconds();
    static void setHoldSeconds(float seconds);

    static constexpr float kHoldSecondsMin = 0.3f;
    static constexpr float kHoldSecondsMax = 5.0f;
    static constexpr float kHoldSecondsDefault = 1.5f;

    // navigation tool: never over real gameplay or editor playtest.
    static bool canOpenInCurrentContext();

    static void abortActiveHold();

private:
    QuickHubManager() = default;
    static void writeCustomButtons(std::vector<CustomQuickButton> const& all);

    static constexpr char const* kSavedKey = "quick-hub-radial-order";
    static constexpr char const* kHoldCtrlKey = "quick-hub-hold-ctrl-enabled";
    static constexpr char const* kHoldSecondsKey = "quick-hub-hold-seconds";
};

} // namespace paimon::quickhub
