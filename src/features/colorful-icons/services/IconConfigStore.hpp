#pragma once
//
// IconConfigStore.hpp - Singleton wrapping load/save of PaimonIconConfig.
//
// Stored under Mod::get()->setSavedValue("paimon-icons.config.v1", ...)
// using a custom matjson::Serialize specialisation so the config struct can
// be passed through Mod's saved-value API directly.
//
// Listeners get notified (via Geode events) when config changes, so any UI
// that displays icons can re-color itself live.
//

#include "../PaimonIconsConfig.hpp"

#include <Geode/loader/Event.hpp>

#include <functional>
#include <memory>

namespace paimon::icons {

// ─────────────────────────────────────────────────────────────
// Event broadcast whenever the config blob changes (user toggles a
// setting, picks a color, loads a preset, etc).
//
// Filter: empty string (single global broadcast). Any UI piece listens
// without filter to receive every change.
// ─────────────────────────────────────────────────────────────
class IconConfigChangedEvent
    : public geode::Event<IconConfigChangedEvent, bool(), std::string>
{
public:
    using Event::Event;
};

// ─────────────────────────────────────────────────────────────
// Store. Singleton accessed via IconConfigStore::get().
// All getters return references to a snapshot owned by the store; do not
// retain references across save() calls.
// ─────────────────────────────────────────────────────────────
class IconConfigStore final {
public:
    static IconConfigStore& get();

    // Read-only access to the live config. Mutations must go through update().
    PaimonIconConfig const& config() const { return m_config; }

    // Apply a mutation, persist it, and broadcast IconConfigChangedEvent.
    void update(std::function<void(PaimonIconConfig&)> const& mutator);

    // Reset everything to the in-memory defaults and persist.
    void resetToDefaults();

    // Load fresh from disk (called once at mod load).
    void load();

    // Force a re-broadcast without modifying anything (used after external
    // events like player color change).
    void notifyChangedExternally();

    // Master-toggle from mod.json. When false, the recolor system stays out
    // of the way entirely. Re-evaluated on every notifyChangedExternally().
    bool isFeatureEnabled() const;

    // Preset helpers - serialize/deserialize the rest of the config so users
    // can save and switch between favourites.
    bool saveCurrentAsPreset(std::string const& name);
    bool loadPreset(std::string const& name);
    bool deletePreset(std::string const& name);

    // Export/import as a single JSON string (for clipboard sharing).
    std::string exportToString() const;
    bool importFromString(std::string const& serialized);

private:
    IconConfigStore();
    void persist();

    PaimonIconConfig m_config;
    bool m_loaded = false;
};

}  // namespace paimon::icons
