#pragma once
#include <Geode/Geode.hpp>

namespace PaimonDebug {
    inline bool s_debugEnabled = false;

    inline void setEnabled(bool enabled) {
        s_debugEnabled = enabled;
    }

    inline bool isEnabled() {
        return s_debugEnabled;
    }

    template<typename... Args>
    void log(fmt::format_string<Args...> format, Args&&... args) {
        if (s_debugEnabled) {
            geode::log::debug(format, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void warn(fmt::format_string<Args...> format, Args&&... args) {
        if (s_debugEnabled) {
            geode::log::warn(format, std::forward<Args>(args)...);
        }
    }
    
    inline void log(geode::ZStringView str) {
        if (s_debugEnabled) {
            geode::log::debug("{}", str);
        }
    }

    inline void warn(geode::ZStringView str) {
        if (s_debugEnabled) {
            geode::log::warn("{}", str);
        }
    }
}
