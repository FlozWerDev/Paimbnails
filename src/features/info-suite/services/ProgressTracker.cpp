#include "ProgressTracker.hpp"
#include "../InfoModule.hpp"
#include "../../../utils/JsonHelper.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <ctime>
#include <fstream>

using namespace geode::prelude;

namespace paimon::info {

namespace {

std::filesystem::path progressPath() {
    return Mod::get()->getSaveDir() / "info_progress.json";
}

int64_t nowSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

int64_t asInt(matjson::Value const& value) {
    auto res = value.asInt();
    return res.isOk() ? res.unwrap() : 0;
}

int maxLevels() {
    auto value = static_cast<int>(moduleSetting<int64_t>("info-progress-max-levels", 500));
    return std::clamp(value, 50, 5000);
}

// Buckets are stored as a flat "percent:count" list so an untouched level costs
// nothing; writing 101 zeroes per level would bloat the file for no reason.
matjson::Value bucketsToJson(std::array<uint32_t, kPercentBuckets> const& buckets) {
    auto list = matjson::Value::array();
    for (int i = 0; i < kPercentBuckets; i++) {
        if (buckets[i] == 0) continue;
        auto entry = matjson::Value::array();
        entry.push(i);
        entry.push(static_cast<int64_t>(buckets[i]));
        list.push(entry);
    }
    return list;
}

void bucketsFromJson(matjson::Value const& value, std::array<uint32_t, kPercentBuckets>& out) {
    paimon::json::forEachInArray(value, [&](matjson::Value const& entry) {
        auto pair = paimon::json::arrayOrEmpty(entry);
        if (pair.size() != 2) return;
        auto percent = static_cast<int>(asInt(pair[0]));
        if (percent < 0 || percent >= kPercentBuckets) return;
        out[percent] = static_cast<uint32_t>(std::max<int64_t>(0, asInt(pair[1])));
    });
}

// Runs are triplets [jumps, percent, practice] for the same reason as buckets:
// one short array instead of an object per attempt.
matjson::Value runsToJson(std::vector<RunRecord> const& runs) {
    auto list = matjson::Value::array();
    for (auto const& run : runs) {
        auto entry = matjson::Value::array();
        entry.push(static_cast<int64_t>(run.jumps));
        entry.push(static_cast<int64_t>(run.percent));
        entry.push(run.practice ? 1 : 0);
        list.push(entry);
    }
    return list;
}

void runsFromJson(matjson::Value const& value, std::vector<RunRecord>& out) {
    paimon::json::forEachInArray(value, [&](matjson::Value const& entry) {
        auto triplet = paimon::json::arrayOrEmpty(entry);
        if (triplet.size() != 3) return;
        RunRecord run;
        run.jumps = static_cast<uint16_t>(std::clamp<int64_t>(asInt(triplet[0]), 0, UINT16_MAX));
        run.percent = static_cast<uint8_t>(std::clamp<int64_t>(asInt(triplet[1]), 0, 100));
        run.practice = asInt(triplet[2]) != 0;
        out.push_back(run);
    });
    if (out.size() > static_cast<size_t>(kMaxRuns)) out.erase(out.begin(), out.end() - kMaxRuns);
}

} // namespace

int LevelProgress::totalDeaths(bool practice) const {
    auto const& buckets = practice ? deathsPractice : deathsNormal;
    int64_t total = 0;
    for (auto count : buckets) total += count;
    return static_cast<int>(std::min<int64_t>(total, INT32_MAX));
}

DeathPeak LevelProgress::worstDeath(bool practice) const {
    auto const& buckets = practice ? deathsPractice : deathsNormal;
    DeathPeak peak;
    for (int i = 0; i < kPercentBuckets; i++) {
        if (buckets[i] > peak.count) {
            peak.count = buckets[i];
            peak.percent = i;
        }
    }
    return peak;
}

std::vector<RunRecord> LevelProgress::recentRuns(bool practice) const {
    std::vector<RunRecord> out;
    out.reserve(runs.size());
    for (auto const& run : runs) {
        if (run.practice == practice) out.push_back(run);
    }
    return out;
}

ProgressTracker& ProgressTracker::get() {
    static ProgressTracker instance;
    return instance;
}

void ProgressTracker::load() {
    auto path = progressPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();
    if (contents.empty()) return;

    auto parsed = matjson::parse(contents);
    if (!parsed.isOk()) {
        log::warn("[Paimbnails] info_progress.json corrupto, se empieza de cero");
        return;
    }

    auto root = parsed.unwrap();
    if (!root["levels"].isObject()) return;

    // Object entries carry their own key; see InfoStore::load for the same shape.
    for (auto const& value : root["levels"]) {
        auto key = value.getKey();
        if (!key) continue;
        auto id = geode::utils::numFromString<int>(*key);
        if (!id.isOk()) continue;

        LevelProgress progress;
        progress.attempts = static_cast<int>(asInt(value["attempts"]));
        progress.practiceAttempts = static_cast<int>(asInt(value["practiceAttempts"]));
        progress.completions = static_cast<int>(asInt(value["completions"]));
        progress.bestNormal = static_cast<int>(asInt(value["bestNormal"]));
        progress.bestPractice = static_cast<int>(asInt(value["bestPractice"]));
        progress.jumpsNormal = static_cast<int>(asInt(value["jumpsNormal"]));
        progress.jumpsPractice = static_cast<int>(asInt(value["jumpsPractice"]));
        progress.playSeconds = asInt(value["playSeconds"]);
        progress.lastPlayed = asInt(value["lastPlayed"]);
        bucketsFromJson(value["deaths"], progress.deathsNormal);
        bucketsFromJson(value["deathsPractice"], progress.deathsPractice);
        runsFromJson(value["runs"], progress.runs);

        m_levels[id.unwrap()] = progress;
    }
}

void ProgressTracker::save() {
    if (!m_dirty) return;

    auto levels = matjson::Value::object();
    for (auto const& [id, progress] : m_levels) {
        auto item = matjson::Value::object();
        item["attempts"] = progress.attempts;
        item["practiceAttempts"] = progress.practiceAttempts;
        item["completions"] = progress.completions;
        item["bestNormal"] = progress.bestNormal;
        item["bestPractice"] = progress.bestPractice;
        item["jumpsNormal"] = progress.jumpsNormal;
        item["jumpsPractice"] = progress.jumpsPractice;
        item["playSeconds"] = progress.playSeconds;
        item["lastPlayed"] = progress.lastPlayed;
        item["deaths"] = bucketsToJson(progress.deathsNormal);
        item["deathsPractice"] = bucketsToJson(progress.deathsPractice);
        item["runs"] = runsToJson(progress.runs);
        levels[std::to_string(id)] = item;
    }

    auto root = matjson::Value::object();
    root["levels"] = levels;

    std::ofstream file(progressPath(), std::ios::trunc);
    if (!file.is_open()) {
        log::warn("[Paimbnails] no se pudo escribir info_progress.json");
        return;
    }
    file << root.dump(matjson::NO_INDENTATION);
    file.close();
    m_dirty = false;
}

LevelProgress& ProgressTracker::touch(int levelID) {
    auto& progress = m_levels[levelID];
    progress.lastPlayed = nowSeconds();
    m_dirty = true;
    return progress;
}

void ProgressTracker::enforceLimit() {
    int limit = maxLevels();
    if (static_cast<int>(m_levels.size()) <= limit) return;

    // Drop the least recently played levels first.
    std::vector<std::pair<int64_t, int>> byAge;
    byAge.reserve(m_levels.size());
    for (auto const& [id, progress] : m_levels) byAge.emplace_back(progress.lastPlayed, id);
    std::sort(byAge.begin(), byAge.end());

    int toRemove = static_cast<int>(m_levels.size()) - limit;
    for (int i = 0; i < toRemove; i++) m_levels.erase(byAge[i].second);
    m_dirty = true;
}

void ProgressTracker::recordDeath(int levelID, int percent, bool practice) {
    if (levelID <= 0) return;
    percent = std::clamp(percent, 0, kPercentBuckets - 1);

    auto& progress = touch(levelID);
    auto& buckets = practice ? progress.deathsPractice : progress.deathsNormal;
    if (buckets[percent] < UINT32_MAX) buckets[percent]++;
    enforceLimit();
}

void ProgressTracker::recordAttempt(int levelID, bool practice) {
    if (levelID <= 0) return;
    auto& progress = touch(levelID);
    if (practice) progress.practiceAttempts++;
    else progress.attempts++;
    enforceLimit();
}

void ProgressTracker::recordCompletion(int levelID, bool practice) {
    if (levelID <= 0 || practice) return;
    auto& progress = touch(levelID);
    progress.completions++;
    progress.bestNormal = 100;
}

void ProgressTracker::recordPlayTime(int levelID, int64_t seconds) {
    if (levelID <= 0 || seconds <= 0) return;
    touch(levelID).playSeconds += seconds;
}

void ProgressTracker::recordBest(int levelID, int percent, bool practice) {
    if (levelID <= 0 || percent <= 0) return;
    auto& progress = touch(levelID);
    auto& best = practice ? progress.bestPractice : progress.bestNormal;
    best = std::max(best, std::min(percent, 100));
}

void ProgressTracker::recordJump(int levelID, bool practice) {
    if (levelID <= 0) return;
    auto& progress = touch(levelID);
    if (practice) progress.jumpsPractice++;
    else progress.jumpsNormal++;
}

void ProgressTracker::recordRun(int levelID, int jumps, int percent, bool practice) {
    if (levelID <= 0 || jumps < 0) return;

    RunRecord run;
    run.jumps = static_cast<uint16_t>(std::min(jumps, static_cast<int>(UINT16_MAX)));
    run.percent = static_cast<uint8_t>(std::clamp(percent, 0, 100));
    run.practice = practice;

    auto& progress = touch(levelID);
    progress.runs.push_back(run);
    if (progress.runs.size() > static_cast<size_t>(kMaxRuns)) {
        progress.runs.erase(progress.runs.begin());
    }
}

LevelProgress const* ProgressTracker::find(int levelID) const {
    auto it = m_levels.find(levelID);
    return it == m_levels.end() ? nullptr : &it->second;
}

bool ProgressTracker::hasData(int levelID) const {
    auto const* progress = find(levelID);
    if (!progress) return false;
    return progress->attempts > 0 || progress->practiceAttempts > 0
        || progress->jumpsNormal > 0 || progress->jumpsPractice > 0
        || progress->totalDeaths(false) > 0 || progress->totalDeaths(true) > 0;
}

} // namespace paimon::info
