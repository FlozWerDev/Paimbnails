#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/HttpClient.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "QualityConfig.hpp"
#include <Geode/binding/PlatformToolbox.hpp>
#include <array>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

extern void clearProfileImgCache();

namespace {
struct MaintenanceStats {
    size_t removedEntries = 0;
    size_t checkedFiles = 0;
    size_t removedCorruptFiles = 0;
    size_t readyDirectories = 0;
    size_t errors = 0;
};

bool looksCorrupt(std::filesystem::path const& path, uintmax_t fileSize) {
    auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(path.extension()));
    if (ext == ".tmp" || ext == ".part" || ext == ".download" || ext == ".crdownload") {
        return true;
    }

    if (fileSize != 0) {
        return false;
    }

    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" ||
           ext == ".webp" || ext == ".dat" || ext == ".rgb";
}

void purgeDirectoryTree(std::filesystem::path const& dir, MaintenanceStats& stats) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        return;
    }

    auto removed = std::filesystem::remove_all(dir, ec);
    if (ec) {
        stats.errors++;
        return;
    }

    stats.removedEntries += static_cast<size_t>(removed);
}

void sanitizeDirectory(std::filesystem::path const& dir, MaintenanceStats& stats) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        return;
    }

    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            stats.errors++;
            break;
        }

        if (!it->is_regular_file()) {
            continue;
        }

        stats.checkedFiles++;
        auto fileSize = it->file_size(ec);
        if (ec) {
            stats.errors++;
            ec.clear();
            continue;
        }

        if (!looksCorrupt(it->path(), fileSize)) {
            continue;
        }

        std::filesystem::remove(it->path(), ec);
        if (ec) {
            stats.errors++;
            ec.clear();
            continue;
        }

        stats.removedCorruptFiles++;
        stats.removedEntries++;
    }
}

void ensureDirectory(std::filesystem::path const& dir, MaintenanceStats& stats) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec || !std::filesystem::exists(dir, ec)) {
        stats.errors++;
        return;
    }
    stats.readyDirectories++;
}

MaintenanceStats runMaintenanceCleanup() {
    MaintenanceStats stats;

    ThumbnailLoader::get().cleanup();
    ThumbnailLoader::get().clearPendingQueue();
    ThumbnailLoader::get().clearCache();
    ThumbnailLoader::get().clearDiskCache();

    ProfileThumbs::get().clearPendingDownloads();
    ProfileThumbs::get().clearNoProfileCache();
    ProfileThumbs::get().clearAllCache();

    ProfileMusicManager::get().stopProfileMusic();
    ProfileMusicManager::get().stopPreview();
    ProfileMusicManager::get().clearCache();

    AnimatedGIFSprite::clearCache();
    clearProfileImgCache();
    HttpClient::get().cleanTasks();

    auto saveDir = Mod::get()->getSaveDir();

    purgeDirectoryTree(saveDir / "gif_cache", stats);
    purgeDirectoryTree(saveDir / "thumbnails" / "profiles", stats);

    // also clean quality-aware dirs
    sanitizeDirectory(paimon::quality::cacheDir(), stats);
    sanitizeDirectory(paimon::quality::cacheDir() / "profiles", stats);
    sanitizeDirectory(paimon::quality::cacheDir() / "gifs", stats);
    sanitizeDirectory(saveDir / "profileimg_cache", stats);

    std::array<std::filesystem::path, 5> requiredDirs = {
        paimon::quality::cacheDir(),
        paimon::quality::cacheDir() / "gifs",
        saveDir / "profile_music",
        saveDir / "profileimg_cache",
        paimon::quality::cacheDir() / "profiles"
    };

    for (auto const& dir : requiredDirs) {
        ensureDirectory(dir, stats);
    }

    {
        std::error_code ec;
        auto probePath = paimon::quality::cacheDir() / ".maintenance_probe";
        std::ofstream probeFile(probePath, std::ios::binary | std::ios::trunc);
        if (!probeFile) {
            stats.errors++;
        } else {
            probeFile << "ok";
            if (!probeFile.good()) {
                stats.errors++;
            }
            probeFile.close();
            if (probeFile.fail()) {
                stats.errors++;
            }
            std::filesystem::remove(probePath, ec);
            if (ec) stats.errors++;
        }
    }

    return stats;
}

} // namespace

$execute {
    ButtonSettingPressedEventV3(Mod::get(), "maintenance-cleanup").listen([](auto buttonKey) {
        if (buttonKey != "run") return;

        auto stats = runMaintenanceCleanup();

        if (stats.errors == 0) {
            auto msg = fmt::format(
                "Limpieza completada. Revisados: {} | Corruptos borrados: {}",
                stats.checkedFiles,
                stats.removedCorruptFiles
            );
            PaimonNotify::create(msg, NotificationIcon::Success)->show();
        } else {
            auto msg = fmt::format(
                "Limpieza completada con avisos ({}). Revisados: {} | Corruptos borrados: {}",
                stats.errors,
                stats.checkedFiles,
                stats.removedCorruptFiles
            );
            PaimonNotify::create(msg, NotificationIcon::Warning)->show();
        }
    }).leak();

    ButtonSettingPressedEventV3(Mod::get(), "maintenance-refresh-mod-code").listen([](auto buttonKey) {
        if (buttonKey != "run") return;

        auto* gm = GameManager::get();
        auto* am = GJAccountManager::get();
        std::string username = gm ? gm->m_playerName : "";
        int accountID = am ? am->m_accountID : 0;

        if (username.empty() || accountID <= 0) {
            PaimonNotify::create("Necesitas iniciar sesion para obtener el mod code.", NotificationIcon::Error)->show();
            return;
        }

        std::string oldCode = HttpClient::get().getModCode();
        PaimonNotify::create("Verificando permisos de mod/admin...", NotificationIcon::Info)->show();

        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [oldCode](bool isMod, bool isAdmin) {
            Loader::get()->queueInMainThread([oldCode, isMod, isAdmin]() {
                bool effectiveMod = isMod || isAdmin;
                Mod::get()->setSavedValue<bool>("is-verified-admin", isAdmin);
                Mod::get()->setSavedValue<bool>("is-verified-moderator", effectiveMod);

                if (!effectiveMod) {
                    PaimonNotify::create("Tu cuenta no tiene permisos de mod/admin.", NotificationIcon::Error)->show();
                    return;
                }

                // Check if GDBrowser verification failed (code not generated/refreshed)
                bool gdFailed = Mod::get()->getSavedValue<bool>("gd-verification-failed", false);
                auto newCode = HttpClient::get().getModCode();

                if (gdFailed) {
                    if (newCode.empty()) {
                        PaimonNotify::create("Error: GDBrowser no disponible. No se pudo generar mod code. Reintenta mas tarde.", NotificationIcon::Error)->show();
                    } else {
                        PaimonNotify::create("Advertencia: GDBrowser fallo, no se pudo refrescar el codigo. El codigo actual puede no funcionar.", NotificationIcon::Warning)->show();
                    }
                    return;
                }

                if (newCode.empty()) {
                    PaimonNotify::create("Error: el servidor no envio mod code. Reintenta mas tarde.", NotificationIcon::Error)->show();
                    return;
                }

                if (newCode != oldCode) {
                    PaimonNotify::create("Mod code actualizado y sincronizado correctamente.", NotificationIcon::Success)->show();
                } else {
                    PaimonNotify::create("Mod code validado correctamente.", NotificationIcon::Success)->show();
                }
            });
        });
    }).leak();

    ButtonSettingPressedEventV3(Mod::get(), "maintenance-copy-mod-code").listen([](auto buttonKey) {
        if (buttonKey != "run") return;

        std::string code = HttpClient::get().getModCode();
        if (code.empty()) {
            PaimonNotify::create("No tienes un mod code generado. Usa 'Fetch Mod Code' primero.", NotificationIcon::Info)->show();
            return;
        }

        PlatformToolbox::copyToClipboard(code);
        PaimonNotify::create("Mod code copiado al portapapeles.", NotificationIcon::Success)->show();
    }).leak();
}
