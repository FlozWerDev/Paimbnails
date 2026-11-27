#include "../../../utils/FilesystemPath.hpp"
#include "ReferenceStore.hpp"

#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/ClipboardImage.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/utils/file.hpp>
#include <fmt/format.h>
#include <matjson.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::editorref::store {

namespace {

constexpr int kVersion = 2;
constexpr size_t kMaxImageMB = 20;

std::filesystem::path rootDir() {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "reference-images";
}

std::filesystem::path docPathFor(std::string const& key) {
    return rootDir() / (key + ".json");
}

bool validIdentity(std::string const& s) {
    if (s.size() < 8 || s.size() > 64) return false;
    for (unsigned char c : s) {
        if (!std::isalnum(c) && c != '-' && c != '_') return false;
    }
    return true;
}

std::string randomHexId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    return fmt::format("{:016x}{:08x}", dist(rng), static_cast<uint32_t>(dist(rng)));
}

std::unordered_map<std::string, WeakRef<GJGameLevel>>& identityOwners() {
    static std::unordered_map<std::string, WeakRef<GJGameLevel>> owners;
    return owners;
}

int intOr(matjson::Value const& obj, char const* key, int fallback) {
    return static_cast<int>(obj[key].asInt().unwrapOr(fallback));
}

float floatOr(matjson::Value const& obj, char const* key, float fallback) {
    auto v = static_cast<float>(obj[key].asDouble().unwrapOr(fallback));
    return std::isfinite(v) ? v : fallback;
}

bool boolOr(matjson::Value const& obj, char const* key, bool fallback) {
    return obj[key].asBool().unwrapOr(fallback);
}

std::string sanitizeExt(std::filesystem::path const& path) {
    auto ext = paimon::paths::toUtf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".bmp") {
        return ext;
    }
    return ".png";
}

bool isManagedAssetName(std::string const& assetFile) {
    if (assetFile.empty()) return false;
    if (assetFile.find('/') != std::string::npos || assetFile.find('\\') != std::string::npos) {
        return false;
    }
    if (assetFile == "." || assetFile == "..") return false;
    return true;
}

matjson::Value encodeEntry(ReferenceEntry const& e) {
    auto obj = matjson::Value::object();
    obj["id"] = e.id;
    obj["name"] = e.name;
    obj["assetFile"] = e.assetFile;
    obj["x"] = e.x;
    obj["y"] = e.y;
    obj["scaleX"] = e.scaleX;
    obj["scaleY"] = e.scaleY;
    obj["rotationX"] = e.rotationX;
    obj["rotationY"] = e.rotationY;
    obj["skewX"] = e.skewX;
    obj["skewY"] = e.skewY;
    obj["flipX"] = e.flipX;
    obj["flipY"] = e.flipY;
    obj["opacity"] = std::clamp(e.opacity, 0, 255);
    obj["zPlacement"] = static_cast<int>(e.zPlacement);
    obj["locked"] = e.locked;
    obj["visible"] = e.visible;
    obj["showInPlaytest"] = e.showInPlaytest;
    return obj;
}

ReferenceEntry decodeEntry(matjson::Value const& obj, int docVersion) {
    ReferenceEntry e;
    e.id = intOr(obj, "id", 0);
    e.name = obj["name"].asString().unwrapOr("Image");
    e.assetFile = obj["assetFile"].asString().unwrapOr("");
    e.x = floatOr(obj, "x", 0.f);
    e.y = floatOr(obj, "y", 0.f);
    e.scaleX = floatOr(obj, "scaleX", 1.f);
    e.scaleY = floatOr(obj, "scaleY", 1.f);
    // Migrate v1 entries (where scale=1 meant 30u) to v2 (where scale=1 means 180u).
    if (docVersion < 2) {
        e.scaleX /= 6.f;
        e.scaleY /= 6.f;
    }
    if (std::abs(e.scaleX) < 0.05f) e.scaleX = 1.f;
    if (std::abs(e.scaleY) < 0.05f) e.scaleY = 1.f;
    e.rotationX = floatOr(obj, "rotationX", 0.f);
    e.rotationY = floatOr(obj, "rotationY", 0.f);
    e.skewX = floatOr(obj, "skewX", 0.f);
    e.skewY = floatOr(obj, "skewY", 0.f);
    e.flipX = boolOr(obj, "flipX", false);
    e.flipY = boolOr(obj, "flipY", false);
    e.opacity = std::clamp(intOr(obj, "opacity", 145), 0, 255);
    int z = intOr(obj, "zPlacement", 0);
    e.zPlacement = (z == static_cast<int>(ZPlacement::Front)) ? ZPlacement::Front : ZPlacement::Behind;
    e.locked = boolOr(obj, "locked", false);
    e.visible = boolOr(obj, "visible", true);
    e.showInPlaytest = boolOr(obj, "showInPlaytest", false);
    return e;
}

matjson::Value encodeDocument(ReferenceDocument const& doc) {
    auto json = matjson::Value::object();
    json["version"] = kVersion;
    json["nextId"] = doc.nextId;
    std::vector<matjson::Value> items;
    items.reserve(doc.entries.size());
    for (auto const& e : doc.entries) items.push_back(encodeEntry(e));
    json["entries"] = matjson::Value(items);
    return json;
}

ReferenceDocument decodeDocument(matjson::Value const& json) {
    ReferenceDocument doc;
    int const docVersion = intOr(json, "version", 1);
    doc.nextId = std::max(1, intOr(json, "nextId", 1));
    if (auto arr = json["entries"].asArray()) {
        for (auto const& entryVal : arr.unwrap()) {
            if (doc.entries.size() >= static_cast<std::size_t>(kMaxReferencesPerLevel)) break;
            auto entry = decodeEntry(entryVal, docVersion);
            if (entry.assetFile.empty()) continue;
            if (entry.id <= 0) entry.id = doc.nextId;
            doc.nextId = std::max(doc.nextId, entry.id + 1);
            doc.entries.push_back(std::move(entry));
        }
    }
    return doc;
}

} // namespace

std::filesystem::path assetsDir() {
    return rootDir() / "assets";
}

std::filesystem::path resolveAssetPath(std::string const& assetFile) {
    if (assetFile.empty()) return {};
    if (isManagedAssetName(assetFile)) {
        return assetsDir() / paimon::assets::pathFromUtf8(assetFile);
    }
    return paimon::assets::pathFromUtf8(assetFile);
}

std::string cloneAssetFile(std::string const& assetFile) {
    auto const src = resolveAssetPath(assetFile);
    std::error_code ec;
    if (src.empty() || !std::filesystem::exists(src, ec)) return assetFile;
    std::filesystem::create_directories(assetsDir(), ec);
    auto const newName = fmt::format("ref_{}{}", randomHexId(), sanitizeExt(src));
    auto const dst = assetsDir() / paimon::assets::pathFromUtf8(newName);
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return assetFile;
    return newName;
}

void deleteAssetIfUnused(std::string const& assetFile, ReferenceDocument const& doc) {
    if (!isManagedAssetName(assetFile)) return;
    for (auto const& e : doc.entries) {
        if (e.assetFile == assetFile) return;
    }
    std::error_code ec;
    std::filesystem::remove(resolveAssetPath(assetFile), ec);
}

void restoreLevelIdentity(GJGameLevel* level, std::string const& identity) {
    if (!level) return;
    std::string chosen = identity;
    auto& owners = identityOwners();
    auto existing = owners.find(chosen);
    auto owner = existing != owners.end() ? existing->second.lock() : Ref<GJGameLevel>();
    bool const duplicate = validIdentity(chosen) && owner && owner.data() != level;
    if (!validIdentity(chosen) || duplicate) {
        do { chosen = randomHexId(); } while (owners.contains(chosen));
    }
    owners[chosen] = level;
    level->setUserObject(kLevelIdentityKey, CCString::create(chosen));
    if (duplicate) {
        auto doc = loadDocument("level-" + identity);
        if (!doc.entries.empty()) {
            for (auto& e : doc.entries) {
                e.assetFile = cloneAssetFile(e.assetFile);
            }
            saveDocument("level-" + chosen, doc);
        }
    }
}

std::string levelKey(GJGameLevel* level) {
    if (!level) return {};
    auto* stored = typeinfo_cast<CCString*>(level->getUserObject(kLevelIdentityKey));
    if (!stored || !validIdentity(stored->getCString())) {
        restoreLevelIdentity(level, "");
        stored = typeinfo_cast<CCString*>(level->getUserObject(kLevelIdentityKey));
    }
    return "level-" + std::string(stored->getCString());
}

void copyLevelDocument(GJGameLevel* source, GJGameLevel* destination) {
    if (!source || !destination || source == destination) return;
    auto* identity = typeinfo_cast<CCString*>(source->getUserObject(kLevelIdentityKey));
    if (!identity) return;
    auto doc = loadDocument(levelKey(source));
    restoreLevelIdentity(destination, "");
    if (!doc.entries.empty()) {
        for (auto& e : doc.entries) {
            e.assetFile = cloneAssetFile(e.assetFile);
        }
        saveDocument(levelKey(destination), doc);
    }
}

ReferenceDocument loadLevelDocument(GJGameLevel* level) {
    if (!level) return {};
    auto const key = levelKey(level);
    return loadDocument(key);
}

ReferenceDocument loadDocument(std::string const& key) {
    auto const path = docPathFor(key);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};
    auto raw = geode::utils::file::readString(paimon::paths::toSdkPath(path));
    if (!raw) {
        log::warn("[EditorReference] Could not read {}: {}", key, raw.unwrapErr());
        return {};
    }
    auto parsed = matjson::parse(raw.unwrap());
    if (!parsed) {
        log::warn("[EditorReference] Invalid JSON in {}: {}", key, parsed.unwrapErr());
        return {};
    }
    return decodeDocument(parsed.unwrap());
}

bool saveDocument(std::string const& key, ReferenceDocument const& doc) {
    auto const path = docPathFor(key);
    std::error_code ec;
    if (doc.entries.empty()) {
        if (std::filesystem::exists(path, ec)) {
            std::filesystem::remove(path, ec);
        }
        return true;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    bool const ok = paimon::file::writeAtomically(
        path, encodeDocument(doc).dump(matjson::NO_INDENTATION)
    );
    if (!ok) log::error("[EditorReference] Failed to save {}", key);
    return ok;
}

bool importImageFile(
    std::filesystem::path const& srcPath,
    std::string& outAssetFile,
    std::string& outName,
    int& outWidth,
    int& outHeight,
    std::string& outError
) {
    auto loaded = ImageLoadHelper::loadStaticImage(srcPath, kMaxImageMB, false);
    if (!loaded.success || !loaded.texture) {
        outError = loaded.error.empty() ? "Could not decode image" : loaded.error;
        return false;
    }
    outWidth = loaded.width;
    outHeight = loaded.height;
    loaded.texture->release();

    std::error_code ec;
    std::filesystem::create_directories(assetsDir(), ec);
    auto const ext = sanitizeExt(srcPath);
    auto const destName = fmt::format("ref_{}{}", randomHexId(), ext);
    auto const destPath = assetsDir() / paimon::assets::pathFromUtf8(destName);

    std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        // Fallback: keep original path if copy failed.
        outAssetFile = paimon::paths::toUtf8(srcPath);
    } else {
        outAssetFile = destName;
    }

    auto stem = paimon::paths::toUtf8(srcPath.stem());
    outName = stem.empty() ? "Image" : stem;
    return true;
}

bool importClipboardImage(
    std::string& outAssetFile,
    std::string& outName,
    int& outWidth,
    int& outHeight,
    std::string& outError
) {
    auto clip = paimon::readImageFromClipboard();
    if (!clip) {
        outError = "No image in clipboard";
        return false;
    }

    ImageLoadHelper::LoadedImage loaded;
    if (clip->isEncodedFile && !clip->encodedBytes.empty()) {
        loaded = ImageLoadHelper::loadWithSTBFromMemory(
            clip->encodedBytes.data(), clip->encodedBytes.size(), false
        );
    } else if (!clip->rgba.empty() && clip->width > 0 && clip->height > 0) {
        loaded = ImageLoadHelper::createFromRGBA(
            clip->rgba.data(), clip->width, clip->height, false
        );
    }

    if (!loaded.success || !loaded.texture) {
        outError = loaded.error.empty() ? "Clipboard image decode failed" : loaded.error;
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(assetsDir(), ec);
    auto const destName = fmt::format("ref_{}.png", randomHexId());
    auto const destPath = assetsDir() / paimon::assets::pathFromUtf8(destName);
    bool const saved = ImageLoadHelper::saveTextureToPng(loaded.texture, destPath);
    outWidth = loaded.width;
    outHeight = loaded.height;
    loaded.texture->release();

    if (!saved) {
        outError = "Could not save clipboard image";
        return false;
    }

    outAssetFile = destName;
    outName = "Clipboard";
    return true;
}

namespace {

std::filesystem::path historyJsonPath() {
    return rootDir() / "history.json";
}

std::int64_t nowUnixMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::uint64_t hashFileBytes(std::filesystem::path const& path, std::uint64_t& outSize) {
    outSize = 0;
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    std::uint64_t h = 14695981039346656037ull;
    char buf[16384];
    while (in.good()) {
        in.read(buf, sizeof(buf));
        auto const n = static_cast<std::size_t>(in.gcount());
        if (n == 0) break;
        outSize += n;
        for (std::size_t i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(buf[i]);
            h *= 1099511628211ull;
        }
    }
    return h;
}

bool saveHistoryList(std::vector<HistoryEntry> const& entries) {
    auto const path = historyJsonPath();
    std::error_code ec;
    if (entries.empty()) {
        if (std::filesystem::exists(path, ec)) {
            std::filesystem::remove(path, ec);
        }
        return true;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    auto root = matjson::Value::object();
    root["version"] = 1;
    std::vector<matjson::Value> arr;
    arr.reserve(entries.size());
    for (auto const& e : entries) {
        auto obj = matjson::Value::object();
        obj["id"] = e.id;
        obj["name"] = e.name;
        obj["assetFile"] = e.assetFile;
        obj["contentHash"] = fmt::format("{:016x}", e.contentHash);
        obj["fileSize"] = static_cast<std::int64_t>(e.fileSize);
        obj["width"] = e.width;
        obj["height"] = e.height;
        obj["usedAtMs"] = e.usedAtMs;
        arr.push_back(std::move(obj));
    }
    root["entries"] = matjson::Value(arr);
    return paimon::file::writeAtomically(path, root.dump(matjson::NO_INDENTATION));
}

std::uint64_t parseHex64(std::string const& s) {
    std::uint64_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint64_t>(c - 'A' + 10);
        else return 0;
    }
    return v;
}

} // namespace

std::filesystem::path historyAssetsDir() {
    return rootDir() / "history";
}

std::filesystem::path resolveHistoryAssetPath(std::string const& assetFile) {
    if (!isManagedAssetName(assetFile)) return {};
    return historyAssetsDir() / paimon::assets::pathFromUtf8(assetFile);
}

std::vector<HistoryEntry> loadHistory() {
    std::vector<HistoryEntry> out;
    auto const path = historyJsonPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return out;
    auto raw = geode::utils::file::readString(paimon::paths::toSdkPath(path));
    if (!raw) return out;
    auto parsed = matjson::parse(raw.unwrap());
    if (!parsed) return out;

    auto const& root = parsed.unwrap();
    bool droppedMissing = false;
    if (auto arr = root["entries"].asArray()) {
        for (auto const& obj : arr.unwrap()) {
            HistoryEntry e;
            e.id = obj["id"].asString().unwrapOr("");
            e.name = obj["name"].asString().unwrapOr("Image");
            e.assetFile = obj["assetFile"].asString().unwrapOr("");
            e.contentHash = parseHex64(obj["contentHash"].asString().unwrapOr(""));
            e.fileSize = static_cast<std::uint64_t>(std::max<std::int64_t>(
                0, obj["fileSize"].asInt().unwrapOr(0)
            ));
            e.width = intOr(obj, "width", 0);
            e.height = intOr(obj, "height", 0);
            e.usedAtMs = obj["usedAtMs"].asInt().unwrapOr(0);
            if (e.id.empty() || !isManagedAssetName(e.assetFile)) {
                droppedMissing = true;
                continue;
            }
            auto const assetPath = resolveHistoryAssetPath(e.assetFile);
            if (!std::filesystem::exists(assetPath, ec)) {
                droppedMissing = true;
                continue;
            }
            out.push_back(std::move(e));
        }
    }

    std::stable_sort(out.begin(), out.end(), [](HistoryEntry const& a, HistoryEntry const& b) {
        return a.usedAtMs > b.usedAtMs;
    });
    if (droppedMissing) {
        saveHistoryList(out);
    }
    return out;
}

void recordHistoryFromAsset(
    std::string const& levelAssetFile,
    std::string const& name,
    int width,
    int height
) {
    auto const srcPath = resolveAssetPath(levelAssetFile);
    std::error_code ec;
    if (srcPath.empty() || !std::filesystem::exists(srcPath, ec)) return;

    std::uint64_t fileSize = 0;
    std::uint64_t const contentHash = hashFileBytes(srcPath, fileSize);
    if (fileSize == 0) return;

    auto entries = loadHistory();
    auto const now = nowUnixMs();

    // Deduplicate identical images by size + content hash, bumping to most recent.
    for (auto& existing : entries) {
        if (existing.fileSize == fileSize && existing.contentHash == contentHash) {
            existing.usedAtMs = now;
            if (!name.empty() && name != "Clipboard") existing.name = name;
            if (width > 0) existing.width = width;
            if (height > 0) existing.height = height;
            std::stable_sort(entries.begin(), entries.end(), [](HistoryEntry const& a, HistoryEntry const& b) {
                return a.usedAtMs > b.usedAtMs;
            });
            saveHistoryList(entries);
            return;
        }
    }

    std::filesystem::create_directories(historyAssetsDir(), ec);
    auto const histId = randomHexId();
    auto const destName = fmt::format("hist_{}{}", histId, sanitizeExt(srcPath));
    auto const destPath = historyAssetsDir() / paimon::assets::pathFromUtf8(destName);
    std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return;

    HistoryEntry entry;
    entry.id = histId;
    entry.name = name.empty() ? "Image" : name;
    entry.assetFile = destName;
    entry.contentHash = contentHash;
    entry.fileSize = fileSize;
    entry.width = width;
    entry.height = height;
    entry.usedAtMs = now;

    entries.insert(entries.begin(), std::move(entry));
    while (entries.size() > static_cast<std::size_t>(kMaxHistoryEntries)) {
        auto const oldFile = entries.back().assetFile;
        entries.pop_back();
        if (isManagedAssetName(oldFile)) {
            std::filesystem::remove(resolveHistoryAssetPath(oldFile), ec);
        }
    }
    saveHistoryList(entries);
}

bool importFromHistory(
    std::string const& historyId,
    std::string& outAssetFile,
    std::string& outName,
    int& outWidth,
    int& outHeight,
    std::string& outError
) {
    auto entries = loadHistory();
    auto it = std::find_if(entries.begin(), entries.end(), [&](HistoryEntry const& e) {
        return e.id == historyId;
    });
    if (it == entries.end()) {
        outError = "History item not found";
        return false;
    }

    auto const srcPath = resolveHistoryAssetPath(it->assetFile);
    std::error_code ec;
    if (srcPath.empty() || !std::filesystem::exists(srcPath, ec)) {
        outError = "History image file missing";
        return false;
    }

    auto loaded = ImageLoadHelper::loadStaticImage(srcPath, kMaxImageMB, false);
    if (!loaded.success || !loaded.texture) {
        outError = loaded.error.empty() ? "Could not decode image" : loaded.error;
        return false;
    }
    outWidth = loaded.width;
    outHeight = loaded.height;
    loaded.texture->release();

    std::filesystem::create_directories(assetsDir(), ec);
    auto const destName = fmt::format("ref_{}{}", randomHexId(), sanitizeExt(srcPath));
    auto const destPath = assetsDir() / paimon::assets::pathFromUtf8(destName);
    std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "Could not copy history image";
        return false;
    }

    outAssetFile = destName;
    outName = it->name;

    it->usedAtMs = nowUnixMs();
    it->width = outWidth;
    it->height = outHeight;
    std::stable_sort(entries.begin(), entries.end(), [](HistoryEntry const& a, HistoryEntry const& b) {
        return a.usedAtMs > b.usedAtMs;
    });
    saveHistoryList(entries);
    return true;
}

bool removeHistoryEntry(std::string const& historyId) {
    auto entries = loadHistory();
    auto it = std::find_if(entries.begin(), entries.end(), [&](HistoryEntry const& e) {
        return e.id == historyId;
    });
    if (it == entries.end()) return false;

    std::error_code ec;
    if (isManagedAssetName(it->assetFile)) {
        std::filesystem::remove(resolveHistoryAssetPath(it->assetFile), ec);
    }
    entries.erase(it);
    saveHistoryList(entries);
    return true;
}

void clearHistory() {
    auto entries = loadHistory();
    std::error_code ec;
    for (auto const& e : entries) {
        if (isManagedAssetName(e.assetFile)) {
            std::filesystem::remove(resolveHistoryAssetPath(e.assetFile), ec);
        }
    }
    if (std::filesystem::exists(historyAssetsDir(), ec)) {
        for (auto const& file : std::filesystem::directory_iterator(historyAssetsDir(), ec)) {
            std::filesystem::remove(file.path(), ec);
        }
    }
    saveHistoryList({});
}

} // namespace paimon::editorref::store
