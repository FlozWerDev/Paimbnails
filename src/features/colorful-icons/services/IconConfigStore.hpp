#pragma once
// Wraps load/save of PaimonIconConfig. Broadcasts IconConfigChangedEvent on mutation.

#include "../PaimonIconsConfig.hpp"

#include <Geode/loader/Event.hpp>

#include <functional>
#include <memory>

namespace paimon::icons {

// Filter: empty string (single global broadcast).
class IconConfigChangedEvent
    : public geode::Event<IconConfigChangedEvent, bool(), std::string>
{
public:
    using Event::Event;
};

// Do not retain references across save() calls.
class IconConfigStore final {
public:
    static IconConfigStore& get();

    PaimonIconConfig const& config() const { return m_config; }

    void update(std::function<void(PaimonIconConfig&)> const& mutator);

    void resetToDefaults();

    void load();

    void notifyChangedExternally();

    bool isFeatureEnabled() const;

    bool saveCurrentAsPreset(std::string const& name);
    bool loadPreset(std::string const& name);
    bool deletePreset(std::string const& name);

    std::string exportToString() const;
    bool importFromString(std::string const& serialized);

private:
    IconConfigStore();
    void persist();

    PaimonIconConfig m_config;
    bool m_loaded = false;
};

}  // namespace paimon::icons
