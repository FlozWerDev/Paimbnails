#pragma once

#include <Geode/loader/Log.hpp>

#include <any>
#include <string>
#include <string_view>
#include <unordered_map>

namespace geode {

class Mod {
public:
    static Mod* get() { return active(); }
    static void set(Mod* mod) { active() = mod; }

    bool hasSetting(std::string_view key) const {
        return m_settings.contains(std::string(key));
    }

    template <class T>
    T getSettingValue(std::string const& key) const {
        return get<T>(m_settings, key, T{});
    }

    template <class T>
    void setSettingValue(std::string const& key, T value) {
        m_settings.insert_or_assign(key, std::move(value));
    }

    template <class T>
    T getSavedValue(std::string const& key, T fallback) const {
        return get<T>(m_saved, key, std::move(fallback));
    }

    template <class T>
    void setSavedValue(std::string const& key, T value) {
        m_saved.insert_or_assign(key, std::move(value));
    }

    void eraseSetting(std::string const& key) { m_settings.erase(key); }
    void eraseSaved(std::string const& key) { m_saved.erase(key); }

private:
    static Mod*& active() {
        static Mod fallback;
        static Mod* current = &fallback;
        return current;
    }

    template <class T>
    static T get(std::unordered_map<std::string, std::any> const& values,
                 std::string const& key, T fallback) {
        auto const it = values.find(key);
        if (it == values.end()) return fallback;
        if (auto const* value = std::any_cast<T>(&it->second)) return *value;
        return fallback;
    }

    std::unordered_map<std::string, std::any> m_settings;
    std::unordered_map<std::string, std::any> m_saved;
};

namespace prelude {
using geode::Mod;
}

} // namespace geode
