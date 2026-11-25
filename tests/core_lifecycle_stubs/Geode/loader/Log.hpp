#pragma once

#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif
#include <fmt/format.h>

namespace geode::log {
template <typename... Args> inline void error(char const*, Args&&...) {}
template <typename... Args> inline void warn(char const*, Args&&...) {}
template <typename... Args> inline void info(char const*, Args&&...) {}
template <typename... Args> inline void debug(char const*, Args&&...) {}
} // namespace geode::log

namespace geode::utils::thread {
template <typename T> inline void setName(T const&) {}
} // namespace geode::utils::thread

namespace geode::prelude {
namespace log = geode::log;
} // namespace geode::prelude
