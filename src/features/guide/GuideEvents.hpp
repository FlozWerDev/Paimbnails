#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>
#include <string>

// menu listeners update guide visibility without reloading the scene.

namespace paimon::guide {

class GuideEnabledChangedEvent final
    : public geode::Event<GuideEnabledChangedEvent, bool(bool enabled), std::string>
{
public:
    using Event::Event;
};

// shared filter string for the global guide event.
inline char const* kGuideEventFilter = "guide.toggle";

} // namespace paimon::guide
