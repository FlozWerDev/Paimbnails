#include "EmoteService.hpp"
#include "../../../utils/WebHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cctype>
#include <cstdlib>

using namespace geode::prelude;
using namespace paimon::emotes;

static constexpr auto EMOTE_SERVER = "https://paimbnails-emote.vercel.app";

// Catalog format version — bump when URL format or schema changes.
// Old disk catalogs with a different version are discarded on load.
static constexpr int CATALOG_VERSION = 2;

static EmoteType parseEmoteType(std::string const& typeStr) {
    if (typeStr == "gif") return EmoteType::Gif;
    return EmoteType::Static;
}

static void dispatchCatalogCallback(EmoteService::CatalogCallback const& callback, bool success) {
    if (!callback) return;
    Loader::get()->queueInMainThread([callback, success]() {
        callback(success);
    });
}

// Convert legacy Bunny CDN / storage URLs to Vercel proxy URLs.
// Old format: https://paimbnails.b-cdn.net/emotes/file.webp
// New format: https://paimbnails-emote.vercel.app/api/paimon-emote/file?name=file.webp
static std::string migrateEmoteUrl(std::string const& url, std::string const& filename) {
    if (url.find("b-cdn.net/") != std::string::npos ||
        url.find("storage.bunnycdn.com/") != std::string::npos) {
        return fmt::format("{}/api/paimon-emote/file?name={}", EMOTE_SERVER, filename);
    }
    return url;
}

void EmoteService::fetchAllEmotes(CatalogCallback callback) {
    if (m_fetching.exchange(true, std::memory_order_acq_rel)) {
        dispatchCatalogCallback(callback, false);
        return;
    }

    // Grab current timelast for incremental check
    std::string currentTimelast;
    {
        std::lock_guard lock(m_mutex);
        currentTimelast = m_timelast;
    }

    // Shared accumulator across pages
    auto accumulator = std::make_shared<std::vector<EmoteInfo>>();
    auto cb = std::make_shared<CatalogCallback>(std::move(callback));

    fetchPage(1, 100, currentTimelast, *accumulator, [this, accumulator, cb](bool success) {
        if (success && !accumulator->empty()) {
            size_t allCount = 0;
            size_t gifCount = 0;
            size_t staticCount = 0;
            {
                std::lock_guard lock(m_mutex);
                m_allEmotes = std::move(*accumulator);
                buildIndex();
                allCount = m_allEmotes.size();
                gifCount = m_gifEmotes.size();
                staticCount = m_staticEmotes.size();
                m_loaded.store(true, std::memory_order_release);
            }
            saveCatalogToDisk();
            log::info("[EmoteService] Catalog loaded: {} emotes ({} gif, {} static)",
                allCount, gifCount, staticCount);
        } else if (success) {
            // Server said no changes (accumulator empty, timelast matched)
            log::info("[EmoteService] Emote catalog up to date (no changes)");
        } else {
            log::warn("[EmoteService] Failed to fetch emote catalog from server");
        }
        m_fetching.store(false, std::memory_order_release);
        dispatchCatalogCallback(*cb, success);
    });
}

void EmoteService::fetchPage(int page, int limit, std::string const& timelast,
                             std::vector<EmoteInfo>& accumulator, CatalogCallback callback) {
    std::string url = fmt::format("{}/api/paimon-emote?page={}&limit={}", EMOTE_SERVER, page, limit);
    if (!timelast.empty()) {
        url += "&timelast=" + timelast;
    }

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(15));
    req.header("Accept", "application/json");

    WebHelper::dispatch(std::move(req), "GET", url, [this, page, limit, timelast, &accumulator, callback = std::move(callback)](web::WebResponse res) mutable {
        if (!res.ok()) {
            log::warn("[EmoteService] HTTP {} fetching emote page {}", res.code(), page);
            callback(false);
            return;
        }

        auto body = res.string().unwrapOr("");
        auto jsonRes = matjson::parse(body);
        if (!jsonRes.isOk()) {
            log::error("[EmoteService] JSON parse error on page {}", page);
            callback(false);
            return;
        }

        auto json = jsonRes.unwrap();

        // Handle "no changes" response
        if (json.contains("changed") && json["changed"].asBool().unwrapOr(true) == false) {
            log::debug("[EmoteService] Server reports no changes since timelast {}", timelast);
            callback(true); // success, but accumulator stays empty
            return;
        }

        // Update timelast from server response
        if (json.contains("timelast")) {
            auto newTimelast = json["timelast"].asString().unwrapOr("");
            if (!newTimelast.empty()) {
                std::lock_guard lock(m_mutex);
                m_timelast = newTimelast;
            }
        }

        // Parse emotes array
        if (json.contains("emotes") && json["emotes"].isArray()) {
            auto arrRes = json["emotes"].asArray();
            if (arrRes.isOk()) {
                for (auto const& item : arrRes.unwrap()) {
                    EmoteInfo info;
                    info.name = item["name"].asString().unwrapOr("");
                    info.filename = item["filename"].asString().unwrapOr("");
                    info.type = parseEmoteType(item["type"].asString().unwrapOr("png"));
                    info.category = item["category"].asString().unwrapOr("");
                    info.size = static_cast<int>(item["size"].asInt().unwrapOr(0));
                    info.url = item["url"].asString().unwrapOr("");

                    // Migrate legacy Bunny CDN URLs to Vercel proxy
                    info.url = migrateEmoteUrl(info.url, info.filename);

                    if (!info.name.empty() && !info.url.empty()) {
                        accumulator.push_back(std::move(info));
                    }
                }
            }
        }

        // Check pagination
        bool hasNext = false;
        if (json.contains("pagination") && json["pagination"].isObject()) {
            auto pag = json["pagination"];
            hasNext = pag["hasNext"].asBool().unwrapOr(false);
        }

        if (hasNext) {
            // Fetch next page (don't send timelast on subsequent pages)
            fetchPage(page + 1, limit, "", accumulator, std::move(callback));
        } else {
            callback(true);
        }
    });
}

// Case-insensitive helper — delegates to Geode's string utility.
static std::string toLowerStr(std::string const& s) {
    return geode::utils::string::toLower(s);
}

void EmoteService::buildIndex() {
    // Must be called with m_mutex held
    m_nameIndex.clear();
    m_gifEmotes.clear();
    m_staticEmotes.clear();

    for (size_t i = 0; i < m_allEmotes.size(); ++i) {
        auto const& e = m_allEmotes[i];
        // Store lowercase key for case-insensitive lookup
        m_nameIndex[toLowerStr(e.name)] = i;

        if (e.type == EmoteType::Gif) {
            m_gifEmotes.push_back(e);
        } else {
            m_staticEmotes.push_back(e);
        }
    }
}

std::optional<EmoteInfo> EmoteService::getEmoteByName(std::string const& name) const {
    std::lock_guard lock(m_mutex);
    // Case-insensitive lookup: index keys are stored lowercase
    auto it = m_nameIndex.find(toLowerStr(name));
    if (it == m_nameIndex.end()) return std::nullopt;
    if (it->second >= m_allEmotes.size()) return std::nullopt;
    return m_allEmotes[it->second];
}

std::vector<EmoteInfo> EmoteService::searchEmotes(std::string const& query, size_t maxResults) const {
    if (query.empty()) return {};
    std::lock_guard lock(m_mutex);

    // Lowercase query for case-insensitive matching
    std::string lq = query;
    for (auto& c : lq) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<EmoteInfo> startsWithMatches;
    std::vector<EmoteInfo> containsMatches;

    for (auto const& e : m_allEmotes) {
        std::string ln = e.name;
        for (auto& c : ln) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ln.starts_with(lq)) {
            startsWithMatches.push_back(e);
        } else if (ln.find(lq) != std::string::npos) {
            containsMatches.push_back(e);
        }

        if (startsWithMatches.size() >= maxResults) break;
    }

    // Prioritize starts-with matches, then fill with contains
    std::vector<EmoteInfo> results = std::move(startsWithMatches);
    for (auto& e : containsMatches) {
        if (results.size() >= maxResults) break;
        results.push_back(std::move(e));
    }
    return results;
}

// ─── Disk persistence ───

static std::filesystem::path getCatalogPath() {
    return Mod::get()->getSaveDir() / "emote_catalog.json";
}

void EmoteService::saveCatalogToDisk() {
    std::vector<EmoteInfo> emotesSnapshot;
    std::string timelastSnapshot;
    {
        std::lock_guard lock(m_mutex);
        emotesSnapshot = m_allEmotes;
        timelastSnapshot = m_timelast;
    }

    matjson::Value arr = matjson::Value::array();
    for (auto const& e : emotesSnapshot) {
        matjson::Value obj = matjson::Value::object();
        obj["name"] = e.name;
        obj["filename"] = e.filename;
        obj["type"] = (e.type == EmoteType::Gif) ? "gif" : "static";
        obj["category"] = e.category;
        obj["size"] = e.size;
        obj["url"] = e.url;
        arr.push(obj);
    }

    matjson::Value root = matjson::Value::object();
    root["emotes"] = arr;
    root["timelast"] = timelastSnapshot;
    root["catalogVersion"] = CATALOG_VERSION;
    root["savedAt"] = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    auto path = getCatalogPath();
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        log::warn("[EmoteService] Failed to save catalog to disk: could not open file");
        return;
    }
    ofs << root.dump();
}

void EmoteService::loadCatalogFromDisk() {
    auto path = getCatalogPath();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());

    auto jsonRes = matjson::parse(content);
    if (!jsonRes.isOk()) {
        log::warn("[EmoteService] Failed to parse catalog JSON from disk");
        return;
    }

    auto json = jsonRes.unwrap();

    // Check TTL
    auto savedAt = json["savedAt"].asInt().unwrapOr(0);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    if (now - savedAt > CATALOG_TTL_SECONDS) {
        log::info("[EmoteService] Disk catalog expired (age: {}s), will re-fetch", now - savedAt);
        std::error_code removeEc;
        std::filesystem::remove(path, removeEc);
        return;
    }

    // Check catalog version — discard old format catalogs
    auto diskVersion = static_cast<int>(json["catalogVersion"].asInt().unwrapOr(0));
    if (diskVersion < CATALOG_VERSION) {
        log::info("[EmoteService] Disk catalog version {} < {}, discarding", diskVersion, CATALOG_VERSION);
        std::error_code removeEc;
        std::filesystem::remove(path, removeEc);
        return;
    }

    if (!json.contains("emotes") || !json["emotes"].isArray()) return;

    // Restore timelast
    auto savedTimelast = json["timelast"].asString().unwrapOr("");

    std::lock_guard lock(m_mutex);
    m_allEmotes.clear();
    m_timelast = savedTimelast;

    auto arrRes = json["emotes"].asArray();
    if (!arrRes.isOk()) return;

    for (auto const& item : arrRes.unwrap()) {
        EmoteInfo info;
        info.name = item["name"].asString().unwrapOr("");
        info.filename = item["filename"].asString().unwrapOr("");
        info.type = parseEmoteType(item["type"].asString().unwrapOr("static"));
        info.category = item["category"].asString().unwrapOr("");
        info.size = static_cast<int>(item["size"].asInt().unwrapOr(0));
        info.url = item["url"].asString().unwrapOr("");

        // Migrate legacy Bunny CDN URLs to Vercel proxy
        info.url = migrateEmoteUrl(info.url, info.filename);

        if (!info.name.empty() && !info.url.empty()) {
            m_allEmotes.push_back(std::move(info));
        }
    }

    buildIndex();
    m_loaded.store(true, std::memory_order_release);
    log::info("[EmoteService] Loaded {} emotes from disk cache", m_allEmotes.size());
}

std::vector<std::string> EmoteService::getCategories(EmoteType type) const {
    std::lock_guard lock(m_mutex);
    auto const& src = (type == EmoteType::Gif) ? m_gifEmotes : m_staticEmotes;
    std::vector<std::string> cats;
    for (auto const& e : src) {
        auto const& c = e.category;
        if (c.empty()) continue;
        bool found = false;
        for (auto const& existing : cats) {
            if (existing == c) { found = true; break; }
        }
        if (!found) cats.push_back(c);
    }
    return cats;
}

std::vector<EmoteInfo> EmoteService::getEmotesByCategory(EmoteType type, std::string const& category) const {
    std::lock_guard lock(m_mutex);
    auto const& src = (type == EmoteType::Gif) ? m_gifEmotes : m_staticEmotes;
    std::vector<EmoteInfo> result;
    for (auto const& e : src) {
        if (e.category == category) result.push_back(e);
    }
    return result;
}

std::vector<std::string> EmoteService::getAllCategories() const {
    std::lock_guard lock(m_mutex);
    std::vector<std::string> cats;
    for (auto const& e : m_allEmotes) {
        if (e.category.empty()) continue;
        bool found = false;
        for (auto const& c : cats) {
            if (c == e.category) { found = true; break; }
        }
        if (!found) cats.push_back(e.category);
    }
    return cats;
}

std::vector<EmoteInfo> EmoteService::getAllEmotesByCategory(std::string const& category) const {
    std::lock_guard lock(m_mutex);
    std::vector<EmoteInfo> result;
    for (auto const& e : m_allEmotes) {
        if (e.category == category) result.push_back(e);
    }
    return result;
}

std::optional<EmoteInfo> EmoteService::getRandomEmote() const {
    std::lock_guard lock(m_mutex);
    if (m_staticEmotes.empty()) return std::nullopt;
    auto idx = static_cast<size_t>(std::rand()) % m_staticEmotes.size();
    return m_staticEmotes[idx];
}

void EmoteService::clearCatalog() {
    {
        std::lock_guard lock(m_mutex);
        m_allEmotes.clear();
        m_gifEmotes.clear();
        m_staticEmotes.clear();
        m_nameIndex.clear();
    }
    m_loaded.store(false, std::memory_order_release);

    // Clear timelast so next fetch is a full fetch
    {
        std::lock_guard lock(m_mutex);
        m_timelast.clear();
    }

    std::error_code ec;
    std::filesystem::remove(getCatalogPath(), ec);
    log::info("[EmoteService] Catalog cleared");
}
