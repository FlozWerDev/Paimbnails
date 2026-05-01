#include "LevelTagsIntegration.hpp"
#include "../../../utils/WebHelper.hpp"
#include <matjson.hpp>

using namespace geode::prelude;

namespace paimon::foryou {

LevelTagsIntegration& LevelTagsIntegration::get() {
    static LevelTagsIntegration instance;
    return instance;
}

std::vector<std::string> LevelTagsIntegration::parseTags(matjson::Value const& data, int levelID) const {
    std::vector<std::string> tags;
    auto key = std::to_string(levelID);

    if (!data.contains(key) || !data[key].isObject()) return tags;

    auto const& levelObj = data[key];
    // Level-Tags format: { style: [...], theme: [...], meta: [...], gameplay: [...] }
    for (auto const& category : {"style", "theme", "meta", "gameplay"}) {
        if (levelObj.contains(category) && levelObj[category].isArray()) {
            for (auto const& tag : levelObj[category].asArray().unwrap()) {
                auto str = tag.asString().unwrapOr("");
                if (!str.empty()) {
                    tags.push_back(str);
                }
            }
        }
    }
    return tags;
}

void LevelTagsIntegration::fetchTagsForLevel(int levelID, std::function<void(std::vector<std::string>)> callback) {
    // Check cache first
    {
        std::lock_guard lock(m_mutex);
        auto it = m_cache.find(levelID);
        if (it != m_cache.end()) {
            if (callback) callback(it->second);
            return;
        }
    }

    std::string url = std::string(LEVEL_TAGS_SERVER) + "/get?id=" + std::to_string(levelID);

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(8));
    req.acceptEncoding("gzip, deflate");

    WebHelper::dispatch(std::move(req), "GET", url, [this, levelID, callback](web::WebResponse res) {
        std::vector<std::string> tags;
        if (res.ok()) {
            auto body = res.string().unwrapOr("");
            auto parsed = matjson::parse(body);
            if (parsed.isOk()) {
                tags = parseTags(parsed.unwrap(), levelID);
            }
        }

        {
            std::lock_guard lock(m_mutex);
            m_cache[levelID] = tags;
        }

        if (callback) callback(tags);
    });
}

void LevelTagsIntegration::fetchTagsForLevels(
    std::vector<int> const& levelIDs,
    std::function<void(std::unordered_map<int, std::vector<std::string>>)> callback
) {
    if (levelIDs.empty()) {
        if (callback) callback({});
        return;
    }

    auto results = std::make_shared<std::unordered_map<int, std::vector<std::string>>>();
    auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(levelIDs.size()));

    for (int id : levelIDs) {
        fetchTagsForLevel(id, [results, remaining, id, callback](std::vector<std::string> tags) {
            (*results)[id] = tags;
            if (remaining->fetch_sub(1) == 1 && callback) {
                callback(*results);
            }
        });
    }
}

std::vector<std::string> LevelTagsIntegration::getCachedTags(int levelID) const {
    std::lock_guard lock(m_mutex);
    auto it = m_cache.find(levelID);
    if (it != m_cache.end()) return it->second;
    return {};
}

bool LevelTagsIntegration::hasCachedTags(int levelID) const {
    std::lock_guard lock(m_mutex);
    return m_cache.count(levelID) > 0;
}

} // namespace paimon::foryou
