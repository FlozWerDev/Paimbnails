#pragma once

namespace geode::log {
template <typename... Args> void error(char const*, Args&&...) {}
template <typename... Args> void warn(char const*, Args&&...) {}
template <typename... Args> void info(char const*, Args&&...) {}
template <typename... Args> void debug(char const*, Args&&...) {}
}

namespace geode::prelude {
namespace log = geode::log;
}
