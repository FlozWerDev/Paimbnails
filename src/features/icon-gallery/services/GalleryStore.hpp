#pragma once
// Main-thread catalog store. Metadata/previews are fetched lazily and cached as
// meta/<slug>.json and preview/<slug>.png.

#include "../IconGalleryTypes.hpp"
#include "GalleryClient.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace paimon::icon_gallery {

class GalleryStore final {
public:
    using RegistryCallback = std::function<void(geode::Result<>)>;
    // Called when an icon finishes loading metadata or preview.
    using IconReadyCallback = std::function<void(std::string const& slug)>;

    static GalleryStore& get();

    static std::filesystem::path rootDir();
    static std::filesystem::path metaDir();
    static std::filesystem::path previewDir();
    static std::filesystem::path installDir();
    static std::filesystem::path installDirFor(std::string_view slug);

    // Load disk cache first, then refresh from the network; cb fires once usable.
    void loadRegistry(RegistryCallback cb, bool forceNetwork = false);
    bool registryLoaded() const { return m_registryLoaded; }
    std::vector<GalleryIcon> const& icons() const { return m_icons; }

    GalleryIcon const* find(std::string_view slug) const;

    // Queue a missing icon unless already loaded or in flight.
    void requestIcon(std::string const& slug);
    bool isLoading(std::string const& slug) const;
    bool failed(std::string const& slug) const;

    cocos2d::CCTexture2D* previewTexture(std::string const& slug);

    void setOnIconReady(IconReadyCallback cb) { m_onIconReady = std::move(cb); }
    void clearOnIconReady() { m_onIconReady = nullptr; }

    struct Query {
        std::string search;
        // Empty means all gamemodes.
        std::set<int> types;
        bool onlyInstalled = false;
        GallerySort sort = GallerySort::Newest;
    };
    // Return matching indices; metadata-free icons remain for name searches.
    std::vector<std::size_t> query(Query const& q) const;

    bool isInstalled(std::string const& slug) const;
    void markInstalled(std::string const& slug, bool installed);
    std::size_t installedCount() const { return m_installed.size(); }

    // Download the full .gdicon, using the disk cache when available.
    using PackageCallback = std::function<void(geode::Result<GalleryPackage>)>;
    void fetchPackage(std::string const& slug, PackageCallback cb);

    // Release textures before GL context recreation.
    void onGLContextReload();

private:
    GalleryStore() = default;
    ~GalleryStore() = default;
    GalleryStore(GalleryStore const&) = delete;
    GalleryStore& operator=(GalleryStore const&) = delete;

    void adoptRegistry(std::vector<std::string> const& paths);
    void saveRegistryCache() const;
    bool loadRegistryCache();
    void loadInstalledIndex();
    void saveInstalledIndex() const;

    void pump();
    void startFetch(std::string const& slug);
    void applyMeta(std::string const& slug, GalleryIcon meta);
    void applyPreview(std::string const& slug, std::vector<std::uint8_t> const& png);
    bool loadFromDiskCache(std::string const& slug);
    void notifyReady(std::string const& slug);

    bool m_registryLoaded = false;
    bool m_registryFetching = false;
    bool m_installedLoaded = false;
    std::vector<GalleryIcon> m_icons;
    std::map<std::string, std::size_t, std::less<>> m_bySlug;

    std::map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_previews;
    std::set<std::string> m_installed;
    std::set<std::string> m_failed;
    std::set<std::string> m_inFlight;
    std::deque<std::string> m_queue;
    std::set<std::string> m_queued;

    IconReadyCallback m_onIconReady;
};

}
