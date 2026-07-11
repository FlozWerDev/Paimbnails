#include "LocalAssetStore.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::assets {
namespace {

bool kindAllowsExtension(Kind kind, std::string ext) {
    ext = geode::utils::string::toLower(ext);

    static std::vector<std::string> const kImageExts = {
        ".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tiff", ".tif",
        ".tga", ".psd", ".qoi", ".jxl", ".apng"
    };
    static std::vector<std::string> const kVideoExts = {
        ".mp4", ".mov", ".m4v"
    };

    auto in = [&](std::vector<std::string> const& list) {
        return std::find(list.begin(), list.end(), ext) != list.end();
    };

    switch (kind) {
        case Kind::Image: return in(kImageExts);
        case Kind::Video: return in(kVideoExts);
        case Kind::Media: return in(kImageExts) || in(kVideoExts);
    }
    return false;
}

std::string sanitizeBucket(std::string bucket) {
    if (bucket.empty()) return "misc";
    std::replace_if(bucket.begin(), bucket.end(), [](unsigned char c) {
        return !(std::isalnum(c) || c == '-' || c == '_');
    }, '_');
    return bucket;
}

std::string sanitizeFilenameStem(std::string stem) {
    if (stem.empty()) return "asset";
    std::replace_if(stem.begin(), stem.end(), [](unsigned char c) {
        return !(std::isalnum(c) || c == '-' || c == '_' || c == '.');
    }, '_');
    return stem;
}

std::string makeUniqueFilename(std::filesystem::path const& source, std::string const& bucket) {
    auto stem = sanitizeFilenameStem(geode::utils::string::pathToString(source.stem()));
    auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(source.extension()));

    uintmax_t size = 0;
    std::error_code sizeEc;
    size = std::filesystem::file_size(source, sizeEc);

    auto stamp = static_cast<unsigned long long>(std::hash<std::string>{}(
        fmt::format("{}|{}|{}|{}",
            bucket,
            geode::utils::string::pathToString(source),
            sizeEc ? 0ull : static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(std::time(nullptr))
        )
    ));

    return fmt::format("{}_{}{}", stem, stamp, ext);
}

} // namespace

std::filesystem::path rootDir() {
    auto dir = Mod::get()->getSaveDir() / "local_assets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path dirForBucket(std::string const& bucket) {
    auto dir = rootDir() / sanitizeBucket(bucket);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool isManagedPath(std::filesystem::path const& path) {
    auto normalized = normalizePath(path);
    auto root = normalizePath(rootDir());
    auto normStr = geode::utils::string::toLower(geode::utils::string::pathToString(normalized));
    auto rootStr = geode::utils::string::toLower(geode::utils::string::pathToString(root));
    return !normStr.empty() && !rootStr.empty() && normStr.rfind(rootStr, 0) == 0;
}

std::filesystem::path normalizePath(std::filesystem::path const& path) {
    if (path.empty()) return {};
    std::error_code ec;
    auto abs = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
    if (ec) abs = path;
    return abs.lexically_normal();
}

std::string normalizePathString(std::filesystem::path const& path) {
    return geode::utils::string::replace(
        geode::utils::string::pathToString(normalizePath(path)), "\\", "/");
}

bool exists(std::string const& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(normalizePath(path), ec) && !ec;
}

ImportResult importToBucket(std::filesystem::path const& source, std::string const& bucket, Kind kind) {
    ImportResult result;

    if (source.empty()) {
        result.error = "empty_path";
        return result;
    }

    auto normalizedSource = normalizePath(source);
    std::error_code ec;
    if (!std::filesystem::exists(normalizedSource, ec) || ec) {
        result.error = "file_not_found";
        return result;
    }

    auto ext = geode::utils::string::pathToString(normalizedSource.extension());
    if (!kindAllowsExtension(kind, ext)) {
        result.error = "unsupported_extension";
        return result;
    }

    if (isManagedPath(normalizedSource)) {
        result.success = true;
        result.path = normalizedSource;
        return result;
    }

    auto targetDir = dirForBucket(bucket);
    auto filename = makeUniqueFilename(normalizedSource, bucket);
    auto target = targetDir / filename;

    std::filesystem::copy_file(normalizedSource, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        result.error = ec.message();
        return result;
    }

    result.success = true;
    result.changed = true;
    result.path = normalizePath(target);
    return result;
}

ImportResult importStoredPath(std::string const& storedPath, std::string const& bucket, Kind kind) {
    if (storedPath.empty()) {
        ImportResult result;
        result.success = true;
        return result;
    }
    return importToBucket(std::filesystem::path(storedPath), bucket, kind);
}

std::string importToBucketString(std::filesystem::path const& source, std::string const& bucket, Kind kind) {
    auto result = importToBucket(source, bucket, kind);
    if (!result.success || result.path.empty()) return "";
    return normalizePathString(result.path);
}

std::string importStoredPathString(std::string const& storedPath, std::string const& bucket, Kind kind) {
    auto result = importStoredPath(storedPath, bucket, kind);
    if (!result.success || result.path.empty()) return "";
    return normalizePathString(result.path);
}

bool cleanupBucket(std::string const& bucket) {
    std::error_code ec;
    std::filesystem::remove_all(dirForBucket(bucket), ec);
    return !ec;
}

} // namespace paimon::assets
