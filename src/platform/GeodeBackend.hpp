#pragma once
#include "../utils/FilesystemPath.hpp"

#include "IModBackend.hpp"

#ifdef PAIM_USE_GEODE
// only platform header allowed to include Geode.
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <memory>
#include <string>

namespace paimon::platform {

// iModBackend over geode::Mod::get(). Type-agnostic observers filter the
// broadcast instead of guessing each setting's declared type.
class GeodeBackend final : public IModBackend {
public:
    static IModBackend& instance() {
        static GeodeBackend s_backend;
        return s_backend;
    }

    GeodeBackend(GeodeBackend const&) = delete;
    GeodeBackend& operator=(GeodeBackend const&) = delete;

    bool hasSetting(std::string_view key) const override {
        return geode::Mod::get()->hasSetting(key);
    }
    bool hasSaved(std::string_view key) const override {
        return geode::Mod::get()->hasSavedValue(key);
    }
    std::filesystem::path getSaveDir() const override {
        return paimon::paths::toStdPath(geode::Mod::get()->getSaveDir());
    }
    std::filesystem::path getConfigDir() const override {
        return paimon::paths::toStdPath(geode::Mod::get()->getConfigDir());
    }
    void queueInMainThread(std::function<void()>&& fn) const override {
        geode::queueInMainThread(std::move(fn));
    }
    void saveData() const override {
        (void)geode::Mod::get()->saveData();
    }
    void observeSetting(std::string_view key, Observer cb) override {
        std::string watched(key);
        (void)geode::listenForAllSettingChanges(
            [watched = std::move(watched), cb = std::move(cb)](
                std::string_view changed, std::shared_ptr<geode::SettingV3>) {
                if (changed == watched) cb(changed);
            });
    }
    void observeAll(Observer cb) override {
        (void)geode::listenForAllSettingChanges(
            [cb = std::move(cb)](
                std::string_view changed, std::shared_ptr<geode::SettingV3>) {
                cb(changed);
            });
    }

protected:
    bool doGetSettingBool(std::string_view key) const override {
        return geode::Mod::get()->getSettingValue<bool>(key);
    }
    int doGetSettingInt(std::string_view key) const override {
        return geode::Mod::get()->getSettingValue<int>(key);
    }
    std::int64_t doGetSettingInt64(std::string_view key) const override {
        return geode::Mod::get()->getSettingValue<std::int64_t>(key);
    }
    double doGetSettingDouble(std::string_view key) const override {
        return geode::Mod::get()->getSettingValue<double>(key);
    }
    float doGetSettingFloat(std::string_view key) const override {
        return geode::Mod::get()->getSettingValue<float>(key);
    }
    std::string doGetSettingString(std::string_view key) const override {
        return geode::Mod::get()->getSettingValue<std::string>(key);
    }

    void doSetSettingBool(std::string_view key, bool value) override {
        (void)geode::Mod::get()->setSettingValue(key, value);
    }
    void doSetSettingInt(std::string_view key, int value) override {
        (void)geode::Mod::get()->setSettingValue(key, value);
    }
    void doSetSettingInt64(std::string_view key, std::int64_t value) override {
        (void)geode::Mod::get()->setSettingValue(key, value);
    }
    void doSetSettingDouble(std::string_view key, double value) override {
        (void)geode::Mod::get()->setSettingValue(key, value);
    }
    void doSetSettingFloat(std::string_view key, float value) override {
        (void)geode::Mod::get()->setSettingValue(key, value);
    }
    void doSetSettingString(std::string_view key, std::string const& value) override {
        (void)geode::Mod::get()->setSettingValue(key, value);
    }

    bool doGetSavedBool(std::string_view key) const override {
        return geode::Mod::get()->getSavedValue(key, bool{});
    }
    int doGetSavedInt(std::string_view key) const override {
        return geode::Mod::get()->getSavedValue(key, int{});
    }
    std::int64_t doGetSavedInt64(std::string_view key) const override {
        return geode::Mod::get()->getSavedValue(key, std::int64_t{});
    }
    double doGetSavedDouble(std::string_view key) const override {
        return geode::Mod::get()->getSavedValue(key, double{});
    }
    float doGetSavedFloat(std::string_view key) const override {
        return geode::Mod::get()->getSavedValue(key, float{});
    }
    std::string doGetSavedString(std::string_view key) const override {
        return geode::Mod::get()->getSavedValue(key, std::string{});
    }

    void doSetSavedBool(std::string_view key, bool value) override {
        (void)geode::Mod::get()->setSavedValue(key, value);
    }
    void doSetSavedInt(std::string_view key, int value) override {
        (void)geode::Mod::get()->setSavedValue(key, value);
    }
    void doSetSavedInt64(std::string_view key, std::int64_t value) override {
        (void)geode::Mod::get()->setSavedValue(key, value);
    }
    void doSetSavedDouble(std::string_view key, double value) override {
        (void)geode::Mod::get()->setSavedValue(key, value);
    }
    void doSetSavedFloat(std::string_view key, float value) override {
        (void)geode::Mod::get()->setSavedValue(key, value);
    }
    void doSetSavedString(std::string_view key, std::string const& value) override {
        (void)geode::Mod::get()->setSavedValue(key, value);
    }

private:
    GeodeBackend() = default;
};

} // namespace paimon::platform

#endif // PAIM_USE_GEODE
