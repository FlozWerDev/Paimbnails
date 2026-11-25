#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/SafeUnzip.hpp"
#include "FfmpegBootstrap.hpp"
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

FfmpegBootstrap& FfmpegBootstrap::get() {
    static FfmpegBootstrap instance;
    return instance;
}

std::filesystem::path FfmpegBootstrap::bundledPath() const {
#ifdef GEODE_IS_WINDOWS
    constexpr const char* kName = "ffmpeg.exe";
#else
    constexpr const char* kName = "ffmpeg";
#endif
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "ffmpeg" / kName;
}

bool FfmpegBootstrap::exists() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(bundledPath(), ec);
}

void FfmpegBootstrap::uninstall() {
    std::error_code ec;
    std::filesystem::remove_all(paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "ffmpeg", ec);
}

std::string FfmpegBootstrap::releaseUrl() {
#ifdef GEODE_IS_WINDOWS
    // btbn essentials build.
    return "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/"
           "ffmpeg-master-latest-win64-gpl.zip";
#elif defined(GEODE_IS_MACOS)
    // evermeet redirects to the latest macos build.
    return "https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip";
#elif defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    // mobile downloads are unsupported.
    return "";
#else
    // btbn linux zip build.
    return "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/"
           "ffmpeg-master-latest-linux64-gpl.zip";
#endif
}

std::string FfmpegBootstrap::archiveEntry() {
#ifdef GEODE_IS_WINDOWS
    return "ffmpeg.exe";
#elif defined(GEODE_IS_MACOS)
    return "ffmpeg";
#else
    return "ffmpeg";
#endif
}

void FfmpegBootstrap::ensureInstalled(
    FfmpegBootstrapProgressCallback onProgress,
    FfmpegBootstrapCompleteCallback onComplete
) {
    auto destBinary = bundledPath();
    auto destDir = destBinary.parent_path();
    auto archiveEntryName = archiveEntry();

    toolfetch::FetchConfig config;
    config.url = releaseUrl();
    config.unsupportedMessage =
        "ffmpeg auto-install is not supported on this platform. "
        "Install ffmpeg manually and make sure it is in your PATH.";
    config.userAgent = "Paimbnails-MenuMusic/1.0 (ffmpeg-bootstrap)";
    config.timeout = std::chrono::minutes(10);
    config.resolvingMessage = "Contacting ffmpeg mirror...";
    config.downloadingVerb = "Downloading ffmpeg... ";
    config.downloadCap = 0.90f;
    config.httpErrorFor = [](int code) {
        return fmt::format(
            "Failed to download ffmpeg ({} {}).\n"
            "Try again later or install ffmpeg manually.",
            code < 0 ? "request error" : "HTTP", code);
    };
    config.sizeErrorFor = [](std::size_t bytes) {
        return fmt::format(
            "Downloaded ffmpeg archive is too small ({} bytes) - the "
            "mirror may be down.", bytes);
    };

    toolfetch::fetchToolBinary<FfmpegBootstrapProgress>(
        std::move(config), destBinary, exists(), destDir,
        m_downloading, std::move(onProgress), std::move(onComplete),
        [destDir, destBinary, archiveEntryName](
                geode::ByteVector data,
                std::shared_ptr<FfmpegBootstrapProgressCallback> progressShared,
                std::shared_ptr<FfmpegBootstrapCompleteCallback> completeShared) {
            auto fail = [completeShared](std::string err) {
                if (completeShared && *completeShared)
                    (*completeShared)(false, std::move(err));
            };

            if (progressShared && *progressShared) {
                (*progressShared)({"extracting", 0.92f,
                    fmt::format("Extracting archive ({:.1f} MB)...",
                        data.size() / 1'048'576.0)});
            }

            // write the archive to disk for geode::unzip.
            auto zipPath = destDir / "ffmpeg-download.zip";
            auto writeRes = geode::utils::file::writeBinary(paimon::paths::toSdkPath(zipPath), data);
            if (!writeRes) {
                fail(fmt::format("Failed to write ffmpeg archive: {}",
                    writeRes.unwrapErr()));
                return;
            }

            std::string zipErr;
            paimon::zip::ZipInspectionLimits ffLimits;
            ffLimits.maxEntries = 1024;
            ffLimits.maxSingleEntryBytes = 384ull * 1024 * 1024;
            ffLimits.maxTotalUncompressedBytes = 768ull * 1024 * 1024;
            ffLimits.maxAllowedRatio = 100;
            if (!paimon::zip::inspectZipFile(zipPath, ffLimits, zipErr)) {
                std::error_code rmEc;
                std::filesystem::remove(zipPath, rmEc);
                fail(fmt::format("Unsafe ffmpeg archive: {}", zipErr));
                return;
            }

            // find the binary entry; mirror layouts vary.
            auto unzipRes = geode::utils::file::Unzip::create(paimon::paths::toSdkPath(zipPath));
            if (!unzipRes) {
                std::error_code rmEc;
                std::filesystem::remove(zipPath, rmEc);
                fail(fmt::format("Failed to open ffmpeg zip: {}",
                    unzipRes.unwrapErr()));
                return;
            }
            auto unzip = std::move(unzipRes).unwrap();

            std::filesystem::path entryToExtract;
            for (const auto& entry : unzip.getEntries()) {
                auto entryStr = paimon::paths::toUtf8(entry);
                auto entryLower = geode::utils::string::toLower(entryStr);
                auto needle = geode::utils::string::toLower(archiveEntryName);
                // match the requested filename case-insensitively.
                if (entryLower.size() >= needle.size() &&
                    entryLower.compare(entryLower.size() - needle.size(),
                                       needle.size(), needle) == 0) {
                    // prefer /bin/ entries over documentation copies.
                    if (entryToExtract.empty() ||
                        entryLower.find("/bin/") != std::string::npos) {
                        entryToExtract = paimon::paths::toStdPath(entry);
                    }
                }
            }

            if (entryToExtract.empty()) {
                std::error_code rmEc;
                std::filesystem::remove(zipPath, rmEc);
                fail(fmt::format(
                    "Could not find '{}' inside the ffmpeg archive. "
                    "The mirror may have changed its packaging.",
                    archiveEntryName));
                return;
            }

            auto extractRes = unzip.extractTo(paimon::paths::toSdkPath(entryToExtract), paimon::paths::toSdkPath(destBinary));
            if (!extractRes) {
                std::error_code rmEc;
                std::filesystem::remove(zipPath, rmEc);
                fail(fmt::format("Failed to extract ffmpeg: {}",
                    extractRes.unwrapErr()));
                return;
            }

            std::error_code rmEc;
            std::filesystem::remove(zipPath, rmEc);

            toolfetch::makeExecutable(destBinary);

            if (progressShared && *progressShared) {
                (*progressShared)({"done", 1.f, "ffmpeg installed"});
            }
            if (completeShared && *completeShared) {
                (*completeShared)(true,
                    paimon::paths::toUtf8(destBinary));
            }
        });
}

}
