#include "EmoteService.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
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

// default api host; override at runtime via the paimon-emote-server-url saved value.
static constexpr auto DEFAULT_EMOTE_SERVER = "https://paimbnailsbot.onrender.com";

static std::string resolveEmoteServer() {
    auto* mod = Mod::get();
    std::string url = mod ? mod->getSavedValue<std::string>("paimon-emote-server-url", "") : std::string{};
    if (url.empty()) {
        url = DEFAULT_EMOTE_SERVER;
    }
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

static constexpr int CATALOG_VERSION = 4;

static EmoteType parseEmoteType(std::string const& typeStr) {
    if (typeStr == "gif") return EmoteType::Gif;
    return EmoteType::Static;
}

static EmoteType classifyByFilename(std::string const& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return EmoteType::Static;
    std::string ext = filename.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "gif") return EmoteType::Gif;
    return EmoteType::Static;
}

void EmoteService::dispatchCatalogCallbacks(std::vector<CatalogCallback> callbacks, bool success, size_t generation) {
    if (callbacks.empty()) return;
    Loader::get()->queueInMainThread([this, callbacks = std::move(callbacks), success, generation]() {
        for (auto const& callback : callbacks) {
            if (paimon::isRuntimeShuttingDown()) return;
            bool current;
            {
                std::lock_guard lock(m_mutex);
                current = generation == m_catalogGeneration;
            }
            callback(success && current);
        }
    });
}

static std::string migrateEmoteUrl(std::string const& url, std::string const& filename) {
    bool isLegacy =
        url.find("b-cdn.net/") != std::string::npos ||
        url.find("storage.bunnycdn.com/") != std::string::npos ||
        url.find("paimbnails-emote.vercel.app") != std::string::npos;
    if (isLegacy) {
        return fmt::format("{}/api/paimon-emote/file?name={}", resolveEmoteServer(), filename);
    }
    return url;
}

void EmoteService::fetchAllEmotes(CatalogCallback callback) {
    if (paimon::isRuntimeShuttingDown()) return;

    std::string currentTimelast;
    size_t generation;
    std::shared_ptr<std::vector<CatalogCallback>> callbacks;
    {
        std::lock_guard lock(m_mutex);
        if (auto pending = m_catalogCallbacks.lock()) {
            if (callback) pending->push_back(std::move(callback));
            return;
        }
        callbacks = std::make_shared<std::vector<CatalogCallback>>();
        if (callback) callbacks->push_back(std::move(callback));
        // the web request owns callbacks so shutdown cannot strand ui refs in the singleton.
        m_catalogCallbacks = callbacks;
        m_fetching.store(true, std::memory_order_release);
        generation = m_catalogGeneration;
        currentTimelast = m_timelast;
    }

    auto accumulator = std::make_shared<std::vector<EmoteInfo>>();

    fetchPage(1, 100, currentTimelast, accumulator, generation, [this, accumulator, callbacks, generation](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        size_t allCount = 0;
        size_t gifCount = 0;
        size_t staticCount = 0;
        std::vector<CatalogCallback> pending;
        {
            std::lock_guard lock(m_mutex);
            if (generation != m_catalogGeneration) return;
            if (success && !accumulator->empty()) {
                m_allEmotes = std::move(*accumulator);
                buildIndex();
                allCount = m_allEmotes.size();
                gifCount = m_gifEmotes.size();
                staticCount = m_staticEmotes.size();
                m_loaded.store(true, std::memory_order_release);
            }
            pending.swap(*callbacks);
            m_catalogCallbacks.reset();
            m_fetching.store(false, std::memory_order_release);
        }
        if (allCount > 0) {
            saveCatalogToDisk();
            log::info("[EmoteService] Catalog loaded: {} emotes ({} gif, {} static)",
                allCount, gifCount, staticCount);
        } else if (success) {
            log::info("[EmoteService] Emote catalog up to date (no changes)");
        } else {
            log::warn("[EmoteService] Failed to fetch emote catalog from server");
        }
        dispatchCatalogCallbacks(std::move(pending), success, generation);
    });
}

void EmoteService::fetchPage(int page, int limit, std::string const& timelast,
                             std::shared_ptr<std::vector<EmoteInfo>> accumulator, size_t generation, CatalogCallback callback) {
    std::string url = fmt::format("{}/api/paimon-emote?page={}&limit={}", resolveEmoteServer(), page, limit);
    if (!timelast.empty()) {
        url += "&timelast=" + timelast;
    }

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(15));
    req.acceptEncoding("gzip, deflate");
    req.header("Accept", "application/json");

    WebHelper::dispatch(std::move(req), "GET", url, [this, page, limit, timelast, accumulator, generation, callback = std::move(callback)](web::WebResponse res) mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        {
            std::lock_guard lock(m_mutex);
            if (generation != m_catalogGeneration) return;
        }
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

        if (json.contains("changed") && json["changed"].asBool().unwrapOr(true) == false) {
            log::debug("[EmoteService] Server reports no changes since timelast {}", timelast);
            callback(true);
            return;
        }

        if (json.contains("timelast")) {
            auto newTimelast = json["timelast"].asString().unwrapOr("");
            if (!newTimelast.empty()) {
                std::lock_guard lock(m_mutex);
                if (generation != m_catalogGeneration) return;
                m_timelast = newTimelast;
            }
        }

        if (json.contains("emotes") && json["emotes"].isArray()) {
            auto arrRes = json["emotes"].asArray();
            if (arrRes.isOk()) {
                for (auto const& item : arrRes.unwrap()) {
                    EmoteInfo info;
                    info.name = item["name"].asString().unwrapOr("");
                    info.filename = item["filename"].asString().unwrapOr("");
                    info.type = parseEmoteType(item["type"].asString().unwrapOr("png"));
                    if (info.type != EmoteType::Gif &&
                        classifyByFilename(info.filename) == EmoteType::Gif) {
                        info.type = EmoteType::Gif;
                    }
                    info.category = item["category"].asString().unwrapOr("");
                    info.size = static_cast<int>(item["size"].asInt().unwrapOr(0));
                    info.url = item["url"].asString().unwrapOr("");

                    info.url = migrateEmoteUrl(info.url, info.filename);

                    if (!info.name.empty() && !info.url.empty()) {
                        accumulator->push_back(std::move(info));
                    }
                }
            }
        }

        bool hasNext = false;
        if (json.contains("pagination") && json["pagination"].isObject()) {
            auto pag = json["pagination"];
            hasNext = pag["hasNext"].asBool().unwrapOr(false);
        }

        if (hasNext) {
            fetchPage(page + 1, limit, "", std::move(accumulator), generation, std::move(callback));
        } else {
            callback(true);
        }
    });
}

static std::string toLowerStr(std::string const& s) {
    return geode::utils::string::toLower(s);
}

void EmoteService::buildIndex() {
    // must be called with m_mutex held
    m_nameIndex.clear();
    m_gifEmotes.clear();
    m_staticEmotes.clear();

    for (size_t i = 0; i < m_allEmotes.size(); ++i) {
        auto const& e = m_allEmotes[i];
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
    auto it = m_nameIndex.find(toLowerStr(name));
    if (it == m_nameIndex.end()) return std::nullopt;
    if (it->second >= m_allEmotes.size()) return std::nullopt;
    return m_allEmotes[it->second];
}

std::vector<EmoteInfo> EmoteService::searchEmotes(std::string const& query, size_t maxResults) const {
    if (query.empty()) return {};
    std::lock_guard lock(m_mutex);

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

    std::vector<EmoteInfo> results = std::move(startsWithMatches);
    for (auto& e : containsMatches) {
        if (results.size() >= maxResults) break;
        results.push_back(std::move(e));
    }
    return results;
}

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
    ofs.close();
    if (!ofs) log::warn("[EmoteService] Failed to save catalog to disk");
}

void EmoteService::loadCatalogFromDisk() {
    auto path = getCatalogPath();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return;

    std::streamoff const size = ifs.tellg();
    if (size <= 0 || size > 16ll * 1024 * 1024) return;
    ifs.seekg(0, std::ios::beg);
    std::string content(static_cast<size_t>(size), '\0');
    if (!ifs.read(content.data(), size)) return;

    auto jsonRes = matjson::parse(content);
    if (!jsonRes.isOk()) {
        log::warn("[EmoteService] Failed to parse catalog JSON from disk");
        return;
    }

    auto json = jsonRes.unwrap();

    auto savedAt = json["savedAt"].asInt().unwrapOr(0);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    auto age = savedAt > 0 && savedAt <= now ? now - savedAt : -1;

    if (age < 0 || age > CATALOG_TTL_SECONDS) {
        log::info("[EmoteService] Disk catalog expired or invalid, will re-fetch");
        std::error_code removeEc;
        std::filesystem::remove(path, removeEc);
        return;
    }

    auto diskVersion = json["catalogVersion"].asInt().unwrapOr(0);
    if (diskVersion < CATALOG_VERSION) {
        log::info("[EmoteService] Disk catalog version {} < {}, discarding", diskVersion, CATALOG_VERSION);
        std::error_code removeEc;
        std::filesystem::remove(path, removeEc);
        return;
    }

    if (!json.contains("emotes") || !json["emotes"].isArray()) return;

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
        if (info.type != EmoteType::Gif &&
            classifyByFilename(info.filename) == EmoteType::Gif) {
            info.type = EmoteType::Gif;
        }
        info.category = item["category"].asString().unwrapOr("");
        info.size = static_cast<int>(item["size"].asInt().unwrapOr(0));
        info.url = item["url"].asString().unwrapOr("");

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
    std::vector<CatalogCallback> callbacks;
    size_t generation;
    {
        std::lock_guard lock(m_mutex);
        generation = ++m_catalogGeneration;
        if (auto pending = m_catalogCallbacks.lock()) callbacks.swap(*pending);
        m_catalogCallbacks.reset();
        m_fetching.store(false, std::memory_order_release);
        m_allEmotes.clear();
        m_gifEmotes.clear();
        m_staticEmotes.clear();
        m_nameIndex.clear();
        m_loaded.store(false, std::memory_order_release);
        m_timelast.clear();
    }

    std::error_code ec;
    std::filesystem::remove(getCatalogPath(), ec);
    dispatchCatalogCallbacks(std::move(callbacks), false, generation);
    log::info("[EmoteService] Catalog cleared");
}
