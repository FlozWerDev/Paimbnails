#pragma once
// Asset pack hosted at https://packgenweb.pages.dev/pack/ (made by Asterveila for PackGen).
// Base sheets plus hand-drawn overlay masks (_OVERLAY1/_OVERLAY2/_GLOWOVERLAY/
// _GOLDOVERLAY/_OVERLAY1_DEMONFACES/...) that give pixel-exact region tinting.

#include <Geode/Geode.hpp>

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace paimon::texture_studio {

struct PackGenManifest {
    // Relative paths as listed by pack/manifest.json (png, plist, fnt).
    std::vector<std::string> files;
    // Files that must NOT be downscaled when building the medium port.
    std::vector<std::string> noScalingFiles;

    bool empty() const { return files.empty(); }
    bool contains(std::string const& relativePath) const;
    bool isNoScaling(std::string const& relativePath) const;
};

// Progress callback signature: (currentIndex, totalFiles, currentFileLabel).
using PackGenProgressCallback = std::function<void(int, int, std::string const&)>;

// All methods are synchronous and safe to call from any single background
// thread; internal state is mutex-guarded so prefetch and export can overlap.
class PackGenAssets final {
public:
    static PackGenAssets& get();

    std::string baseUrl() const { return m_baseUrl; }

    std::filesystem::path cacheDir() const;

    bool isManifestLoaded() const;

    // Valid only after ensureManifest succeeded (copy under lock).
    PackGenManifest manifest() const;

    geode::Result<> ensureManifest(bool forceRefresh = false);

    // Downloads (or reuses the cached copy of) a file that must exist.
    geode::Result<std::filesystem::path> ensureFile(std::string const& relativePath);

    // For overlay probing: Ok(nullopt) means the server confirmed the file
    // does not exist (negative result is cached on disk); Err means we could
    // not find out (network trouble) and the caller should fail loudly
    // rather than silently produce an untinted pack.
    geode::Result<std::optional<std::filesystem::path>> ensureOptionalFile(
        std::string const& relativePath);

    std::filesystem::path localPathFor(std::string const& relativePath) const;

    struct PrefetchResult {
        int alreadyCached = 0;
        int downloaded    = 0;
        int failed        = 0;
        std::vector<std::string> failedPaths;
    };
    // Downloads every manifest file plus its overlay variants. Heavy on the
    // first run (~200 files); afterwards everything is served from cache.
    PrefetchResult prefetchAll(PackGenProgressCallback progress);

private:
    PackGenAssets();

    geode::Result<> parseManifestJson(std::string const& jsonText);
    geode::Result<> parseNoScalingJson(std::string const& jsonText);

    std::string                m_baseUrl;
    PackGenManifest            m_manifest;
    mutable std::mutex         m_mutex;
};

// The overlay-file suffix convention used by the PackGen asset pack.
namespace packgen_suffix {
std::string overlay1(std::string const& pngRelPath);
std::string overlay2(std::string const& pngRelPath);
std::string glow(std::string const& pngRelPath);
std::string gold(std::string const& pngRelPath);
std::string demonFaces1(std::string const& pngRelPath);
std::string demonFaces2(std::string const& pngRelPath);
}  // namespace packgen_suffix

}  // namespace paimon::texture_studio
