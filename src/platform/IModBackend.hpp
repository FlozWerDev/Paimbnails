#pragma once

// backend-agnostic mod storage: declared settings (mod.json) plus saved
// values (save container). Compiles with and without Geode.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace paimon::platform {

// every value type a backend must support.
template <typename T>
inline constexpr bool kSupportedValue =
    std::is_same_v<T, bool> || std::is_same_v<T, int> ||
    std::is_same_v<T, std::int64_t> || std::is_same_v<T, double> ||
    std::is_same_v<T, float> || std::is_same_v<T, std::string>;

class IModBackend {
public:
    using Observer = std::function<void(std::string_view)>;

    virtual ~IModBackend() = default;
    IModBackend(IModBackend const&) = delete;
    IModBackend& operator=(IModBackend const&) = delete;

    // true when key is a declared mod.json setting; otherwise it is a saved value.
    virtual bool hasSetting(std::string_view key) const = 0;
    virtual bool hasSaved(std::string_view key) const = 0;

    template <typename T>
    T getSetting(std::string_view key) const {
        static_assert(kSupportedValue<T>, "paimon::platform: unsupported setting type");
        if constexpr (std::is_same_v<T, bool>) return doGetSettingBool(key);
        else if constexpr (std::is_same_v<T, int>) return doGetSettingInt(key);
        else if constexpr (std::is_same_v<T, std::int64_t>) return doGetSettingInt64(key);
        else if constexpr (std::is_same_v<T, double>) return doGetSettingDouble(key);
        else if constexpr (std::is_same_v<T, float>) return doGetSettingFloat(key);
        else return doGetSettingString(key);
    }

    template <typename T>
    void setSetting(std::string_view key, T const& value) {
        static_assert(kSupportedValue<T>, "paimon::platform: unsupported setting type");
        if constexpr (std::is_same_v<T, bool>) return doSetSettingBool(key, value);
        else if constexpr (std::is_same_v<T, int>) return doSetSettingInt(key, value);
        else if constexpr (std::is_same_v<T, std::int64_t>) return doSetSettingInt64(key, value);
        else if constexpr (std::is_same_v<T, double>) return doSetSettingDouble(key, value);
        else if constexpr (std::is_same_v<T, float>) return doSetSettingFloat(key, value);
        else return doSetSettingString(key, value);
    }

    // missing keys yield def without dirtying the store.
    template <typename T>
    T getSaved(std::string_view key, T def) const {
        static_assert(kSupportedValue<T>, "paimon::platform: unsupported saved type");
        if (!hasSaved(key)) return def;
        if constexpr (std::is_same_v<T, bool>) return doGetSavedBool(key);
        else if constexpr (std::is_same_v<T, int>) return doGetSavedInt(key);
        else if constexpr (std::is_same_v<T, std::int64_t>) return doGetSavedInt64(key);
        else if constexpr (std::is_same_v<T, double>) return doGetSavedDouble(key);
        else if constexpr (std::is_same_v<T, float>) return doGetSavedFloat(key);
        else return doGetSavedString(key);
    }

    template <typename T>
    void setSaved(std::string_view key, T const& value) {
        static_assert(kSupportedValue<T>, "paimon::platform: unsupported saved type");
        if constexpr (std::is_same_v<T, bool>) return doSetSavedBool(key, value);
        else if constexpr (std::is_same_v<T, int>) return doSetSavedInt(key, value);
        else if constexpr (std::is_same_v<T, std::int64_t>) return doSetSavedInt64(key, value);
        else if constexpr (std::is_same_v<T, double>) return doSetSavedDouble(key, value);
        else if constexpr (std::is_same_v<T, float>) return doSetSavedFloat(key, value);
        else return doSetSavedString(key, value);
    }

    virtual std::filesystem::path getSaveDir() const = 0;
    virtual std::filesystem::path getConfigDir() const = 0;
    virtual void queueInMainThread(std::function<void()>&& fn) const = 0;
    virtual void saveData() const = 0;

    // fired with the changed key; observeAll fires for any key.
    virtual void observeSetting(std::string_view key, Observer cb) = 0;
    virtual void observeAll(Observer cb) = 0;

protected:
    IModBackend() = default;

    virtual bool doGetSettingBool(std::string_view key) const = 0;
    virtual int doGetSettingInt(std::string_view key) const = 0;
    virtual std::int64_t doGetSettingInt64(std::string_view key) const = 0;
    virtual double doGetSettingDouble(std::string_view key) const = 0;
    virtual float doGetSettingFloat(std::string_view key) const = 0;
    virtual std::string doGetSettingString(std::string_view key) const = 0;

    virtual void doSetSettingBool(std::string_view key, bool value) = 0;
    virtual void doSetSettingInt(std::string_view key, int value) = 0;
    virtual void doSetSettingInt64(std::string_view key, std::int64_t value) = 0;
    virtual void doSetSettingDouble(std::string_view key, double value) = 0;
    virtual void doSetSettingFloat(std::string_view key, float value) = 0;
    virtual void doSetSettingString(std::string_view key, std::string const& value) = 0;

    virtual bool doGetSavedBool(std::string_view key) const = 0;
    virtual int doGetSavedInt(std::string_view key) const = 0;
    virtual std::int64_t doGetSavedInt64(std::string_view key) const = 0;
    virtual double doGetSavedDouble(std::string_view key) const = 0;
    virtual float doGetSavedFloat(std::string_view key) const = 0;
    virtual std::string doGetSavedString(std::string_view key) const = 0;

    virtual void doSetSavedBool(std::string_view key, bool value) = 0;
    virtual void doSetSavedInt(std::string_view key, int value) = 0;
    virtual void doSetSavedInt64(std::string_view key, std::int64_t value) = 0;
    virtual void doSetSavedDouble(std::string_view key, double value) = 0;
    virtual void doSetSavedFloat(std::string_view key, float value) = 0;
    virtual void doSetSavedString(std::string_view key, std::string const& value) = 0;
};

} // namespace paimon::platform
