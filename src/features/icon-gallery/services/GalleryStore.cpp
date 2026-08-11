#include "GalleryStore.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

#include <Geode/utils/file.hpp>
#include <matjson.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

using namespace geode::prelude;

namespace paimon::icon_gallery {

namespace {

    // Four concurrent downloads keep the grid responsive on slow connections.
constexpr std::size_t kMaxInFlight = 4;

constexpr char const* kRootName = "icon-gallery";
constexpr char const* kRegistryCacheName = "registry-cache.json";
constexpr char const* kInstalledIndexName = "installed.json";

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool containsNoCase(std::string const& haystack, std::string const& needleLower) {
    if (needleLower.empty()) return true;
    return lowered(haystack).find(needleLower) != std::string::npos;
}

// "icons/Wumpus.gdicon" -> "Wumpus"
std::string slugFromPath(std::string const& path) {
    auto slash = path.find_last_of('/');
    auto start = slash == std::string::npos ? 0 : slash + 1;
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < start) dot = path.size();
    return path.substr(start, dot - start);
}

    // Sanitize gallery names before using them as disk paths.
bool safeSlug(std::string const& slug) {
    if (slug.empty() || slug.size() > 120) return false;
    if (slug.front() == '.') return false;
    for (unsigned char c : slug) {
        bool const ok = std::isalnum(c) || c == '_' || c == '-' || c == '.' ||
                        c == '(' || c == ')' || c == ' ';
        if (!ok) return false;
    }
    return slug.find("..") == std::string::npos;
}

std::vector<std::uint8_t> readFile(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

bool writeFile(std::filesystem::path const& path, void const* data, std::size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(static_cast<char const*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

}

GalleryStore& GalleryStore::get() {
    static GalleryStore instance;
    return instance;
}


std::filesystem::path GalleryStore::rootDir() {
    return Mod::get()->getSaveDir() / kRootName;
}
std::filesystem::path GalleryStore::metaDir() { return rootDir() / "meta"; }
std::filesystem::path GalleryStore::previewDir() { return rootDir() / "preview"; }
std::filesystem::path GalleryStore::installDir() { return rootDir() / "installed"; }

std::filesystem::path GalleryStore::installDirFor(std::string_view slug) {
    return installDir() / std::string(slug);
}


void GalleryStore::adoptRegistry(std::vector<std::string> const& paths) {
    // Keep loaded metadata so a refresh does not clear the visible grid.
    std::map<std::string, GalleryIcon, std::less<>> previous;
    for (auto& icon : m_icons) {
        if (icon.metaLoaded) previous.emplace(icon.slug, std::move(icon));
    }

    m_icons.clear();
    m_bySlug.clear();
    m_icons.reserve(paths.size());

    for (auto const& path : paths) {
        auto slug = slugFromPath(path);
        if (!safeSlug(slug)) continue;
        if (m_bySlug.count(slug)) continue;

        if (auto it = previous.find(slug); it != previous.end()) {
            m_bySlug.emplace(slug, m_icons.size());
            m_icons.push_back(std::move(it->second));
            continue;
        }
        GalleryIcon icon;
        icon.slug = slug;
        icon.path = path;
        m_bySlug.emplace(slug, m_icons.size());
        m_icons.push_back(std::move(icon));
    }
    m_registryLoaded = !m_icons.empty();
}

void GalleryStore::saveRegistryCache() const {
    auto list = matjson::Value::array();
    for (auto const& icon : m_icons) list.push(icon.path);

    auto root = matjson::Value::object();
    root["paths"] = std::move(list);
    auto dumped = root.dump(matjson::NO_INDENTATION);
    writeFile(rootDir() / kRegistryCacheName, dumped.data(), dumped.size());
}

bool GalleryStore::loadRegistryCache() {
    auto bytes = readFile(rootDir() / kRegistryCacheName);
    if (bytes.empty()) return false;

    auto parsed = matjson::parse(std::string(bytes.begin(), bytes.end()));
    if (!parsed || !parsed.unwrap().isObject()) return false;
    auto const& list = parsed.unwrap()["paths"];
    if (!list.isArray()) return false;

    std::vector<std::string> paths;
    for (auto const& entry : list) {
        if (!entry.isString()) continue;
        paths.push_back(entry.asString().unwrapOr(""));
    }
    if (paths.empty()) return false;

    adoptRegistry(paths);
    return m_registryLoaded;
}

void GalleryStore::loadRegistry(RegistryCallback cb, bool forceNetwork) {
    loadInstalledIndex();

    bool const haveCache = m_registryLoaded || loadRegistryCache();

    // Cache renders immediately; the network only refreshes the list.
    if (haveCache && !forceNetwork && cb) {
        cb(Ok());
        cb = nullptr;
    }

    if (m_registryFetching) {
    // An in-flight request will notify the UI when it completes.
        if (cb && haveCache) cb(Ok());
        return;
    }
    m_registryFetching = true;

    GalleryClient::fetchRegistry([this, cb](Result<std::vector<std::string>> res) {
        m_registryFetching = false;
        if (paimon::isRuntimeShuttingDown()) return;

        if (!res) {
    // Cached data keeps the store usable offline.
            if (m_registryLoaded) {
                if (cb) cb(Ok());
                return;
            }
            if (cb) cb(Err("{}", res.unwrapErr()));
            return;
        }

        adoptRegistry(res.unwrap());
        saveRegistryCache();

        if (cb) {
    // cb already rebuilds the grid; notifying again would duplicate work.
            cb(m_registryLoaded ? Result<>(Ok()) : Err("Catalogo vacio"));
            return;
        }
    // Background refreshes notify an already-rendered cached view.
        if (m_onIconReady) m_onIconReady("");
    });
}

GalleryIcon const* GalleryStore::find(std::string_view slug) const {
    auto it = m_bySlug.find(slug);
    if (it == m_bySlug.end()) return nullptr;
    return &m_icons[it->second];
}


bool GalleryStore::isLoading(std::string const& slug) const {
    return m_inFlight.count(slug) || m_queued.count(slug);
}

bool GalleryStore::failed(std::string const& slug) const {
    return m_failed.count(slug) > 0;
}

cocos2d::CCTexture2D* GalleryStore::previewTexture(std::string const& slug) {
    auto it = m_previews.find(slug);
    return it == m_previews.end() ? nullptr : it->second.data();
}

void GalleryStore::notifyReady(std::string const& slug) {
    if (m_onIconReady) m_onIconReady(slug);
}

void GalleryStore::applyMeta(std::string const& slug, GalleryIcon meta) {
    auto it = m_bySlug.find(slug);
    if (it == m_bySlug.end()) return;
    auto& target = m_icons[it->second];
    // The registry path is the source of truth for the URL.
    meta.path = target.path;
    meta.slug = target.slug;
    target = std::move(meta);
}

void GalleryStore::applyPreview(std::string const& slug,
                                std::vector<std::uint8_t> const& png) {
    if (png.empty()) return;

    auto* image = new cocos2d::CCImage();
    if (!image->initWithImageData(const_cast<std::uint8_t*>(png.data()), png.size())) {
        image->release();
        return;
    }
    auto* texture = new cocos2d::CCTexture2D();
    if (!texture->initWithImage(image)) {
        texture->release();
        image->release();
        return;
    }
    image->release();

    m_previews[slug] = texture;
    texture->release();  // el Ref del mapa ya la retiene
}

bool GalleryStore::loadFromDiskCache(std::string const& slug) {
    auto metaBytes = readFile(metaDir() / (slug + ".json"));
    if (metaBytes.empty()) return false;

    auto meta = GalleryClient::parseMeta(
        std::string(metaBytes.begin(), metaBytes.end()), slug);
    if (!meta) return false;

    applyMeta(slug, std::move(meta.unwrap()));
    if (!m_previews.count(slug)) {
        applyPreview(slug, readFile(previewDir() / (slug + ".png")));
    }
    return true;
}

void GalleryStore::requestIcon(std::string const& slug) {
    auto it = m_bySlug.find(slug);
    if (it == m_bySlug.end()) return;

    auto const& icon = m_icons[it->second];
    bool const needsMeta = !icon.metaLoaded;
    bool const needsPreview = !m_previews.count(slug);
    if (!needsMeta && !needsPreview) return;
    if (isLoading(slug) || m_failed.count(slug)) return;

    if (loadFromDiskCache(slug)) {
        notifyReady(slug);
        return;
    }

    m_queue.push_back(slug);
    m_queued.insert(slug);
    pump();
}

void GalleryStore::pump() {
    while (m_inFlight.size() < kMaxInFlight && !m_queue.empty()) {
        auto slug = m_queue.front();
        m_queue.pop_front();
        m_queued.erase(slug);
        startFetch(slug);
    }
}

void GalleryStore::startFetch(std::string const& slug) {
    auto it = m_bySlug.find(slug);
    if (it == m_bySlug.end()) return;

    m_inFlight.insert(slug);
    GalleryClient::fetchPackage(m_icons[it->second], [this, slug](Result<GalleryPackage> res) {
        m_inFlight.erase(slug);
        if (paimon::isRuntimeShuttingDown()) return;

        if (!res) {
            log::warn("[icon-gallery] '{}' fallo: {}", slug, res.unwrapErr());
            m_failed.insert(slug);
            notifyReady(slug);
            pump();
            return;
        }

        auto pkg = std::move(res.unwrap());

    // Cache the normalized icon.json so the next visit avoids the network.
        auto metaJson = matjson::Value::object();
        metaJson["iconName"] = pkg.meta.name;
        metaJson["iconType"] = iconTypeName(pkg.meta.type);
        metaJson["author"] = pkg.meta.author;
        metaJson["description"] = pkg.meta.description;
        metaJson["format"] = pkg.meta.format;
        metaJson["uuid"] = pkg.meta.uuid;
        metaJson["isCollab"] = pkg.meta.isCollab;
        metaJson["hasProjectFiles"] = pkg.meta.hasProjectFiles;
        auto collab = matjson::Value::array();
        for (auto const& who : pkg.meta.collabWith) collab.push(who);
        metaJson["collabWith"] = std::move(collab);
        if (pkg.meta.hasColors) {
            auto colors = matjson::Value::array();
            auto set = matjson::Value::object();
            auto hex = [](cocos2d::ccColor3B c) {
                return fmt::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
            };
            set["p1"] = hex(pkg.meta.color1);
            set["p2"] = hex(pkg.meta.color2);
            set["glow"] = hex(pkg.meta.colorGlow);
            colors.push(std::move(set));
            metaJson["colors"] = std::move(colors);
        }
    // Store dates in ISO so reparsing preserves ordering.
        {
            auto secs = static_cast<std::time_t>(pkg.meta.createdAtMs / 1000);
            std::tm tmv{};
#ifdef GEODE_IS_WINDOWS
            gmtime_s(&tmv, &secs);
#else
            gmtime_r(&secs, &tmv);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
            metaJson["creationDate"] = std::string(buf);
        }
        auto dumped = metaJson.dump(matjson::NO_INDENTATION);
        writeFile(metaDir() / (slug + ".json"), dumped.data(), dumped.size());
        if (!pkg.preview.empty()) {
            writeFile(previewDir() / (slug + ".png"), pkg.preview.data(), pkg.preview.size());
        }

        applyMeta(slug, pkg.meta);
        applyPreview(slug, pkg.preview);
        notifyReady(slug);
        pump();
    });
}

void GalleryStore::fetchPackage(std::string const& slug, PackageCallback cb) {
    auto it = m_bySlug.find(slug);
    if (it == m_bySlug.end()) {
        if (cb) cb(Err("Icono desconocido"));
        return;
    }
    GalleryClient::fetchPackage(m_icons[it->second], [cb](Result<GalleryPackage> res) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (cb) cb(std::move(res));
    });
}


std::vector<std::size_t> GalleryStore::query(Query const& q) const {
    auto const needle = lowered(q.search);
    std::vector<std::size_t> out;
    out.reserve(m_icons.size());

    for (std::size_t i = 0; i < m_icons.size(); ++i) {
        auto const& icon = m_icons[i];

        if (q.onlyInstalled && !m_installed.count(icon.slug)) continue;

    // Game-mode filters require metadata; unknown icons are excluded.
        if (!q.types.empty()) {
            if (!icon.metaLoaded) continue;
            if (!q.types.count(static_cast<int>(icon.type))) continue;
        }

        if (!needle.empty()) {
            bool const hit = containsNoCase(icon.displayName(), needle) ||
                             containsNoCase(icon.slug, needle) ||
                             (icon.metaLoaded && containsNoCase(icon.author, needle));
            if (!hit) continue;
        }
        out.push_back(i);
    }

    auto const& icons = m_icons;
    switch (q.sort) {
        case GallerySort::Newest:
            std::stable_sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) {
                return icons[a].createdAtMs > icons[b].createdAtMs;
            });
            break;
        case GallerySort::Oldest:
            std::stable_sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) {
                return icons[a].createdAtMs < icons[b].createdAtMs;
            });
            break;
        case GallerySort::NameAsc:
            std::stable_sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) {
                return lowered(icons[a].displayName()) < lowered(icons[b].displayName());
            });
            break;
        case GallerySort::AuthorAsc:
            std::stable_sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) {
                return lowered(icons[a].author) < lowered(icons[b].author);
            });
            break;
    }
    return out;
}


void GalleryStore::loadInstalledIndex() {
    if (m_installedLoaded) return;
    m_installedLoaded = true;

    auto bytes = readFile(rootDir() / kInstalledIndexName);
    if (bytes.empty()) return;
    auto parsed = matjson::parse(std::string(bytes.begin(), bytes.end()));
    if (!parsed || !parsed.unwrap().isArray()) return;
    for (auto const& entry : parsed.unwrap()) {
        if (!entry.isString()) continue;
        auto slug = entry.asString().unwrapOr("");
        if (!slug.empty()) m_installed.insert(std::move(slug));
    }
}

void GalleryStore::saveInstalledIndex() const {
    auto list = matjson::Value::array();
    for (auto const& slug : m_installed) list.push(slug);
    auto dumped = list.dump(matjson::NO_INDENTATION);
    writeFile(rootDir() / kInstalledIndexName, dumped.data(), dumped.size());
}

bool GalleryStore::isInstalled(std::string const& slug) const {
    return m_installed.count(slug) > 0;
}

void GalleryStore::markInstalled(std::string const& slug, bool installed) {
    if (installed) {
        m_installed.insert(slug);
    } else {
        m_installed.erase(slug);
    }
    saveInstalledIndex();
}

void GalleryStore::onGLContextReload() {
    m_previews.clear();
}

}
