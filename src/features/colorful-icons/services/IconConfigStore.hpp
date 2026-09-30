#pragma once
// broadcasts iconconfigchangedevent on mutation.

#include "../PaimonIconsConfig.hpp"

#include <Geode/loader/Event.hpp>

#include <functional>

namespace paimon::icons {

// filter: empty string (single global broadcast).
class IconConfigChangedEvent
    : public geode::Event<IconConfigChangedEvent, bool(), std::string>
{
public:
    using Event::Event;
};

// do not retain references across save() calls.
class IconConfigStore final {
public:
    static IconConfigStore& get();

    PaimonIconConfig const& config() const { return m_config; }

    void update(std::function<void(PaimonIconConfig&)> const& mutator);

    void resetToDefaults();

    void load();

    // master switch, backed by the "colorful-icons-enabled" mod setting.
    bool isFeatureEnabled() const;
    void setFeatureEnabled(bool enabled);

private:
    IconConfigStore();
    void persist();

    PaimonIconConfig m_config;
    bool m_loaded = false;
};

}  // namespace paimon::icons
