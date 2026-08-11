#include "LevelTagsClient.hpp"

#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/WebHelper.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

using namespace geode::prelude;

namespace paimon::foryou {

char const* tagCategoryKey(TagCategory category) {
    switch (category) {
        case TagCategory::Style:    return "style";
        case TagCategory::Theme:    return "theme";
        case TagCategory::Meta:     return "meta";
        case TagCategory::Gameplay: return "gameplay";
        default:                    return "unknown";
    }
}

TagCategory tagCategoryFromKey(std::string_view key) {
    if (key == "style")    return TagCategory::Style;
    if (key == "theme")    return TagCategory::Theme;
    if (key == "meta")     return TagCategory::Meta;
    if (key == "gameplay") return TagCategory::Gameplay;
    return TagCategory::Unknown;
}

LevelTagsClient& LevelTagsClient::get() {
    static LevelTagsClient instance;
    return instance;
}

bool LevelTagsClient::isAvailable() {
    // Queried on each call rather than cached: the user can toggle the mod at
    // runtime and the feed has to notice.
    return Loader::get()->isModLoaded(kModID);
}

std::string LevelTagsClient::serverURL() {
    auto* mod = Loader::get()->getLoadedMod(kModID);
    if (mod && mod->hasSetting("serverUrl")) {
        auto url = mod->getSettingValue<std::string>("serverUrl");
        // Trailing slashes would produce "…//get"; the server tolerates it but
        // our own cache keys would differ per user, so normalize.
        while (!url.empty() && url.back() == '/') url.pop_back();
        if (!url.empty()) return url;
    }
    return kDefaultServer;
}

std::string LevelTagsClient::urlEncode(std::string const& value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << static_cast<char>(c);
        } else {
            out << '%' << std::setw(2) << static_cast<unsigned int>(c);
        }
    }
    return out.str();
}


void LevelTagsClient::parseCatalog(matjson::Value const& root) {
    if (!root.isObject()) return;

    std::lock_guard lock(m_mutex);
    for (auto const& [categoryKey, tags] : root) {
        auto category = tagCategoryFromKey(categoryKey);
        if (!tags.isObject()) continue;

        for (auto const& [name, entry] : tags) {
            TagInfo info;
            info.name = name;
            info.category = category;

            // entry is [availability, [r,g,b], description]
            if (entry.isArray()) {
                auto fields = paimon::json::arrayOrEmpty(entry);
                if (fields.size() > 1 && fields[1].isArray()) {
                    auto rgb = paimon::json::arrayOrEmpty(fields[1]);
                    if (rgb.size() >= 3) {
                        info.color = {
                            static_cast<GLubyte>(std::clamp<int>(rgb[0].asInt().unwrapOr(255), 0, 255)),
                            static_cast<GLubyte>(std::clamp<int>(rgb[1].asInt().unwrapOr(255), 0, 255)),
                            static_cast<GLubyte>(std::clamp<int>(rgb[2].asInt().unwrapOr(255), 0, 255))
                        };
                    }
                }
                if (fields.size() > 2) {
                    info.description = fields[2].asString().unwrapOr("");
                }
            }

            m_catalog[name] = std::move(info);
        }
    }
    m_catalogLoaded = !m_catalog.empty();
}

void LevelTagsClient::loadCatalog(std::function<void(bool)> callback) {
    if (!isAvailable()) {
        if (callback) callback(false);
        return;
    }

    bool alreadyLoaded = false;
    bool startRequest = false;
    {
        std::lock_guard lock(m_mutex);
        if (m_catalogLoaded) {
            alreadyLoaded = true;
        } else {
            // Concurrent callers queue up behind the single in-flight request.
            if (callback) m_catalogWaiters.push_back(std::move(callback));
            startRequest = !m_catalogPending;
            m_catalogPending = true;
        }
    }

    // Answered outside the lock so the callback is free to re-enter us.
    if (alreadyLoaded) {
        if (callback) callback(true);
        return;
    }
    if (!startRequest) return;

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(12));
    req.acceptEncoding("gzip, deflate");

    WebHelper::dispatch(std::move(req), "GET", serverURL() + "/tags", [this](web::WebResponse res) {
        bool ok = false;
        if (res.ok()) {
            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (parsed.isOk()) {
                parseCatalog(parsed.unwrap());
                ok = hasCatalog();
            }
        }

        std::vector<std::function<void(bool)>> waiters;
        {
            std::lock_guard lock(m_mutex);
            m_catalogPending = false;
            waiters.swap(m_catalogWaiters);
        }
        if (!ok) log::warn("[ForYou] Level Tags catalog request failed");
        for (auto& waiter : waiters) waiter(ok);
    });
}

bool LevelTagsClient::hasCatalog() const {
    std::lock_guard lock(m_mutex);
    return m_catalogLoaded;
}

std::vector<TagInfo> LevelTagsClient::catalog() const {
    std::lock_guard lock(m_mutex);
    std::vector<TagInfo> out;
    out.reserve(m_catalog.size());
    for (auto const& [name, info] : m_catalog) out.push_back(info);
    std::sort(out.begin(), out.end(), [](TagInfo const& a, TagInfo const& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.name < b.name;
    });
    return out;
}

std::vector<TagInfo> LevelTagsClient::catalogFor(TagCategory category) const {
    std::lock_guard lock(m_mutex);
    std::vector<TagInfo> out;
    for (auto const& [name, info] : m_catalog) {
        if (info.category == category) out.push_back(info);
    }
    std::sort(out.begin(), out.end(), [](TagInfo const& a, TagInfo const& b) {
        return a.name < b.name;
    });
    return out;
}

TagCategory LevelTagsClient::categoryOf(std::string const& tag) const {
    std::lock_guard lock(m_mutex);
    auto it = m_catalog.find(tag);
    return it != m_catalog.end() ? it->second.category : TagCategory::Unknown;
}

std::optional<TagInfo> LevelTagsClient::infoFor(std::string const& tag) const {
    std::lock_guard lock(m_mutex);
    auto it = m_catalog.find(tag);
    if (it == m_catalog.end()) return std::nullopt;
    return it->second;
}


LevelTagMap LevelTagsClient::parseLevelTags(matjson::Value const& root) const {
    LevelTagMap out;
    if (!root.isObject()) return out;

    for (auto const& [key, levelObj] : root) {
        auto idResult = geode::utils::numFromString<int>(key);
        if (!idResult.isOk()) continue;

        std::vector<std::string> tags;
        if (levelObj.isObject()) {
            for (auto const* category : {"style", "theme", "meta", "gameplay"}) {
                paimon::json::forEachInArray(levelObj[category], [&](matjson::Value const& tag) {
                    auto name = tag.asString().unwrapOr("");
                    if (!name.empty()) tags.push_back(std::move(name));
                });
            }
        }
        // An empty vector is a real answer ("this level has no tags") and is
        // cached as such, so we never re-request it.
        out[idResult.unwrap()] = std::move(tags);
    }
    return out;
}

void LevelTagsClient::fetchTags(std::vector<int> const& levelIDs, std::function<void(LevelTagMap)> callback) {
    auto results = std::make_shared<LevelTagMap>();
    std::vector<int> missing;

    {
        std::lock_guard lock(m_mutex);
        for (int id : levelIDs) {
            if (id <= 0) continue;
            auto it = m_levelTags.find(id);
            if (it != m_levelTags.end()) {
                (*results)[id] = it->second;
            } else {
                missing.push_back(id);
            }
        }
    }

    // Duplicate IDs in the input would otherwise be requested twice.
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());

    if (missing.empty() || !isAvailable()) {
        if (callback) callback(*results);
        return;
    }

    std::vector<std::vector<int>> chunks;
    for (size_t i = 0; i < missing.size(); i += kFetchChunkSize) {
        auto end = std::min(i + kFetchChunkSize, missing.size());
        chunks.emplace_back(missing.begin() + i, missing.begin() + end);
    }

    auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(chunks.size()));
    auto shared = std::make_shared<std::function<void(LevelTagMap)>>(std::move(callback));
    auto base = serverURL();

    for (auto const& chunk : chunks) {
        std::string ids;
        for (size_t i = 0; i < chunk.size(); i++) {
            if (i) ids += ',';
            ids += std::to_string(chunk[i]);
        }

        auto req = web::WebRequest();
        req.timeout(std::chrono::seconds(12));
        req.acceptEncoding("gzip, deflate");

        WebHelper::dispatch(std::move(req), "GET", base + "/get?id=" + ids,
            [this, chunk, results, remaining, shared](web::WebResponse res) {
                LevelTagMap parsed;
                if (res.ok()) {
                    auto json = matjson::parse(res.string().unwrapOr(""));
                    if (json.isOk()) parsed = parseLevelTags(json.unwrap());
                }

                {
                    std::lock_guard lock(m_mutex);
                    for (auto const& [id, tags] : parsed) {
                        m_levelTags[id] = tags;
                        (*results)[id] = tags;
                        m_dirty = true;
                    }
                    // IDs the server omitted entirely are untagged; record that
                    // so a feed refresh doesn't ask again.
                    for (int id : chunk) {
                        if (!m_levelTags.count(id)) {
                            m_levelTags[id] = {};
                            (*results)[id] = {};
                            m_dirty = true;
                        }
                    }
                }

                if (remaining->fetch_sub(1) == 1) {
                    saveDiskCache();
                    if (shared && *shared) (*shared)(*results);
                }
            });
    }
}

std::vector<std::string> LevelTagsClient::cachedTags(int levelID) const {
    std::lock_guard lock(m_mutex);
    auto it = m_levelTags.find(levelID);
    if (it == m_levelTags.end()) return {};
    return it->second;
}

bool LevelTagsClient::isResolved(int levelID) const {
    std::lock_guard lock(m_mutex);
    return m_levelTags.count(levelID) > 0;
}


void LevelTagsClient::searchByTags(
    std::vector<std::string> const& include,
    std::vector<std::string> const& exclude,
    std::function<void(std::vector<int>)> callback
) {
    if (!isAvailable() || include.empty()) {
        if (callback) callback({});
        return;
    }

    auto join = [](std::vector<std::string> const& tags) {
        std::string out;
        for (size_t i = 0; i < tags.size(); i++) {
            if (i) out += ',';
            out += tags[i];
        }
        return out;
    };

    std::string query = "?i=" + urlEncode(join(include));
    if (!exclude.empty()) query += "&e=" + urlEncode(join(exclude));

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(12));
    req.acceptEncoding("gzip, deflate");

    WebHelper::dispatch(std::move(req), "GET", serverURL() + "/search" + query,
        [callback](web::WebResponse res) {
            std::vector<int> ids;
            if (res.ok()) {
                auto json = matjson::parse(res.string().unwrapOr(""));
                if (json.isOk()) {
                    // The server answers with an array of ID strings.
                    paimon::json::forEachInArray(json.unwrap(), [&](matjson::Value const& entry) {
                        int id = 0;
                        if (entry.isString()) {
                            auto parsed = geode::utils::numFromString<int>(entry.asString().unwrapOr(""));
                            if (parsed.isOk()) id = parsed.unwrap();
                        } else {
                            id = entry.asInt().unwrapOr(0);
                        }
                        if (id > 0) ids.push_back(id);
                    });
                }
            }
            if (callback) callback(std::move(ids));
        });
}


std::filesystem::path LevelTagsClient::cachePath() const {
    return Mod::get()->getSaveDir() / "foryou_leveltags_cache.json";
}

void LevelTagsClient::loadDiskCache() {
    {
        std::lock_guard lock(m_mutex);
        if (m_diskCacheLoaded) return;
        m_diskCacheLoaded = true;
    }

    auto path = cachePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return;

    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    auto parsed = matjson::parse(contents);
    if (!parsed.isOk()) return;
    auto root = parsed.unwrap();

    // Tags get edited upstream; a month-old snapshot is refetched rather than trusted.
    constexpr int64_t kMaxAge = 30LL * 24 * 60 * 60;
    auto savedAt = static_cast<int64_t>(root["savedAt"].asInt().unwrapOr(0));
    if (savedAt > 0 && std::time(nullptr) - savedAt > kMaxAge) return;

    if (!root["levels"].isObject()) return;

    std::lock_guard lock(m_mutex);
    for (auto const& [key, tagsValue] : root["levels"]) {
        auto idResult = geode::utils::numFromString<int>(key);
        if (!idResult.isOk()) continue;
        std::vector<std::string> tags;
        paimon::json::forEachInArray(tagsValue, [&](matjson::Value const& tag) {
            auto name = tag.asString().unwrapOr("");
            if (!name.empty()) tags.push_back(std::move(name));
        });
        m_levelTags[idResult.unwrap()] = std::move(tags);
    }
    log::info("[ForYou] Level Tags cache loaded for {} levels", m_levelTags.size());
}

void LevelTagsClient::saveDiskCache() {
    auto root = matjson::Value::object();
    {
        std::lock_guard lock(m_mutex);
        if (!m_dirty) return;

        auto levels = matjson::Value::object();
        for (auto const& [id, tags] : m_levelTags) {
            auto arr = matjson::Value::array();
            for (auto const& tag : tags) arr.push(tag);
            levels[std::to_string(id)] = arr;
        }
        root["levels"] = levels;
        root["savedAt"] = static_cast<int64_t>(std::time(nullptr));
        m_dirty = false;
    }

    auto path = cachePath();
    auto tmpPath = std::filesystem::path(path).replace_extension(".tmp");

    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;
    file << root.dump();
    file.close();

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) log::warn("[ForYou] Failed to persist Level Tags cache: {}", ec.message());
}

} // namespace paimon::foryou
