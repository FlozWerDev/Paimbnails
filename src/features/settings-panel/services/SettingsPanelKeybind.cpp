#include "SettingsPanelManager.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>

using namespace geode::prelude;

namespace {
    constexpr char const* kPanelKeybind = "settings-panel-keybind";
    constexpr char const* kLegacyKey = "settings-panel-key";
    constexpr char const* kLegacyModifier = "settings-panel-modifier";
    constexpr char const* kMigrationFlag = "settings-panel-keybind-migrated-v1";

    std::string makeLegacyKeybindString(std::string const& modifier, std::string const& key) {
        if (key.empty()) return "";

        if (modifier == "ctrl")  return fmt::format("Ctrl+{}", key);
        if (modifier == "shift") return fmt::format("Shift+{}", key);
        if (modifier == "alt")   return fmt::format("Alt+{}", key);
        if (modifier == "none" || modifier.empty()) return key;

        return fmt::format("{}+{}", modifier, key);
    }

    void migrateLegacySettingsPanelKeybind() {
        auto* mod = Mod::get();
        if (!mod || !mod->hasSetting(kPanelKeybind)) return;
        if (mod->getSavedValue<bool>(kMigrationFlag, false)) return;

        auto legacyKey = mod->getSavedValue<std::string>(kLegacyKey, "P");
        auto legacyModifier = mod->getSavedValue<std::string>(kLegacyModifier, "ctrl");

        auto legacyChord = makeLegacyKeybindString(legacyModifier, legacyKey);
        auto parsed = Keybind::fromString(legacyChord);

        auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(mod->getSetting(kPanelKeybind));
        if (!setting) {
            mod->setSavedValue(kMigrationFlag, true);
            paimon::requestDeferredModSave();
            return;
        }

        if (parsed.isOk()) {
            auto desired = std::vector<Keybind>{ parsed.unwrap() };
            if (setting->getValue() == setting->getDefaultValue()) {
                setting->setValue(desired);
            }
        }

        mod->setSavedValue(kMigrationFlag, true);
        paimon::requestDeferredModSave();
    }
}

$execute {
    migrateLegacySettingsPanelKeybind();

    KeybindSettingPressedEventV3(Mod::get(), kPanelKeybind).listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            SettingsPanelManager::get().toggle();
        }
    ).leak();
}