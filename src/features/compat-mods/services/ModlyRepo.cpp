#include "ModlyRepo.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::compat_mods {

namespace {
    // Re-entering the tab within this window reuses what is already in memory.
    constexpr std::time_t kCatalogTTL = 600;

    std::string jsonStr(matjson::Value const& v, std::string const& def = "") {
        if (v.isString()) return v.asString().unwrapOr(def);
        return def;
    }

    int64_t jsonInt(matjson::Value const& v, int64_t def = 0) {
        if (v.isNumber()) return static_cast<int64_t>(v.asDouble().unwrapOr(static_cast<double>(def)));
        return def;
    }

    bool jsonBool(matjson::Value const& v, bool def = false) {
        if (v.isBool()) return v.asBool().unwrapOr(def);
        return def;
    }

    ModlyMod parseMod(matjson::Value const& v) {
        ModlyMod mod;
        mod.id = jsonStr(v["id"]);
        mod.name = jsonStr(v["name"]);
        mod.description = jsonStr(v["description"]);
        mod.version = jsonStr(v["version"]);
        mod.link = jsonStr(v["link"]);
        mod.gdps = jsonStr(v["gdps"]);
        mod.discord = jsonStr(v["discord"]);
        mod.kofi = jsonStr(v["kofi"]);
        mod.repo = jsonStr(v["repo"]);
        mod.type = jsonStr(v["type"], "mod");
        mod.state = jsonStr(v["state"]);
        mod.authorUid = jsonStr(v["authorUid"]);
        mod.authorName = jsonStr(v["authorName"]);
        mod.downloads = static_cast<int>(jsonInt(v["downloads"]));
        mod.date = jsonInt(v["date"]);
        mod.previewCount = static_cast<int>(jsonInt(v["previewCount"]));
        mod.hasLogo = jsonBool(v["hasLogo"]);
        return mod;
    }

    ModlyUser parseUser(matjson::Value const& v) {
        ModlyUser user;
        user.uid = jsonStr(v["uid"]);
        user.name = jsonStr(v["name"]);
        user.description = jsonStr(v["description"]);
        user.rank = jsonStr(v["rank"]);
        user.verified = jsonBool(v["verified"]);
        user.hasPhoto = jsonBool(v["hasPhoto"]);
        user.hasBanner = jsonBool(v["hasBanner"]);
        if (v["tags"].isArray()) {
            if (auto arr = v["tags"].asArray(); arr.isOk()) {
                for (auto const& tag : arr.unwrap()) {
                    auto text = jsonStr(tag);
                    if (!text.empty()) user.tags.push_back(text);
                }
            }
        }
        return user;
    }

    ModlyComment parseComment(matjson::Value const& v) {
        ModlyComment comment;
        comment.id = jsonStr(v["id"]);
        comment.text = jsonStr(v["text"]);
        comment.authorUid = jsonStr(v["authorUid"]);
        comment.authorName = jsonStr(v["authorName"]);
        comment.date = jsonInt(v["date"]);
        return comment;
    }
} // namespace

ModlyRepo& ModlyRepo::get() {
    static ModlyRepo instance;
    return instance;
}

std::string ModlyRepo::apiBase() const {
    return HttpClient::get().getServerURL() + "/api/modly";
}

std::vector<ModlyMod> ModlyRepo::modsByAuthor(std::string const& uid) const {
    std::vector<ModlyMod> out;
    if (uid.empty()) return out;
    for (auto const& mod : m_mods) {
        if (mod.authorUid == uid) out.push_back(mod);
    }
    return out;
}

ModlyUser const* ModlyRepo::user(std::string const& uid) const {
    auto it = m_users.find(uid);
    return it == m_users.end() ? nullptr : &it->second;
}

void ModlyRepo::deliverCatalog(bool ok) {
    auto pending = std::move(m_pendingCatalog);
    m_pendingCatalog.clear();
    m_catalogInFlight = false;
    for (auto const& cb : pending) cb(ok);
}

void ModlyRepo::fetchCatalog(bool force, CatalogCallback callback) {
    if (!force && m_hasCatalog && std::time(nullptr) - m_catalogFetchedAt < kCatalogTTL) {
        callback(true);
        return;
    }

    m_pendingCatalog.push_back(std::move(callback));
    if (m_catalogInFlight) return;
    m_catalogInFlight = true;

    HttpClient::get().get("/api/modly/catalog", [](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([success, response]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto& repo = ModlyRepo::get();

            if (!success) {
                log::warn("[Modly] catalog request failed: {}", response);
                repo.deliverCatalog(false);
                return;
            }

            auto parsed = matjson::parse(response);
            if (!parsed.isOk()) {
                log::warn("[Modly] catalog is not valid JSON");
                repo.deliverCatalog(false);
                return;
            }

            auto json = parsed.unwrap();
            std::vector<ModlyMod> mods;
            std::unordered_map<std::string, ModlyUser> users;

            if (json["mods"].isArray()) {
                if (auto arr = json["mods"].asArray(); arr.isOk()) {
                    for (auto const& item : arr.unwrap()) {
                        auto mod = parseMod(item);
                        if (!mod.id.empty() && !mod.name.empty()) mods.push_back(std::move(mod));
                    }
                }
            }

            if (json["users"].isArray()) {
                if (auto arr = json["users"].asArray(); arr.isOk()) {
                    for (auto const& item : arr.unwrap()) {
                        auto user = parseUser(item);
                        if (!user.uid.empty()) users.emplace(user.uid, std::move(user));
                    }
                }
            }

            repo.m_mods = std::move(mods);
            repo.m_users = std::move(users);
            repo.m_hasCatalog = true;
            repo.m_catalogFetchedAt = std::time(nullptr);
            log::info("[Modly] catalog loaded: {} mods, {} profiles", repo.m_mods.size(), repo.m_users.size());
            repo.deliverCatalog(true);
        });
    });
}

void ModlyRepo::fetchComments(std::string const& modId, bool force, CommentsCallback callback) {
    if (modId.empty()) {
        callback(false, {});
        return;
    }

    if (!force) {
        auto it = m_comments.find(modId);
        if (it != m_comments.end()) {
            callback(true, it->second);
            return;
        }
    }

    HttpClient::get().get("/api/modly/comments/" + modId,
        [modId, callback = std::move(callback)](bool success, std::string const& response) {
            Loader::get()->queueInMainThread([modId, callback, success, response]() {
                if (paimon::isRuntimeShuttingDown()) return;

                if (!success) {
                    log::warn("[Modly] comments request failed for {}: {}", modId, response);
                    callback(false, {});
                    return;
                }

                auto parsed = matjson::parse(response);
                if (!parsed.isOk()) {
                    callback(false, {});
                    return;
                }

                std::vector<ModlyComment> comments;
                auto json = parsed.unwrap();
                if (json["comments"].isArray()) {
                    if (auto arr = json["comments"].asArray(); arr.isOk()) {
                        for (auto const& item : arr.unwrap()) {
                            auto comment = parseComment(item);
                            if (!comment.text.empty()) comments.push_back(std::move(comment));
                        }
                    }
                }

                auto& repo = ModlyRepo::get();

                // Commenters are not necessarily authors, so the catalog does not
                // carry them; the comments payload brings their profiles along.
                if (json["users"].isArray()) {
                    if (auto arr = json["users"].asArray(); arr.isOk()) {
                        for (auto const& item : arr.unwrap()) {
                            auto user = parseUser(item);
                            if (!user.uid.empty()) repo.m_users.insert_or_assign(user.uid, std::move(user));
                        }
                    }
                }

                repo.m_comments[modId] = comments;
                callback(true, comments);
            });
        });
}

std::string ModlyRepo::logoUrl(ModlyMod const& mod) const {
    return apiBase() + "/img/mod/" + mod.id + "/logo.png";
}

std::string ModlyRepo::previewUrl(ModlyMod const& mod, int index) const {
    return previewUrlBase(mod) + std::to_string(index) + ".png";
}

std::string ModlyRepo::previewUrlBase(ModlyMod const& mod) const {
    return apiBase() + "/img/mod/" + mod.id + "/preview/";
}

std::string ModlyRepo::photoUrl(ModlyUser const& user) const {
    return apiBase() + "/img/user/" + user.uid + "/photo.png";
}

std::string ModlyRepo::bannerUrl(ModlyUser const& user) const {
    return apiBase() + "/img/user/" + user.uid + "/banner.png";
}

void ModlyRepo::clearCache() {
    m_mods.clear();
    m_users.clear();
    m_comments.clear();
    m_hasCatalog = false;
    m_catalogFetchedAt = 0;
}

std::string formatModlyDate(int64_t epoch) {
    if (epoch <= 0) return "";

    static char const* monthsEs[] = {"ene", "feb", "mar", "abr", "may", "jun",
                                     "jul", "ago", "sep", "oct", "nov", "dic"};
    static char const* monthsEn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    int month = std::clamp(tm.tm_mon, 0, 11);
    bool spanish = Localization::get().getLanguage() == Localization::Language::SPANISH;
    return fmt::format("{} {} {}", tm.tm_mday, spanish ? monthsEs[month] : monthsEn[month], tm.tm_year + 1900);
}

} // namespace paimon::compat_mods
