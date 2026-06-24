#include "SearchHistory.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <ctime>

using namespace geode::prelude;

namespace paimon::searchhistory {

std::vector<Entry> history;

bool Entry::operator==(const Entry& o) const {
    // Same day (ignores exact time) to avoid duplicating same-day searches.
    return (time - time % 86400) == (o.time - o.time % 86400)
        && type == o.type && query == o.query
        && difficulties == o.difficulties && lengths == o.lengths
        && uncompleted == o.uncompleted && completed == o.completed
        && featured == o.featured && original == o.original
        && twoPlayer == o.twoPlayer && coins == o.coins
        && epic == o.epic && legendary == o.legendary && mythic == o.mythic
        && song == o.song && (!song || (customSong == o.customSong && songID == o.songID))
        && demonFilter == o.demonFilter && noStar == o.noStar && star == o.star;
}

std::string Entry::summary() const {
    std::vector<std::string> parts;
    if (type == 2) parts.push_back("Users");
    else if (type == 1) parts.push_back("Lists");
    if (star) parts.push_back("Star");
    if (noStar) parts.push_back("No Star");
    if (featured) parts.push_back("Featured");
    if (epic) parts.push_back("Epic");
    if (legendary) parts.push_back("Legendary");
    if (mythic) parts.push_back("Mythic");
    if (original) parts.push_back("Original");
    if (twoPlayer) parts.push_back("2P");
    if (coins) parts.push_back("Coins");
    if (completed) parts.push_back("Completed");
    if (uncompleted) parts.push_back("Uncompleted");
    if (song) parts.push_back("Song");
    if (!difficulties.empty()) parts.push_back("Difficulty");
    if (!lengths.empty()) parts.push_back("Length");
    if (demonFilter > 0) parts.push_back("Demon");
    if (parts.empty()) return "No filters";
    return geode::utils::string::join(parts, ", ");
}

void add(GJSearchObject* search, std::vector<int> difficulties, std::vector<int> lengths, int type) {
    if (!search) return;
    Entry obj;
    obj.time = std::time(nullptr);
    obj.type = type;
    obj.query = search->m_searchQuery;
    obj.difficulties = std::move(difficulties);
    obj.lengths = std::move(lengths);
    obj.uncompleted = search->m_uncompletedFilter;
    obj.completed = search->m_completedFilter;
    obj.featured = search->m_featuredFilter;
    obj.original = search->m_originalFilter;
    obj.twoPlayer = search->m_twoPlayerFilter;
    obj.coins = search->m_coinsFilter;
    obj.epic = search->m_epicFilter;
    obj.legendary = search->m_legendaryFilter;
    obj.mythic = search->m_mythicFilter;
    obj.song = search->m_songFilter;
    obj.customSong = search->m_customSongFilter;
    obj.songID = search->m_songID;
    obj.demonFilter = (int)search->m_demonFilter;
    obj.noStar = search->m_noStarFilter;
    obj.star = search->m_starFilter;

    std::erase(history, obj);
    history.insert(history.begin(), std::move(obj));
}

void remove(int index) {
    if (index >= 0 && index < (int)history.size()) {
        history.erase(history.begin() + index);
    }
}

void clear() {
    history.clear();
}

void load() {
    history = Mod::get()->getSavedValue<std::vector<Entry>>("search-history", {});
}

void save() {
    Mod::get()->setSavedValue<std::vector<Entry>>("search-history", history);
}

} // namespace paimon::searchhistory

$on_mod(Loaded) {
    paimon::searchhistory::load();
}

$on_mod(DataSaved) {
    paimon::searchhistory::save();
}

// matjson serialization

using paimon::searchhistory::Entry;

geode::Result<Entry> matjson::Serialize<Entry>::fromJson(const matjson::Value& v) {
    if (!v.isObject()) return geode::Err("Expected object");
    Entry o;
    o.time = v["time"].asInt().unwrapOr(0);
    o.type = (int)v["type"].asInt().unwrapOr(0);
    o.query = v["query"].asString().unwrapOr("");
    if (auto arr = v["difficulties"].asArray()) {
        for (auto& d : arr.unwrap()) o.difficulties.push_back((int)d.asInt().unwrapOr(0));
    }
    if (auto arr = v["lengths"].asArray()) {
        for (auto& l : arr.unwrap()) o.lengths.push_back((int)l.asInt().unwrapOr(0));
    }
    o.uncompleted = v["uncompleted"].asBool().unwrapOr(false);
    o.completed = v["completed"].asBool().unwrapOr(false);
    o.featured = v["featured"].asBool().unwrapOr(false);
    o.original = v["original"].asBool().unwrapOr(false);
    o.twoPlayer = v["two-player"].asBool().unwrapOr(false);
    o.coins = v["coins"].asBool().unwrapOr(false);
    o.epic = v["epic"].asBool().unwrapOr(false);
    o.legendary = v["legendary"].asBool().unwrapOr(false);
    o.mythic = v["mythic"].asBool().unwrapOr(false);
    o.song = v["song"].asBool().unwrapOr(false);
    o.customSong = v["custom-song"].asBool().unwrapOr(false);
    o.songID = (int)v["song-id"].asInt().unwrapOr(0);
    o.demonFilter = (int)v["demon-filter"].asInt().unwrapOr(0);
    o.noStar = v["no-star"].asBool().unwrapOr(false);
    o.star = v["star"].asBool().unwrapOr(false);
    return geode::Ok(std::move(o));
}

matjson::Value matjson::Serialize<Entry>::toJson(const Entry& o) {
    auto v = matjson::Value::object();
    v["time"] = o.time;
    v["type"] = o.type;
    if (!o.query.empty()) v["query"] = o.query;
    if (!o.difficulties.empty()) {
        auto a = matjson::Value::array();
        for (int d : o.difficulties) a.push(d);
        v["difficulties"] = a;
    }
    if (!o.lengths.empty()) {
        auto a = matjson::Value::array();
        for (int l : o.lengths) a.push(l);
        v["lengths"] = a;
    }
    if (o.uncompleted) v["uncompleted"] = true;
    if (o.completed) v["completed"] = true;
    if (o.featured) v["featured"] = true;
    if (o.original) v["original"] = true;
    if (o.twoPlayer) v["two-player"] = true;
    if (o.coins) v["coins"] = true;
    if (o.epic) v["epic"] = true;
    if (o.legendary) v["legendary"] = true;
    if (o.mythic) v["mythic"] = true;
    if (o.song) {
        v["song"] = true;
        if (o.customSong) v["custom-song"] = true;
        v["song-id"] = o.songID;
    }
    if (o.demonFilter != 0) v["demon-filter"] = o.demonFilter;
    if (o.noStar) v["no-star"] = true;
    if (o.star) v["star"] = true;
    return v;
}
