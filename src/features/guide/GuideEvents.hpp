#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>
#include <string>

// Events for the "Paimon Guide" system. Emitted when the Guide toggle changes
// (PaimonHubLayer / PaimonGuideService::setEnabled); MenuLayer listens to
// enable/disable the dynamic Paimon without reloading the scene.

namespace paimon::guide {

class GuideEnabledChangedEvent final
    : public geode::Event<GuideEnabledChangedEvent, bool(bool enabled), std::string>
{
public:
    using Event::Event;
};

// Shared filter string for the global guide event.
inline char const* kGuideEventFilter = "guide.toggle";

} // namespace paimon::guide
