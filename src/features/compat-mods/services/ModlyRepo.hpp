#pragma once

#include "ModlyTypes.hpp"
#include <Geode/loader/Types.hpp>
#include <Geode/utils/general.hpp>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

// Read-only client for the Modly mirror.
//
// modly.web.app itself has no API: every read goes through the Firebase SDK
// straight to Firestore, and the whole "mods-web" project sits behind App Check
// with reCAPTCHA v3, so a plain request answers 403 PERMISSION_DENIED. The mirror
// (cloudflare-server/modly-mirror.js) is what reads Firestore with a service
// account and re-serves it as flat JSON plus plain PNG endpoints.

namespace paimon::compat_mods {

class ModlyRepo {
public:
    using CatalogCallback = geode::CopyableFunction<void(bool ok)>;
    using CommentsCallback = geode::CopyableFunction<void(bool ok, std::vector<ModlyComment> const& comments)>;

    static ModlyRepo& get();

    // Approved mods, newest first. Only valid once fetchCatalog reported success.
    std::vector<ModlyMod> const& mods() const { return m_mods; }

    // Every mod whose author is uid, keeping the catalog order.
    std::vector<ModlyMod> modsByAuthor(std::string const& uid) const;

    // Returns nullptr when the author never filled a profile.
    ModlyUser const* user(std::string const& uid) const;

    bool hasCatalog() const { return m_hasCatalog; }

    // Serves from cache while it is younger than the TTL unless force is set.
    // The callback always runs on the main thread.
    void fetchCatalog(bool force, CatalogCallback callback);

    // Comments are fetched per mod and cached for the session.
    void fetchComments(std::string const& modId, bool force, CommentsCallback callback);

    std::string logoUrl(ModlyMod const& mod) const;
    std::string previewUrl(ModlyMod const& mod, int index) const;   // index is 1-based
    std::string photoUrl(ModlyUser const& user) const;
    std::string bannerUrl(ModlyUser const& user) const;
    // Prefix accepted by ModPreviewGalleryPopup, which appends "<n>.png".
    std::string previewUrlBase(ModlyMod const& mod) const;

    void clearCache();

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
    std::time_t m_catalogFetchedAt = 0;
};

// "8 jul 2026" in the active language, matching how the site prints dates.
std::string formatModlyDate(int64_t epoch);

} // namespace paimon::compat_mods
