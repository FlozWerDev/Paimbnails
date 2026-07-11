#include "FfmpegBootstrap.hpp"

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

FfmpegBootstrap& FfmpegBootstrap::get() {
    static FfmpegBootstrap instance;
    return instance;
}

// Rutas y URLs

std::filesystem::path FfmpegBootstrap::bundledPath() const {
#ifdef GEODE_IS_WINDOWS
    constexpr const char* kName = "ffmpeg.exe";
#else
    constexpr const char* kName = "ffmpeg";
#endif
    return Mod::get()->getSaveDir() / "ffmpeg" / kName;
}

bool FfmpegBootstrap::exists() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(bundledPath(), ec);
}

void FfmpegBootstrap::uninstall() {
    std::error_code ec;
    std::filesystem::remove_all(Mod::get()->getSaveDir() / "ffmpeg", ec);
}

std::string FfmpegBootstrap::releaseUrl() {
#ifdef GEODE_IS_WINDOWS
    // BtbN builds — distribuyen ffmpeg.exe como single-file dentro de un zip.
    // Usamos la build "essentials" (sin libx264, mas pequena, ~40MB).
    // La URL "latest" apunta siempre a la ultima publicada.
    return "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/"
           "ffmpeg-master-latest-win64-gpl.zip";
#elif defined(GEODE_IS_MACOS)
    // Evermeet publica el binario suelto (sin zip) — mas facil.
    // En caso de que cambie, apuntamos al getrelease que redirige.
    return "https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip";
#elif defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    // Mobile: no soportamos descarga autom. ffmpeg no se distribuye
    // como binario estatico para Android/iOS de forma estandar.
    return "";
#else
    // Linux: BtbN tambien tiene builds estaticas. Vienen en tar.xz pero
    // hay un zip alternativo.
    return "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/"
           "ffmpeg-master-latest-linux64-gpl.zip";
#endif
}

bool FfmpegBootstrap::isArchive() {
    // Todas las URLs anteriores son .zip.
    return true;
}

std::string FfmpegBootstrap::archiveEntry() {
#ifdef GEODE_IS_WINDOWS
    // BtbN: ffmpeg-master-latest-win64-gpl/bin/ffmpeg.exe
    return "ffmpeg.exe";
#elif defined(GEODE_IS_MACOS)
    return "ffmpeg";
#else
    return "ffmpeg";
#endif
}

// Ensure installed

void FfmpegBootstrap::ensureInstalled(
    FfmpegBootstrapProgressCallback onProgress,
    FfmpegBootstrapCompleteCallback onComplete
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
                    "ffmpeg auto-install is not supported on this platform. "
                    "Install ffmpeg manually and make sure it is in your PATH.");
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

    auto destBinary = bundledPath();
    auto destDir = destBinary.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    if (onProgress) {
        Loader::get()->queueInMainThread([onProgress]() {
            if (paimon::isRuntimeShuttingDown()) return;
            onProgress({"resolving", 0.f, "Contacting ffmpeg mirror..."});
        });
    }

    auto progressShared =
        std::make_shared<FfmpegBootstrapProgressCallback>(std::move(onProgress));
    auto completeShared =
        std::make_shared<FfmpegBootstrapCompleteCallback>(std::move(onComplete));

    auto req = web::WebRequest()
        .timeout(std::chrono::minutes(10))
        .userAgent("Paimbnails-MenuMusic/1.0 (ffmpeg-bootstrap)");

    req.onProgress([progressShared](web::WebProgress const& p) {
        if (!progressShared || !*progressShared) return;
        auto downloaded = static_cast<uint64_t>(p.downloaded());
        auto total = static_cast<uint64_t>(p.downloadTotal());
        Loader::get()->queueInMainThread([progressShared, downloaded, total]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!progressShared || !*progressShared) return;
            float pct = (total > 0)
                ? std::min(0.90f, static_cast<float>(downloaded) /
                           static_cast<float>(total) * 0.90f)
                : 0.f;
            std::string msg = (total > 0)
                ? fmt::format("Downloading ffmpeg... {:.2f} / {:.2f} MB",
                    downloaded / 1'048'576.0, total / 1'048'576.0)
                : fmt::format("Downloading ffmpeg... {:.2f} MB",
                    downloaded / 1'048'576.0);
            (*progressShared)({"downloading", pct, std::move(msg)});
        });
    });

    auto archiveEntryName = archiveEntry();
    WebHelper::dispatch(std::move(req), "GET", url,
        [this, destDir, destBinary, archiveEntryName,
         progressShared, completeShared](web::WebResponse res) {
            m_downloading = false;

            auto fail = [completeShared](std::string err) {
                if (completeShared && *completeShared)
                    (*completeShared)(false, std::move(err));
            };

            if (!res.ok()) {
                int code = res.code();
                fail(fmt::format(
                    "Failed to download ffmpeg (HTTP {}). Try again later or install "
                    "ffmpeg manually.", code));
                return;
            }

            auto data = res.data();
            if (data.size() < 1'000'000) {
                fail(fmt::format(
                    "Downloaded ffmpeg archive is too small ({} bytes) — the "
                    "mirror may be down.", data.size()));
                return;
            }

            if (progressShared && *progressShared) {
                (*progressShared)({"extracting", 0.92f,
                    fmt::format("Extracting archive ({:.1f} MB)...",
                        data.size() / 1'048'576.0)});
            }

            // Guardar el zip a disco para poder extraerlo. Geode's Unzip
            // tambien acepta ByteSpan pero el disco es mas simple.
            auto zipPath = destDir / "ffmpeg-download.zip";
            auto writeRes = geode::utils::file::writeBinary(zipPath, data);
            if (!writeRes) {
                fail(fmt::format("Failed to write ffmpeg archive: {}",
                    writeRes.unwrapErr()));
                return;
            }

            // Abrir el zip y buscar la entrada que termina en
            // "/bin/ffmpeg.exe" o "/ffmpeg.exe" (la estructura varia
            // segun el mirror). Extraerla a destBinary.
            auto unzipRes = geode::utils::file::Unzip::create(zipPath);
            if (!unzipRes) {
                fail(fmt::format("Failed to open ffmpeg zip: {}",
                    unzipRes.unwrapErr()));
                return;
            }
            auto unzip = std::move(unzipRes).unwrap();

            // Encontrar la entrada que coincida con el binario buscado.
            // BtbN empaqueta dentro de "ffmpeg-master-latest-win64-gpl/bin/ffmpeg.exe".
            // Evermeet empaqueta directamente "ffmpeg".
            std::filesystem::path entryToExtract;
            for (const auto& entry : unzip.getEntries()) {
                auto entryStr = geode::utils::string::pathToString(entry);
                auto entryLower = geode::utils::string::toLower(entryStr);
                auto needle = geode::utils::string::toLower(archiveEntryName);
                // Matcheamos si la entrada termina exactamente con el
                // nombre buscado (insensible a mayusculas).
                if (entryLower.size() >= needle.size() &&
                    entryLower.compare(entryLower.size() - needle.size(),
                                       needle.size(), needle) == 0) {
                    // Preferimos las rutas con "/bin/" sobre las que no lo
                    // tienen (algunos zips traen tambien ffmpeg en docs/).
                    if (entryToExtract.empty() ||
                        entryLower.find("/bin/") != std::string::npos) {
                        entryToExtract = entry;
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

            auto extractRes = unzip.extractTo(entryToExtract, destBinary);
            if (!extractRes) {
                std::error_code rmEc;
                std::filesystem::remove(zipPath, rmEc);
                fail(fmt::format("Failed to extract ffmpeg: {}",
                    extractRes.unwrapErr()));
                return;
            }

            // Borrar el zip temporal (ya no lo necesitamos).
            std::error_code rmEc;
            std::filesystem::remove(zipPath, rmEc);

#ifndef GEODE_IS_WINDOWS
            std::error_code permEc;
            std::filesystem::permissions(destBinary,
                std::filesystem::perms::owner_exec |
                std::filesystem::perms::group_exec |
                std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add, permEc);
#endif

            if (progressShared && *progressShared) {
                (*progressShared)({"done", 1.f, "ffmpeg installed"});
            }
            if (completeShared && *completeShared) {
                (*completeShared)(true,
                    geode::utils::string::pathToString(destBinary));
            }
        }
    );
}

} // namespace paimon::menumusic
