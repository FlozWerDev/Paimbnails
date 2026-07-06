#include "YtDlpBootstrap.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>
#include <fmt/format.h>
#include <memory>

using namespace geode::prelude;

namespace paimon::menumusic {

YtDlpBootstrap& YtDlpBootstrap::get() {
    static YtDlpBootstrap instance;
    return instance;
}

std::filesystem::path YtDlpBootstrap::bundledPath() const {
#ifdef GEODE_IS_WINDOWS
    constexpr const char* kName = "yt-dlp.exe";
#elif defined(GEODE_IS_MACOS)
    constexpr const char* kName = "yt-dlp_macos";
#else
    constexpr const char* kName = "yt-dlp_linux";
#endif
    return Mod::get()->getSaveDir() / "yt-dlp" / kName;
}

bool YtDlpBootstrap::exists() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(bundledPath(), ec);
}

void YtDlpBootstrap::uninstall() {
    std::error_code ec;
    std::filesystem::remove(bundledPath(), ec);
}

std::string YtDlpBootstrap::releaseUrl() {
#ifdef GEODE_IS_WINDOWS
    return "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
#elif defined(GEODE_IS_MACOS)
    return "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos";
#elif defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    // Mobile: yt-dlp requiere python; no es practico bundlearlo.
    return "";
#else
    return "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux";
#endif
}

void YtDlpBootstrap::ensureInstalled(
    BootstrapProgressCallback onProgress,
    BootstrapCompleteCallback onComplete
) {
    if (exists()) {
        auto path = geode::utils::string::pathToString(bundledPath());
        if (onComplete) {
            Loader::get()->queueInMainThread([onComplete, path]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (onComplete) onComplete(true, path);
            });
        }
        return;
    }

    auto url = releaseUrl();
    if (url.empty()) {
        if (onComplete) {
            Loader::get()->queueInMainThread([onComplete]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (onComplete) onComplete(false,
                    "yt-dlp is not supported on mobile. Use the 'Import local file' "
                    "option instead.");
            });
        }
        return;
    }

    bool expected = false;
    if (!m_downloading.compare_exchange_strong(expected, true)) {
        if (onComplete) {
            Loader::get()->queueInMainThread([onComplete]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (onComplete) onComplete(false, "A download is already in progress.");
            });
        }
        return;
    }

    auto destPath = bundledPath();
    std::error_code ec;
    std::filesystem::create_directories(destPath.parent_path(), ec);

    if (onProgress) {
        Loader::get()->queueInMainThread([onProgress]() {
            if (paimon::isRuntimeShuttingDown()) return;
            onProgress({"resolving", 0.f, "Contacting github.com..."});
        });
    }

    auto progressShared = std::make_shared<BootstrapProgressCallback>(std::move(onProgress));
    auto completeShared = std::make_shared<BootstrapCompleteCallback>(std::move(onComplete));

    auto req = web::WebRequest()
        .timeout(std::chrono::minutes(5))
        .userAgent("Paimbnails-MenuMusic/1.0 (yt-dlp-bootstrap)");

    // Geode despacha onProgress en un worker thread, asi que re-encolamos
    // en main thread antes de tocar UI.
    req.onProgress([progressShared](web::WebProgress const& p) {
        if (!progressShared || !*progressShared) return;
        auto downloaded = static_cast<uint64_t>(p.downloaded());
        auto total = static_cast<uint64_t>(p.downloadTotal());
        Loader::get()->queueInMainThread([progressShared, downloaded, total]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!progressShared || !*progressShared) return;
            float pct = (total > 0)
                ? std::min(0.95f, static_cast<float>(downloaded) / static_cast<float>(total) * 0.95f)
                : 0.f;
            std::string msg = (total > 0)
                ? fmt::format("Downloading yt-dlp... {:.2f} / {:.2f} MB",
                    downloaded / 1'048'576.0, total / 1'048'576.0)
                : fmt::format("Downloading yt-dlp... {:.2f} MB", downloaded / 1'048'576.0);
            (*progressShared)({"downloading", pct, std::move(msg)});
        });
    });

    WebHelper::dispatch(std::move(req), "GET", url,
        [this, destPath, progressShared, completeShared](web::WebResponse res) {
            m_downloading = false;

            auto fail = [completeShared](std::string err) {
                if (completeShared && *completeShared) (*completeShared)(false, std::move(err));
            };

            if (!res.ok()) {
                int code = res.code();
                fail(fmt::format(
                    "Failed to download yt-dlp (HTTP {}). "
                    "Check your internet connection or place the binary manually at:\n{}",
                    code, geode::utils::string::pathToString(destPath)));
                return;
            }

            auto data = res.data();
            // yt-dlp.exe pesa entre 14 y 20 MB tipicamente. Si vemos algo
            // debajo de 1MB asumimos que es un redirect roto o una pagina
            // de error HTML camuflada.
            if (data.size() < 1'000'000) {
                fail(fmt::format("Downloaded file is suspiciously small ({} bytes). "
                                 "The binary may have failed to fetch.", data.size()));
                return;
            }

            if (progressShared && *progressShared) {
                (*progressShared)({"installing", 0.97f,
                    fmt::format("Installing ({:.1f} MB)...", data.size() / 1'048'576.0)});
            }

            auto writeRes = geode::utils::file::writeBinary(destPath, data);
            if (!writeRes) {
                fail(fmt::format("Failed to write binary: {}", writeRes.unwrapErr()));
                return;
            }

#ifndef GEODE_IS_WINDOWS
            std::error_code permEc;
            std::filesystem::permissions(destPath,
                std::filesystem::perms::owner_exec |
                std::filesystem::perms::group_exec |
                std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add, permEc);
#endif

            if (progressShared && *progressShared) {
                (*progressShared)({"done", 1.f, "yt-dlp installed"});
            }
            if (completeShared && *completeShared) {
                (*completeShared)(true, geode::utils::string::pathToString(destPath));
            }
        }
    );
}

} // namespace paimon::menumusic
