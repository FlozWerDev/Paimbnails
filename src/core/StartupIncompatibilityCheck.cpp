#include <Geode/Geode.hpp>
#include <Geode/ui/PopupManager.hpp>
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
            PopupManager::get().alert(
                tr("startup.incompat.disable_error_title"),
                tr("startup.incompat.disable_error_missing")
            ).showQueue();
            return;
        }

        auto result = mod->disable();
        if (!result) {
            PopupManager::get().alert(
                tr("startup.incompat.disable_error_title"),
                result.unwrapErr()
            ).showQueue();
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

        auto popup = PopupManager::get().quickPopup(
            tr("startup.incompat.title"),
            buildConflictMessage(mod, incompatible),
            tr("startup.incompat.keep_enabled"),
            tr("startup.incompat.disable_restart"),
            [mod](FLAlertLayer*, bool btn2) {
                if (!btn2) {
                    return;
                }
                disableModAndRestart(mod);
            },
            360.f
        );
        popup.setPriority(true);
        popup.blockClosingFor(0.8f);
        popup.showQueue();
        return;
    }
}
