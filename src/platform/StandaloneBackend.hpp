#pragma once

#include "IModBackend.hpp"

#include <mutex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace paimon::platform {

// in-memory backend for standalone builds and host tests. Hosts declare
// known setting keys up front so hasSetting() mirrors mod.json; anything
// else behaves as a saved value.
class StandaloneBackend final : public IModBackend {
public:
    using Value = std::variant<bool, int, std::int64_t, double, float, std::string>;
    using Map = std::unordered_map<std::string, Value>;

    StandaloneBackend() : StandaloneBackend(currentDir(), currentDir()) {}

    explicit StandaloneBackend(
        std::filesystem::path saveDir, std::filesystem::path configDir = {})
        : m_saveDir(std::move(saveDir)), m_configDir(std::move(configDir)) {
        if (m_configDir.empty()) m_configDir = m_saveDir;
    }

    static StandaloneBackend& defaultInstance() {
        static StandaloneBackend s_backend;
        return s_backend;
    }

    // seeds a declared setting; reads/writes keep the stored type.
    void declareSetting(std::string_view key, Value def = Value{bool{}}) {
        std::lock_guard lock(m_mutex);
        m_settings.emplace(std::string(key), std::move(def));
    }

    void setSaveDir(std::filesystem::path dir) {
        std::lock_guard lock(m_mutex);
        m_saveDir = std::move(dir);
    }
    void setConfigDir(std::filesystem::path dir) {
        std::lock_guard lock(m_mutex);
        m_configDir = std::move(dir);
    }

    bool hasSetting(std::string_view key) const override {
        std::lock_guard lock(m_mutex);
        return m_settings.find(std::string(key)) != m_settings.end();
    }
    bool hasSaved(std::string_view key) const override {
        std::lock_guard lock(m_mutex);
        return m_saved.find(std::string(key)) != m_saved.end();
    }
    std::filesystem::path getSaveDir() const override {
        std::lock_guard lock(m_mutex);
        return m_saveDir;
    }
    std::filesystem::path getConfigDir() const override {
        std::lock_guard lock(m_mutex);
        return m_configDir;
    }
    // standalone has no event loop; runs inline.
    void queueInMainThread(std::function<void()>&& fn) const override {
        fn();
    }
    // values live in memory; hosts persist explicitly when needed.
    void saveData() const override {}
    void observeSetting(std::string_view key, Observer cb) override {
        std::lock_guard lock(m_mutex);
        m_keyObservers[std::string(key)].push_back(std::move(cb));
    }
    void observeAll(Observer cb) override {
        std::lock_guard lock(m_mutex);
        m_allObservers.push_back(std::move(cb));
    }

protected:
    bool doGetSettingBool(std::string_view key) const override {
        return read<bool>(m_settings, key);
    }
    int doGetSettingInt(std::string_view key) const override {
        return read<int>(m_settings, key);
    }
    std::int64_t doGetSettingInt64(std::string_view key) const override {
        return read<std::int64_t>(m_settings, key);
    }
    double doGetSettingDouble(std::string_view key) const override {
        return read<double>(m_settings, key);
    }
    float doGetSettingFloat(std::string_view key) const override {
        return read<float>(m_settings, key);
    }
    std::string doGetSettingString(std::string_view key) const override {
        return read<std::string>(m_settings, key);
    }

    void doSetSettingBool(std::string_view key, bool value) override {
        writeSetting(key, Value{value});
    }
    void doSetSettingInt(std::string_view key, int value) override {
        writeSetting(key, Value{value});
    }
    void doSetSettingInt64(std::string_view key, std::int64_t value) override {
        writeSetting(key, Value{value});
    }
    void doSetSettingDouble(std::string_view key, double value) override {
        writeSetting(key, Value{value});
    }
    void doSetSettingFloat(std::string_view key, float value) override {
        writeSetting(key, Value{value});
    }
    void doSetSettingString(std::string_view key, std::string const& value) override {
        writeSetting(key, Value{value});
    }

    bool doGetSavedBool(std::string_view key) const override {
        return read<bool>(m_saved, key);
    }
    int doGetSavedInt(std::string_view key) const override {
        return read<int>(m_saved, key);
    }
    std::int64_t doGetSavedInt64(std::string_view key) const override {
        return read<std::int64_t>(m_saved, key);
    }
    double doGetSavedDouble(std::string_view key) const override {
        return read<double>(m_saved, key);
    }
    float doGetSavedFloat(std::string_view key) const override {
        return read<float>(m_saved, key);
    }
    std::string doGetSavedString(std::string_view key) const override {
        return read<std::string>(m_saved, key);
    }

    void doSetSavedBool(std::string_view key, bool value) override {
        writeSaved(key, Value{value});
    }
    void doSetSavedInt(std::string_view key, int value) override {
        writeSaved(key, Value{value});
    }
    void doSetSavedInt64(std::string_view key, std::int64_t value) override {
        writeSaved(key, Value{value});
    }
    void doSetSavedDouble(std::string_view key, double value) override {
        writeSaved(key, Value{value});
    }
    void doSetSavedFloat(std::string_view key, float value) override {
        writeSaved(key, Value{value});
    }
    void doSetSavedString(std::string_view key, std::string const& value) override {
        writeSaved(key, Value{value});
    }

private:
    // exact hit wins; numeric (non-bool) widths convert, like Geode does.
    template <typename T>
    static T read(Map const& map, std::string_view key) {
        auto it = map.find(std::string(key));
        if (it == map.end()) return T{};
        if (T const* hit = std::get_if<T>(&it->second)) return *hit;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            return std::visit([](auto const& v) -> T {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_arithmetic_v<V> && !std::is_same_v<V, bool>) {
                    return static_cast<T>(v);
                }
                return T{};
            }, it->second);
        } else {
            return T{};
        }
    }

    static std::filesystem::path currentDir() {
        std::error_code ec;
        auto dir = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(".") : dir;
    }

    void writeSetting(std::string_view key, Value value) {
        std::vector<Observer> pending;
        {
            std::lock_guard lock(m_mutex);
            m_settings[std::string(key)] = std::move(value);
            collect(key, pending);
        }
        for (auto& cb : pending) cb(key);
    }

    void writeSaved(std::string_view key, Value value) {
        std::lock_guard lock(m_mutex);
        m_saved[std::string(key)] = std::move(value);
    }

    // copies matching observers; callers invoke without the lock held.
    void collect(std::string_view key, std::vector<Observer>& out) {
        if (auto it = m_keyObservers.find(std::string(key)); it != m_keyObservers.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
        out.insert(out.end(), m_allObservers.begin(), m_allObservers.end());
    }

    mutable std::mutex m_mutex;
    Map m_settings;
    Map m_saved;
    std::unordered_map<std::string, std::vector<Observer>> m_keyObservers;
    std::vector<Observer> m_allObservers;
    std::filesystem::path m_saveDir;
    std::filesystem::path m_configDir;
};

} // namespace paimon::platform
