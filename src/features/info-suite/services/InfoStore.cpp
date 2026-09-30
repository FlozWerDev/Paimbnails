#include "InfoStore.hpp"
#include "../../../utils/JsonHelper.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <climits>
#include <fstream>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr size_t kMaxPages = 200;
constexpr size_t kMaxUsernames = 5000;
constexpr size_t kMaxLevelDates = 5000;
constexpr size_t kMaxCommentSamples = 4000;

std::filesystem::path storePath() {
    return Mod::get()->getSaveDir() / "info_suite.json";
}

std::string asString(matjson::Value const& value) {
    auto res = value.asString();
    return res.isOk() ? res.unwrap() : std::string{};
}

int64_t asInt(matjson::Value const& value) {
    auto res = value.asInt();
    return res.isOk() ? res.unwrap() : 0;
}

} // namespace

InfoStore& InfoStore::get() {
    static InfoStore instance;
    return instance;
}

void InfoStore::load() {
    auto path = storePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    std::streamoff const size = file.tellg();
    if (size <= 0 || size > 8ll * 1024 * 1024) return;
    file.seekg(0, std::ios::beg);
    std::string contents(static_cast<size_t>(size), '\0');
    if (!file.read(contents.data(), size)) return;

    auto parsed = matjson::parse(contents);
    if (!parsed.isOk()) {
        log::warn("[Paimbnails] info_suite.json corrupto, se empieza de cero");
        return;
    }
    auto root = parsed.unwrap();

    // iterating a matjson object yields the entries themselves; the key comes
    // from getkey() on each one.
    if (root["lastPages"].isObject()) {
        for (auto const& entry : root["lastPages"]) {
            if (m_lastPages.size() >= kMaxPages) break;
            auto key = entry.getKey();
            if (!key) continue;
            auto page = asInt(entry);
            if (page > 0 && page <= INT_MAX) m_lastPages[*key] = static_cast<int>(page);
        }
    }
    if (root["usernames"].isObject()) {
        for (auto const& entry : root["usernames"]) {
            if (m_usernames.size() >= kMaxUsernames) break;
            auto key = entry.getKey();
            if (!key) continue;
            auto id = geode::utils::numFromString<int>(*key);
            if (id.isOk()) m_usernames[id.unwrap()] = asString(entry);
        }
    }
    if (root["levelDates"].isObject()) {
        for (auto const& entry : root["levelDates"]) {
            if (m_levelDates.size() >= kMaxLevelDates) break;
            auto key = entry.getKey();
            if (!key) continue;
            auto id = geode::utils::numFromString<int>(*key);
            if (id.isOk()) m_levelDates[id.unwrap()] = asString(entry);
        }
    }

    paimon::json::forEachInArray(root["commentSamples"], [&](matjson::Value const& item) {
        if (m_commentSamples.size() >= kMaxCommentSamples) return;
        int64_t id = asInt(item["id"]);
        int64_t time = asInt(item["t"]);
        if (id > 0 && time > 0) m_commentSamples.emplace_back(id, time);
    });
    std::sort(m_commentSamples.begin(), m_commentSamples.end());

}

void InfoStore::save() {
    if (!m_dirty) return;

    auto root = matjson::Value::object();

    auto pages = matjson::Value::object();
    for (auto const& [key, page] : m_lastPages) pages[key] = page;
    root["lastPages"] = pages;

    auto names = matjson::Value::object();
    for (auto const& [id, name] : m_usernames) names[std::to_string(id)] = name;
    root["usernames"] = names;

    auto dates = matjson::Value::object();
    for (auto const& [id, date] : m_levelDates) dates[std::to_string(id)] = date;
    root["levelDates"] = dates;

    auto samples = matjson::Value::array();
    for (auto const& [id, time] : m_commentSamples) {
        auto entry = matjson::Value::object();
        entry["id"] = id;
        entry["t"] = time;
        samples.push(entry);
    }
    root["commentSamples"] = samples;

    std::ofstream file(storePath(), std::ios::trunc);
    if (!file.is_open()) {
        log::warn("[Paimbnails] no se pudo escribir info_suite.json");
        return;
    }
    file << root.dump(matjson::NO_INDENTATION);
    file.close();
    if (!file) return;
    m_dirty = false;
}

std::optional<int> InfoStore::lastPage(std::string const& searchKey) const {
    auto it = m_lastPages.find(searchKey);
    if (it == m_lastPages.end()) return std::nullopt;
    return it->second;
}

void InfoStore::setLastPage(std::string const& searchKey, int page) {
    if (searchKey.empty()) return;
    if (page <= 0) {
        // page 0 is the default, so storing it would only waste a slot.
        if (m_lastPages.erase(searchKey) > 0) m_dirty = true;
        return;
    }

    auto it = m_lastPages.find(searchKey);
    if (it != m_lastPages.end() && it->second == page) return;

    if (m_lastPages.size() >= kMaxPages && it == m_lastPages.end()) {
        m_lastPages.erase(m_lastPages.begin());
    }
    m_lastPages[searchKey] = page;
    m_dirty = true;
}

std::string InfoStore::username(int userID) const {
    auto it = m_usernames.find(userID);
    return it == m_usernames.end() ? std::string{} : it->second;
}

void InfoStore::rememberUsername(int userID, std::string const& name) {
    if (userID <= 0 || name.empty()) return;

    auto it = m_usernames.find(userID);
    if (it != m_usernames.end() && it->second == name) return;

    if (m_usernames.size() >= kMaxUsernames && it == m_usernames.end()) {
        m_usernames.erase(m_usernames.begin());
    }
    m_usernames[userID] = name;
    m_dirty = true;
}

std::string InfoStore::levelDate(int levelID) const {
    auto it = m_levelDates.find(levelID);
    return it == m_levelDates.end() ? std::string{} : it->second;
}

void InfoStore::setLevelDate(int levelID, std::string const& isoDate) {
    if (levelID <= 0 || isoDate.empty()) return;
    if (m_levelDates.size() >= kMaxLevelDates && !m_levelDates.count(levelID)) {
        m_levelDates.erase(m_levelDates.begin());
    }
    m_levelDates[levelID] = isoDate;
    m_dirty = true;
}

void InfoStore::addCommentSample(int64_t commentID, int64_t epochSeconds) {
    if (commentID <= 0 || epochSeconds <= 0) return;

    auto it = std::lower_bound(m_commentSamples.begin(), m_commentSamples.end(),
                               std::pair<int64_t, int64_t>{commentID, 0});
    if (it != m_commentSamples.end() && it->first == commentID) return;

    m_commentSamples.insert(it, {commentID, epochSeconds});
    if (m_commentSamples.size() > kMaxCommentSamples) {
        // thin out the oldest half of the ids; the newest range is the one
        // people actually browse.
        m_commentSamples.erase(m_commentSamples.begin(),
                               m_commentSamples.begin() + kMaxCommentSamples / 4);
    }
    m_dirty = true;
}

int64_t InfoStore::estimateCommentTime(int64_t commentID) const {
    if (commentID <= 0 || m_commentSamples.size() < 2) return 0;

    auto upper = std::lower_bound(m_commentSamples.begin(), m_commentSamples.end(),
                                  std::pair<int64_t, int64_t>{commentID, 0});
    // needs a sample on each side; extrapolating past the ends is guesswork.
    if (upper == m_commentSamples.begin() || upper == m_commentSamples.end()) return 0;

    auto lower = std::prev(upper);
    if (lower->first == commentID) return lower->second;

    double span = static_cast<double>(upper->first - lower->first);
    if (span <= 0.0) return lower->second;

    double ratio = static_cast<double>(commentID - lower->first) / span;
    double delta = static_cast<double>(upper->second - lower->second) * ratio;
    return lower->second + static_cast<int64_t>(delta);
}

} // namespace paimon::info
