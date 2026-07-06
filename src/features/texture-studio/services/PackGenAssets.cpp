#include "PackGenAssets.hpp"

#include "PackGenSyncFetch.hpp"
#include "../persist/SlotPaths.hpp"

#include <matjson.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr char const* kDefaultBaseUrl = "https://packgenweb.pages.dev/pack/";
constexpr char const* kManifestFile   = "manifest.json";
constexpr char const* kNoScalingFile  = "noscaling.json";
// Sentinel extension marking a server-confirmed 404 so overlay probing does
// not re-hit the network on every export.
constexpr char const* kMissingSuffix  = ".404";
// The PackGen server gains overlays / mod textures over time; cached
// negatives and the manifest must expire or the mod silently falls behind
// the website forever (unpainted buttons that the web does paint).
constexpr auto kMissingSentinelTtl = std::chrono::hours(24 * 7);
constexpr auto kManifestTtl        = std::chrono::hours(24);

// True when `path` exists and was written within `ttl`. Any filesystem error
// counts as expired, which just costs one extra network probe.
bool fileIsFresh(std::filesystem::path const& path, std::chrono::hours ttl) {
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    auto now = std::filesystem::file_time_type::clock::now();
    return mtime <= now && (now - mtime) < ttl;
}

// Remote manifest paths feed directly into filesystem paths; reject anything
// that could escape the cache directory.
bool isSafeRelativePath(std::string const& rel) {
    if (rel.empty() || rel.front() == '/' || rel.find(':') != std::string::npos) {
        return false;
    }
    return rel.find("..") == std::string::npos;
}

std::string replaceExtensionSuffix(std::string const& pngRelPath,
                                   char const* overlayTag) {
    // "dir/name.png" → "dir/name<overlayTag>.png", mirroring PackGen's JS.
    constexpr std::string_view kPng = ".png";
    if (pngRelPath.size() < kPng.size() ||
        pngRelPath.compare(pngRelPath.size() - kPng.size(), kPng.size(), kPng) != 0) {
        return pngRelPath + overlayTag;
    }
    return pngRelPath.substr(0, pngRelPath.size() - kPng.size())
         + overlayTag + std::string(kPng);
}

// The 8-byte PNG signature. Cloudflare Pages answers missing files with the
// SPA's index.html (HTTP 200), so a content-type check is unreliable; the
// magic bytes are the only trustworthy "is this really a PNG" signal.
bool bytesArePng(std::vector<std::uint8_t> const& b) {
    static constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return b.size() >= 8 && std::equal(std::begin(kSig), std::end(kSig), b.begin());
}

bool fileIsPng(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::uint8_t head[8] = {};
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    if (in.gcount() < static_cast<std::streamsize>(sizeof(head))) return false;
    static constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return std::equal(std::begin(kSig), std::end(kSig), head);
}

geode::Result<std::string> readTextFile(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Err("cannot open {}", geode::utils::string::pathToString(path));
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return Ok(std::move(text));
}

geode::Result<> writeBytesToFile(std::filesystem::path const& path,
                                 std::vector<std::uint8_t> const& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return Err("mkdir failed: {}", ec.message());

    // Write to a temp name then rename, so a crash mid-write never leaves a
    // truncated file that later reads as a valid cached asset.
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Err("cannot open {} for write",
                             geode::utils::string::pathToString(tmp));
        if (!bytes.empty()) {
            out.write(reinterpret_cast<char const*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        if (!out.good()) return Err("write failed");
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) return Err("rename failed: {}", ec.message());
    return Ok();
}

}  // namespace

namespace packgen_suffix {
std::string overlay1(std::string const& p)    { return replaceExtensionSuffix(p, "_OVERLAY1"); }
std::string overlay2(std::string const& p)    { return replaceExtensionSuffix(p, "_OVERLAY2"); }
std::string glow(std::string const& p)        { return replaceExtensionSuffix(p, "_GLOWOVERLAY"); }
std::string gold(std::string const& p)        { return replaceExtensionSuffix(p, "_GOLDOVERLAY"); }
std::string demonFaces1(std::string const& p) { return replaceExtensionSuffix(p, "_OVERLAY1_DEMONFACES"); }
std::string demonFaces2(std::string const& p) { return replaceExtensionSuffix(p, "_OVERLAY2_DEMONFACES"); }
}  // namespace packgen_suffix

bool PackGenManifest::contains(std::string const& relativePath) const {
    return std::find(files.begin(), files.end(), relativePath) != files.end();
}

bool PackGenManifest::isNoScaling(std::string const& relativePath) const {
    return std::find(noScalingFiles.begin(), noScalingFiles.end(), relativePath)
        != noScalingFiles.end();
}

PackGenAssets& PackGenAssets::get() {
    static PackGenAssets instance;
    return instance;
}

PackGenAssets::PackGenAssets() : m_baseUrl(kDefaultBaseUrl) {}

std::filesystem::path PackGenAssets::cacheDir() const {
    return SlotPaths::rootDir() / "packgen-cache";
}

bool PackGenAssets::isManifestLoaded() const {
    std::lock_guard lock(m_mutex);
    return !m_manifest.empty();
}

PackGenManifest PackGenAssets::manifest() const {
    std::lock_guard lock(m_mutex);
    return m_manifest;
}

geode::Result<> PackGenAssets::parseManifestJson(std::string const& jsonText) {
    auto parsed = matjson::parse(jsonText);
    if (!parsed) return Err("manifest.json parse error: {}", parsed.unwrapErr());

    auto filesArr = parsed.unwrap()["files"].asArray();
    if (!filesArr) return Err("manifest.json missing 'files' array");

    std::vector<std::string> files;
    for (auto const& v : filesArr.unwrap()) {
        auto s = v.asString().unwrapOr("");
        if (!s.empty() && isSafeRelativePath(s)) files.push_back(std::move(s));
    }
    if (files.empty()) return Err("manifest.json lists no usable files");

    std::lock_guard lock(m_mutex);
    m_manifest.files = std::move(files);
    return Ok();
}

geode::Result<> PackGenAssets::parseNoScalingJson(std::string const& jsonText) {
    auto parsed = matjson::parse(jsonText);
    if (!parsed) return Err("noscaling.json parse error: {}", parsed.unwrapErr());

    std::vector<std::string> files;
    if (auto arr = parsed.unwrap()["files"].asArray()) {
        for (auto const& v : arr.unwrap()) {
            auto s = v.asString().unwrapOr("");
            if (!s.empty()) files.push_back(std::move(s));
        }
    }
    std::lock_guard lock(m_mutex);
    m_manifest.noScalingFiles = std::move(files);
    return Ok();
}

geode::Result<> PackGenAssets::ensureManifest(bool forceRefresh) {
    auto manifestPath  = cacheDir() / kManifestFile;
    auto noScalingPath = cacheDir() / kNoScalingFile;

    // A fresh-enough cached manifest also refreshes the in-memory copy; an
    // EXPIRED cache falls through to a re-download so newly added server
    // files show up like they do on the website, but is kept as offline
    // fallback if that download fails.
    std::error_code ec;
    bool haveLocal = !forceRefresh
        && std::filesystem::exists(manifestPath, ec) && !ec;
    bool localFresh = haveLocal && fileIsFresh(manifestPath, kManifestTtl);

    if (localFresh || (haveLocal && isManifestLoaded())) {
        if (localFresh) {
            auto text = readTextFile(manifestPath);
            if (text && parseManifestJson(text.unwrap())) {
                if (auto ns = readTextFile(noScalingPath)) {
                    (void)parseNoScalingJson(ns.unwrap());
                }
                return Ok();
            }
            // Corrupt cache: fall through to a fresh download.
        } else {
            return Ok();  // stale but already loaded this session
        }
    }

    auto bytesRes = syncFetchBytes(m_baseUrl + kManifestFile);
    if (!bytesRes) {
        // Offline: fall back to the stale cached copy rather than failing.
        if (haveLocal) {
            auto text = readTextFile(manifestPath);
            if (text && parseManifestJson(text.unwrap())) {
                if (auto ns = readTextFile(noScalingPath)) {
                    (void)parseNoScalingJson(ns.unwrap());
                }
                return Ok();
            }
        }
        return Err(bytesRes.unwrapErr());
    }
    auto bytes = std::move(bytesRes).unwrap();
    std::string text(bytes.begin(), bytes.end());
    GEODE_UNWRAP(parseManifestJson(text));
    (void)writeBytesToFile(manifestPath, bytes);

    // noscaling.json is optional on the server (PackGen treats it as such).
    if (auto nsBytes = syncFetchBytes(m_baseUrl + kNoScalingFile)) {
        auto ns = nsBytes.unwrap();
        std::string nsText(ns.begin(), ns.end());
        if (parseNoScalingJson(nsText)) {
            (void)writeBytesToFile(noScalingPath, ns);
        }
    }
    return Ok();
}

std::filesystem::path PackGenAssets::localPathFor(std::string const& relativePath) const {
    // The manifest paths use '/'; std::filesystem handles them on Windows too.
    return cacheDir() / std::filesystem::path(relativePath);
}

geode::Result<std::filesystem::path> PackGenAssets::ensureFile(
    std::string const& relativePath) {

    if (!isSafeRelativePath(relativePath)) {
        return Err("unsafe path: {}", relativePath);
    }

    bool isPng = relativePath.size() > 4
        && relativePath.compare(relativePath.size() - 4, 4, ".png") == 0;

    auto local = localPathFor(relativePath);
    std::error_code ec;
    if (std::filesystem::exists(local, ec) && !ec &&
        std::filesystem::file_size(local, ec) > 0 && !ec) {
        if (!isPng || fileIsPng(local)) return Ok(local);
        std::filesystem::remove(local, ec);  // stale HTML cache, re-fetch
    }

    GEODE_UNWRAP_INTO(auto bytes, syncFetchBytes(m_baseUrl + relativePath));
    // A .png that comes back as the SPA fallback means the server doesn't
    // actually have it; caching that HTML would later fail to decode.
    if (isPng && !bytesArePng(bytes)) {
        return Err("server returned a non-PNG body for {} (missing asset)", relativePath);
    }
    GEODE_UNWRAP(writeBytesToFile(local, bytes));
    return Ok(local);
}

geode::Result<std::optional<std::filesystem::path>> PackGenAssets::ensureOptionalFile(
    std::string const& relativePath) {

    if (!isSafeRelativePath(relativePath)) {
        return Err("unsafe path: {}", relativePath);
    }

    auto local = localPathFor(relativePath);
    std::error_code ec;
    if (std::filesystem::exists(local, ec) && !ec &&
        std::filesystem::file_size(local, ec) > 0 && !ec) {
        // Self-heal caches written before PNG-signature validation: an entry
        // that is actually the SPA fallback (HTML) is discarded and re-probed.
        if (fileIsPng(local)) return Ok(std::optional(local));
        std::filesystem::remove(local, ec);
    }

    auto sentinel = local;
    sentinel += kMissingSuffix;
    if (std::filesystem::exists(sentinel, ec) && !ec) {
        if (fileIsFresh(sentinel, kMissingSentinelTtl)) {
            return Ok(std::nullopt);
        }
        // Stale negative: the server may have added this overlay since
        // (PackGen's site would already be using it) — re-probe.
        std::filesystem::remove(sentinel, ec);
    }

    // Single GET, then decide by the actual bytes. A non-PNG body means the
    // overlay does not exist on the server (SPA catch-all) — cache a sentinel
    // so we never re-hit the network or try to decode HTML as an image.
    GEODE_UNWRAP_INTO(auto bytes, syncFetchBytes(m_baseUrl + relativePath));
    if (!bytesArePng(bytes)) {
        (void)writeBytesToFile(sentinel, {});
        return Ok(std::nullopt);
    }

    GEODE_UNWRAP(writeBytesToFile(local, bytes));
    return Ok(std::optional(local));
}

PackGenAssets::PrefetchResult PackGenAssets::prefetchAll(PackGenProgressCallback progress) {
    PrefetchResult result;

    if (auto r = ensureManifest(); !r) {
        result.failed = 1;
        result.failedPaths.push_back(std::string(kManifestFile) + ": " + r.unwrapErr());
        return result;
    }

    auto snapshot = manifest();
    int total = static_cast<int>(snapshot.files.size());
    int index = 0;

    for (auto const& rel : snapshot.files) {
        if (progress) progress(index++, total, rel);

        std::error_code ec;
        bool wasCached = std::filesystem::exists(localPathFor(rel), ec) && !ec;

        if (auto r = ensureFile(rel); !r) {
            ++result.failed;
            result.failedPaths.push_back(rel);
            continue;
        }
        wasCached ? ++result.alreadyCached : ++result.downloaded;

        // Probe the overlay variants PackGen probes. Optional-miss (404) is
        // normal; only transport errors count as failures.
        bool isPng = rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".png") == 0;
        if (!isPng) continue;

        std::vector<std::string> variants = {
            packgen_suffix::overlay1(rel),
            packgen_suffix::overlay2(rel),
            packgen_suffix::glow(rel),
        };
        if (rel == "GJ_GameSheet03-uhd.png" || rel == "GJ_GameSheet04-uhd.png" ||
            rel == "TreasureRoomSheet-uhd.png" ||
            rel.find("newBestFont") != std::string::npos) {
            variants.push_back(packgen_suffix::gold(rel));
        }
        if (rel == "GJ_GameSheet03-uhd.png") {
            variants.push_back(packgen_suffix::demonFaces1(rel));
            variants.push_back(packgen_suffix::demonFaces2(rel));
        }

        for (auto const& v : variants) {
            if (auto r = ensureOptionalFile(v); !r) {
                ++result.failed;
                result.failedPaths.push_back(v);
            }
        }
    }

    if (progress) progress(total, total, std::string());
    return result;
}

}  // namespace paimon::texture_studio
