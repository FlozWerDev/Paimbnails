#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include "../utils/Localization.hpp"

using namespace geode::prelude;

namespace {
    struct StartupIncompatibleMod {
        char const* id;
        char const* displayName;
        char const* reasonKey;
    };

    constexpr StartupIncompatibleMod kStartupIncompatibleMods[] = {
        {
            "cdc.level_thumbnails",
            "Level Thumbnails",
            "startup.incompat.reason.level_thumbnails"
        },
    };

    bool s_startupIncompatibilityPopupShown = false;

    std::string tr(std::string const& key) {
        return Localization::get().getString(key);
    }

    std::string buildConflictMessage(Mod* mod, StartupIncompatibleMod const& info) {
        auto modName = mod ? std::string(mod->getName()) : std::string(info.displayName);
        return fmt::format(
            fmt::runtime(tr("startup.incompat.body")),
            modName,
            tr(info.reasonKey)
        );
    }

    void disableModAndRestart(Mod* mod) {
        if (!mod) {
            FLAlertLayer::create(
                tr("startup.incompat.disable_error_title").c_str(),
                tr("startup.incompat.disable_error_missing").c_str(),
                "OK"
            )->show();
            return;
        }

        auto result = mod->disable();
        if (!result) {
            FLAlertLayer::create(
                tr("startup.incompat.disable_error_title").c_str(),
                result.unwrapErr().c_str(),
                "OK"
            )->show();
            return;
        }

        geode::utils::game::restart(true);
    }
}

void PaimonCheckStartupIncompatibilities() {
    if (s_startupIncompatibilityPopupShown) {
        return;
    }

    auto* loader = Loader::get();
    if (!loader) {
        return;
    }

    for (auto const& incompatible : kStartupIncompatibleMods) {
        auto* mod = loader->getLoadedMod(incompatible.id);
        if (!mod) {
            continue;
        }

        s_startupIncompatibilityPopupShown = true;

        createQuickPopup(
            tr("startup.incompat.title").c_str(),
            buildConflictMessage(mod, incompatible),
            tr("startup.incompat.keep_enabled").c_str(),
            tr("startup.incompat.disable_restart").c_str(),
            360.f,
            [mod](FLAlertLayer*, bool btn2) {
                if (!btn2) {
                    return;
                }
                disableModAndRestart(mod);
            },
            true,
            false
        );
        return;
    }
}
