#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Dispatch.hpp>

#if __has_include(<globed/core/Event.hpp>)

#if GEODE_COMP_GD_VERSION >= 22091
// globed 2.2's public headers still use the previous two-argument export
// macro and ThreadSafeGlobalEvent (removed in Geode v6.0.0-alpha.3 when all
// events became thread-safe). Keep that source compatibility local here.
namespace gd { using string = std::string; }
namespace geode {
    template <class Marker, class Func, class... FArgs>
    using ThreadSafeGlobalEvent = GlobalEvent<Marker, Func, FArgs...>;
}
#pragma push_macro("GEODE_EVENT_EXPORT")
#pragma push_macro("MY_MOD_ID")
#undef GEODE_EVENT_EXPORT
#define GEODE_EVENT_EXPORT(fnPtr, args) { \
    static auto listener = geode::geode_internal::callEventExportListener<decltype(fnPtr)>( \
        GEODE_EVENT_EXPORT_ID_FOR(#fnPtr, #args)); \
    if (!listener) return geode::Err("Globed API is unavailable"); \
    return listener args; \
}
#endif

#include <globed/soft-link/API.hpp>
#include <globed/core/Event.hpp>
#define PAIMON_VERSUS_GLOBED 1

#if GEODE_COMP_GD_VERSION >= 22091
#pragma pop_macro("MY_MOD_ID")
#pragma pop_macro("GEODE_EVENT_EXPORT")
#endif

#endif
