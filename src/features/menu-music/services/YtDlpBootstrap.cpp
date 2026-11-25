#include "../../../utils/FilesystemPath.hpp"
#include "YtDlpBootstrap.hpp"
#include "ToolBinaryFetch.hpp"

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
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "yt-dlp" / kName;
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
    // mobile: yt-dlp requiere python; no es practico bundlearlo.
    return "";
#else
    return "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux";
#endif
}

void YtDlpBootstrap::ensureInstalled(
    BootstrapProgressCallback onProgress,
    BootstrapCompleteCallback onComplete
) {
    auto destPath = bundledPath();
    toolfetch::FetchConfig config;
    config.url = releaseUrl();
    config.unsupportedMessage =
        "yt-dlp is not supported on mobile. Use the 'Import local file' "
        "option instead.";
    config.userAgent = "Paimbnails-MenuMusic/1.0 (yt-dlp-bootstrap)";
    config.timeout = std::chrono::minutes(5);
    config.resolvingMessage = "Contacting github.com...";
    config.downloadingVerb = "Downloading yt-dlp... ";
    config.downloadCap = 0.95f;
    config.httpErrorFor = [](int code) {
        return fmt::format(
            "Failed to download yt-dlp ({} {}).\n"
            "Try again or install it at the destination below.",
            code < 0 ? "request error" : "HTTP", code);
    };
    config.sizeErrorFor = [](std::size_t bytes) {
        return fmt::format("Downloaded file is suspiciously small ({} bytes). "
                           "The binary may have failed to fetch.", bytes);
    };

    toolfetch::fetchToolBinary<BootstrapProgress>(
        std::move(config), destPath, exists(), destPath.parent_path(),
        m_downloading, std::move(onProgress), std::move(onComplete),
        [destPath](geode::ByteVector data,
                   std::shared_ptr<BootstrapProgressCallback> progressShared,
                   std::shared_ptr<BootstrapCompleteCallback> completeShared) {
            // yt-dlp.exe is ~14-20mb; the fetch helper already rejected
            // anything under 1mb as a broken redirect or html error page.
            if (progressShared && *progressShared) {
                (*progressShared)({"installing", 0.97f,
                    fmt::format("Installing ({:.1f} MB)...", data.size() / 1'048'576.0)});
            }

            auto writeRes = geode::utils::file::writeBinary(paimon::paths::toSdkPath(destPath), data);
            if (!writeRes) {
                if (completeShared && *completeShared) {
                    (*completeShared)(false,
                        fmt::format("Failed to write binary: {}", writeRes.unwrapErr()));
                }
                return;
            }

            toolfetch::makeExecutable(destPath);

            if (progressShared && *progressShared) {
                (*progressShared)({"done", 1.f, "yt-dlp installed"});
            }
            if (completeShared && *completeShared) {
                (*completeShared)(true, paimon::paths::toUtf8(destPath));
            }
        });
}

} // namespace paimon::menumusic
