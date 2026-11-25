#pragma once

#include <Geode/loader/Dispatch.hpp>

// more Icons 2.1's optional event API passes the function pointer to infer
// its type. The GD 2.2091 SDK now takes that type as a template argument.
#if GEODE_COMP_GD_VERSION >= 22091
namespace geode::geode_internal {
template <class Fn>
inline auto callEventExportListener(Fn, std::string eventID) {
    return callEventExportListener<Fn>(std::move(eventID));
}
} // namespace geode::geode_internal
#endif

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>
