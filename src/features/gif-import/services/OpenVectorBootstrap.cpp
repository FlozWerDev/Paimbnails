#include "../../../utils/FilesystemPath.hpp"
#include "OpenVectorBootstrap.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../utils/stb_image.h"
#include "../../menu-music/services/ToolBinaryFetch.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <memory>

using namespace geode::prelude;

namespace paimon::gifimport {

namespace {

constexpr std::size_t kMaxTarUncompressedBytes = 128ull * 1024 * 1024;
constexpr std::size_t kMaxSingleBinaryBytes = 32ull * 1024 * 1024;

bool locateInPath(char const* exeName, std::filesystem::path& outPath) {
    char const* envPath = std::getenv("PATH");
    if (!envPath || !*envPath) return false;
#ifdef GEODE_IS_WINDOWS
    constexpr char kSep = ';';
#else
    constexpr char kSep = ':';
#endif
    std::string_view sv(envPath);
    std::size_t pos = 0;
    std::error_code ec;
    while (pos <= sv.size()) {
        std::size_t next = sv.find(kSep, pos);
        if (next == std::string_view::npos) next = sv.size();
        std::string_view dir = sv.substr(pos, next - pos);
        if (!dir.empty()) {
            auto candidate = std::filesystem::path(std::string(dir)) / exeName;
            if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
                outPath = std::move(candidate);
                return true;
            }
        }
        if (next == sv.size()) break;
        pos = next + 1;
    }
    return false;
}

bool skipGzipHeader(std::uint8_t const* data, std::size_t size, std::size_t& offsetOut) {
    if (!data || size < 18) return false;
    if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 0x08) return false;
    std::uint8_t const flg = data[3];
    std::size_t pos = 10;
    if (flg & 0x04) { // FEXTRA
        if (pos + 2 > size) return false;
        std::uint16_t xlen = static_cast<std::uint16_t>(data[pos] | (data[pos + 1] << 8));
        pos += 2 + xlen;
        if (pos > size) return false;
    }
    if (flg & 0x08) { // FNAME
        while (pos < size && data[pos] != 0) ++pos;
        if (pos >= size) return false;
        ++pos;
    }
    if (flg & 0x10) { // FCOMMENT
        while (pos < size && data[pos] != 0) ++pos;
        if (pos >= size) return false;
        ++pos;
    }
    if (flg & 0x02) { // FHCRC
        pos += 2;
        if (pos > size) return false;
    }
    if (pos + 8 > size) return false;
    offsetOut = pos;
    return true;
}

std::uint64_t parseTarOctal(char const* field, std::size_t len) {
    std::uint64_t val = 0;
    std::size_t i = 0;
    while (i < len && (field[i] == ' ' || field[i] == '\0')) ++i;
    while (i < len && field[i] >= '0' && field[i] <= '7') {
        val = (val << 3) + static_cast<std::uint64_t>(field[i] - '0');
        ++i;
    }
    return val;
}

std::string tarCString(char const* field, std::size_t maxLen) {
    std::size_t len = 0;
    while (len < maxLen && field[len] != '\0') ++len;
    return std::string(field, len);
}

} // namespace

OpenVectorBootstrap& OpenVectorBootstrap::get() {
    static OpenVectorBootstrap instance;
    return instance;
}

std::filesystem::path OpenVectorBootstrap::bundledPath() const {
#ifdef GEODE_IS_WINDOWS
    constexpr char const* kName = "openvector.exe";
#else
    constexpr char const* kName = "openvector";
#endif
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "openvector" / kName;
}

std::filesystem::path OpenVectorBootstrap::locateBinary() const {
    std::error_code ec;
    auto bundled = bundledPath();
    if (std::filesystem::is_regular_file(bundled, ec) && !ec) return bundled;
#ifdef GEODE_IS_WINDOWS
    constexpr char const* kName = "openvector.exe";
#else
    constexpr char const* kName = "openvector";
#endif
    std::filesystem::path found;
    if (locateInPath(kName, found)) return found;
    return {};
}

bool OpenVectorBootstrap::exists() const {
    return !locateBinary().empty();
}

void OpenVectorBootstrap::uninstall() {
    std::error_code ec;
    std::filesystem::remove_all(paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "openvector", ec);
}

std::string OpenVectorBootstrap::releaseUrl() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return "";
#else
    return "https://registry.npmjs.org/openvector/-/openvector-0.1.6.tgz";
#endif
}

std::string OpenVectorBootstrap::archiveEntry() {
#ifdef GEODE_IS_WINDOWS
    return "package/native/win32-x64/openvector.exe";
#elif defined(GEODE_IS_MACOS)
#if defined(__aarch64__) || defined(_M_ARM64)
    return "package/native/darwin-arm64/openvector";
#else
    return "package/native/darwin-x64/openvector";
#endif
#elif defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return "";
#else
#if defined(__aarch64__)
    return "package/native/linux-arm64/openvector";
#else
    return "package/native/linux-x64/openvector";
#endif
#endif
}

bool OpenVectorBootstrap::extractEntryFromTgz(
    std::uint8_t const* tgzData,
    std::size_t tgzSize,
    std::string const& targetEntry,
    std::vector<std::uint8_t>& outBytes,
    std::string& errorOut
) {
    outBytes.clear();
    if (!tgzData || tgzSize < 32 || targetEntry.empty()) {
        errorOut = "Archivo OpenVector invalido.";
        return false;
    }
    std::size_t deflateOffset = 0;
    if (!skipGzipHeader(tgzData, tgzSize, deflateOffset)) {
        errorOut = "Cabecera gzip invalida en el paquete de OpenVector.";
        return false;
    }
    // Last 4 bytes of gzip store uncompressed size mod 2^32 (used as initial hint).
    std::uint32_t const isizeHint =
        static_cast<std::uint32_t>(tgzData[tgzSize - 4]) |
        (static_cast<std::uint32_t>(tgzData[tgzSize - 3]) << 8) |
        (static_cast<std::uint32_t>(tgzData[tgzSize - 2]) << 16) |
        (static_cast<std::uint32_t>(tgzData[tgzSize - 1]) << 24);
    int const initialSize = static_cast<int>(std::clamp<std::uint64_t>(
        isizeHint > 0 ? isizeHint : 64ull * 1024 * 1024,
        1024ull * 1024, kMaxTarUncompressedBytes));

    int const deflateLen = static_cast<int>(tgzSize - deflateOffset - 8);
    int tarLen = 0;
    char* tarRaw = stbi_zlib_decode_malloc_guesssize_headerflag(
        reinterpret_cast<char const*>(tgzData + deflateOffset),
        deflateLen, initialSize, &tarLen, 0);
    if (!tarRaw || tarLen <= 0 || static_cast<std::size_t>(tarLen) > kMaxTarUncompressedBytes) {
        if (tarRaw) std::free(tarRaw);
        errorOut = "No se pudo descomprimir el paquete de OpenVector.";
        return false;
    }

    // Walk POSIX ustar 512-byte headers until we find the exact targetEntry.
    std::size_t const total = static_cast<std::size_t>(tarLen);
    std::size_t cursor = 0;
    bool found = false;
    while (cursor + 512 <= total) {
        char const* hdr = tarRaw + cursor;
        bool allZero = true;
        for (int i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;

        std::string name = tarCString(hdr, 100);
        std::string prefix = tarCString(hdr + 345, 155);
        std::string fullName = prefix.empty() ? name : (prefix + "/" + name);
        std::uint64_t const entrySize = parseTarOctal(hdr + 124, 12);
        char const typeFlag = hdr[156];
        cursor += 512;

        if (entrySize > total - cursor) break;
        bool const isRegular = (typeFlag == '\0' || typeFlag == '0');
        if (isRegular && fullName == targetEntry) {
            if (entrySize < 256 * 1024 || entrySize > kMaxSingleBinaryBytes) {
                std::free(tarRaw);
                errorOut = fmt::format("Tamano inesperado del binario OpenVector ({} bytes).", entrySize);
                return false;
            }
            outBytes.assign(
                reinterpret_cast<std::uint8_t const*>(tarRaw + cursor),
                reinterpret_cast<std::uint8_t const*>(tarRaw + cursor + entrySize));
            found = true;
            break;
        }
        std::size_t const padded = static_cast<std::size_t>((entrySize + 511ull) & ~511ull);
        if (padded > total - cursor) break;
        cursor += padded;
    }

    std::free(tarRaw);
    if (!found) {
        errorOut = fmt::format("No se encontro '{}' dentro del paquete OpenVector.", targetEntry);
        return false;
    }
    return true;
}

void OpenVectorBootstrap::ensureInstalled(
    OpenVectorBootstrapProgressCb onProgress,
    OpenVectorBootstrapCompleteCb onComplete
) {
    auto existing = locateBinary();
    if (!existing.empty()) {
        if (onComplete) {
            auto pathStr = paimon::paths::toUtf8(existing);
            Loader::get()->queueInMainThread([onComplete, pathStr]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (onComplete) onComplete(true, pathStr);
            });
        }
        return;
    }

    auto destBinary = bundledPath();
    auto destDir = destBinary.parent_path();
    auto targetEntry = archiveEntry();

    paimon::menumusic::toolfetch::FetchConfig config;
    config.url = releaseUrl();
    config.unsupportedMessage =
        "El motor complementario OpenVector no esta disponible en dispositivos moviles. "
        "Puedes importar archivos .svg directamente o usar los modos de imagen nativos.";
    config.userAgent = "Paimbnails-OpenVector/1.0";
    config.timeout = std::chrono::minutes(5);
    config.resolvingMessage = "Conectando con OpenVector...";
    config.downloadingVerb = "Descargando OpenVector... ";
    config.downloadCap = 0.88f;
    config.minBytes = 5'000'000;
    config.httpErrorFor = [](int code) {
        return fmt::format(
            "No se pudo descargar OpenVector ({} {}).\n"
            "Intentalo de nuevo mas tarde o instala OpenVector manualmente.",
            code < 0 ? "error de red" : "HTTP", code);
    };
    config.sizeErrorFor = [](std::size_t bytes) {
        return fmt::format(
            "El paquete descargado de OpenVector es demasiado pequeno ({} bytes).", bytes);
    };

    paimon::menumusic::toolfetch::fetchToolBinary<OpenVectorBootstrapProgress>(
        std::move(config), destBinary, false, destDir,
        m_downloading, std::move(onProgress), std::move(onComplete),
        [destDir, destBinary, targetEntry](
            geode::ByteVector data,
            std::shared_ptr<OpenVectorBootstrapProgressCb> progressShared,
            std::shared_ptr<OpenVectorBootstrapCompleteCb> completeShared
        ) {
            if (progressShared && *progressShared) {
                (*progressShared)({"extracting", 0.90f, "Extrayendo binario OpenVector..."});
            }
            auto payload = std::make_shared<geode::ByteVector>(std::move(data));
            bool const spawned = paimon::ThreadTracker::get().spawn(
                [payload, destDir, destBinary, targetEntry, progressShared, completeShared] {
                    geode::utils::thread::setName("Paimon OpenVector Extract");
                    std::vector<std::uint8_t> binary;
                    std::string err;
                    bool ok = extractEntryFromTgz(
                        payload->data(), payload->size(), targetEntry, binary, err);
                    if (ok) {
                        std::error_code ec;
                        std::filesystem::create_directories(destDir, ec);
                        auto tmp = destBinary;
                        tmp += ".tmp";
                        auto writeRes = geode::utils::file::writeBinary(
                            paimon::paths::toSdkPath(tmp), binary);
                        if (!writeRes) {
                            ok = false;
                            err = fmt::format("No se pudo guardar OpenVector: {}", writeRes.unwrapErr());
                        } else {
                            paimon::menumusic::toolfetch::makeExecutable(tmp);
                            std::filesystem::rename(tmp, destBinary, ec);
                            if (ec) {
                                std::filesystem::remove(destBinary, ec);
                                ec.clear();
                                std::filesystem::rename(tmp, destBinary, ec);
                            }
                            if (ec) {
                                std::filesystem::remove(tmp, ec);
                                ok = false;
                                err = fmt::format("No se pudo instalar OpenVector: {}", ec.message());
                            }
                        }
                    }

                    auto resultPathOrErr = ok ? paimon::paths::toUtf8(destBinary) : std::move(err);
                    Loader::get()->queueInMainThread(
                        [ok, resultPathOrErr = std::move(resultPathOrErr), progressShared, completeShared]() {
                            if (paimon::isRuntimeShuttingDown()) return;
                            if (ok && progressShared && *progressShared) {
                                (*progressShared)({"done", 1.f, "OpenVector instalado"});
                            }
                            if (completeShared && *completeShared) {
                                (*completeShared)(ok, resultPathOrErr);
                            }
                        });
                });
            if (!spawned && completeShared && *completeShared) {
                (*completeShared)(false, "El juego se esta cerrando.");
            }
        });
}

} // namespace paimon::gifimport
