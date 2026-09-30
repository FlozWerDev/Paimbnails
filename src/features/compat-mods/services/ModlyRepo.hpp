#pragma once

#include "ModlyTypes.hpp"
#include <Geode/loader/Types.hpp>
#include <Geode/utils/general.hpp>
#include <ctime>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

// read-only modly mirror client: direct reads 403 behind app check, mirror serves flat json + png.

namespace paimon::compat_mods {

class ModlyRepo {
public:
    using CatalogCallback = geode::CopyableFunction<void(bool ok)>;
    using CommentsCallback = geode::CopyableFunction<void(bool ok, std::vector<ModlyComment> const& comments)>;

    static ModlyRepo& get();

    // approved mods, newest first. only valid once fetchcatalog reported success.
    std::vector<ModlyMod> const& mods() const { return m_mods; }

    // every mod whose author is uid, keeping the catalog order.
    std::vector<ModlyMod> modsByAuthor(std::string const& uid) const;

    // returns nullptr when the author never filled a profile.
    ModlyUser const* user(std::string const& uid) const;

    bool hasCatalog() const { return m_hasCatalog; }

    // serves cache younger than ttl unless forced; callback always on main thread.
    void fetchCatalog(bool force, CatalogCallback callback);

    // comments are fetched per mod and cached for the session.
    void fetchComments(std::string const& modId, bool force, CommentsCallback callback);

    std::string logoUrl(ModlyMod const& mod) const;
    std::string previewUrl(ModlyMod const& mod, int index) const;   // index is 1-based
    std::string photoUrl(ModlyUser const& user) const;
    std::string bannerUrl(ModlyUser const& user) const;
    // prefix accepted by modpreviewgallerypopup, which appends "<n>.png".
    std::string previewUrlBase(ModlyMod const& mod) const;

private:
    ModlyRepo() = default;

    std::string apiBase() const;
    void deliverCatalog(bool ok);

    std::vector<ModlyMod> m_mods;
    std::unordered_map<std::string, ModlyUser> m_users;
    std::unordered_map<std::string, std::vector<ModlyComment>> m_comments;

    std::vector<CatalogCallback> m_pendingCatalog;
    bool m_catalogInFlight = false;
    bool m_hasCatalog = false;
    std::chrono::steady_clock::time_point m_catalogFetchedAt;
};

// "8 jul 2026" in the active language, matching how the site prints dates.
std::string formatModlyDate(int64_t epoch);

} // namespace paimon::compat_mods
