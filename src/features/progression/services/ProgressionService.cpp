#include "ProgressionService.hpp"
#include "../../versus/services/VersusStore.hpp"
#include "../data/ProgressionTiers.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include "../../../utils/MainThreadDelay.hpp"
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::progression {

namespace {

constexpr char const* kKeyExp        = "progression-exp";
constexpr char const* kKeyBadges     = "progression-badges";
constexpr char const* kKeyDemonInfo  = "progression-demon-info";
constexpr char const* kKeyStarsInfo  = "progression-stars-info";
constexpr char const* kKeyPlatInfo   = "progression-platformer-info";
constexpr char const* kKeyCreatorPts = "progression-creator-points";
constexpr char const* kKeyGlobalRank = "progression-global-rank";

std::vector<std::string> splitIds(std::string const& raw) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < raw.size()) {
        size_t const comma = raw.find(',', start);
        size_t const end = comma == std::string::npos ? raw.size() : comma;
        if (end > start) out.emplace_back(raw.substr(start, end - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

} // namespace

bool ProgressDelta::tierChanged() const {
    return tierIndexForLevel(toLevel) != tierIndexForLevel(fromLevel);
}

ProgressionService& ProgressionService::get() {
    static ProgressionService instance;
    return instance;
}

bool ProgressionService::enabled() const {
    return paimon::modules::isEnabled("paimbnails.progression.profile");
}

BadgeContext ProgressionService::ownContext() {
    auto stats = statsFromLocalSave();

    auto* mod = Mod::get();
    stats.creatorPoints = mod->getSavedValue<int>(kKeyCreatorPts, 0);
    stats.globalRank    = mod->getSavedValue<int>(kKeyGlobalRank, 0);

    auto const demonInfo = mod->getSavedValue<std::string>(kKeyDemonInfo, "");
    if (!demonInfo.empty()) {
        stats.demonInfo = parseDemonInfo(demonInfo, &stats.hasDemonInfo);
    }
    auto const starsInfo = mod->getSavedValue<std::string>(kKeyStarsInfo, "");
    if (!starsInfo.empty()) {
        stats.classicInfo = parseDifficultyInfo(starsInfo, &stats.hasClassicInfo);
    }
    auto const platInfo = mod->getSavedValue<std::string>(kKeyPlatInfo, "");
    if (!platInfo.empty()) {
        stats.platformerInfo = parseDifficultyInfo(platInfo, &stats.hasPlatformerInfo);
    }

    auto const& versus = versus::VersusStore::get();
    stats.versusExp = versus.versusExp();
    stats.versusWins = versus.profile(versus::Mode::Classic).wins
                     + versus.profile(versus::Mode::Platformer).wins;

    return makeContext(stats);
}

void ProgressionService::rememberOwnScore(GJUserScore* score) {
    if (!score) return;

    auto* mod = Mod::get();
    bool changed = false;

    if (mod->getSavedValue<int>(kKeyCreatorPts, 0) != score->m_creatorPoints) {
        mod->setSavedValue<int>(kKeyCreatorPts, score->m_creatorPoints);
        changed = true;
    }
    if (mod->getSavedValue<int>(kKeyGlobalRank, 0) != score->m_globalRank) {
        mod->setSavedValue<int>(kKeyGlobalRank, score->m_globalRank);
        changed = true;
    }

    // Empty field: server skipped it, keep the last good one.
    auto keep = [&](char const* key, std::string const& value) {
        if (value.empty()) return;
        if (mod->getSavedValue<std::string>(key, "") == value) return;
        mod->setSavedValue<std::string>(key, value);
        changed = true;
    };
    keep(kKeyDemonInfo, std::string(score->m_demonInfo));
    keep(kKeyStarsInfo, std::string(score->m_starsInfo));
    keep(kKeyPlatInfo, std::string(score->m_platformerInfo));

    // A fresh breakdown re-prices old demons; commit so the next gain skips it.
    if (changed && hasSnapshot()) commitSnapshot();
}

bool ProgressionService::hasSnapshot() const {
    return Mod::get()->getSavedValue<int64_t>(kKeyExp, -1) >= 0;
}

std::vector<std::string> ProgressionService::storedBadges() const {
    return splitIds(Mod::get()->getSavedValue<std::string>(kKeyBadges, ""));
}

void ProgressionService::storeBadges(BadgeContext const& ctx) {
    std::string joined;
    for (auto const& badge : allBadges()) {
        if (!isUnlocked(badge, ctx)) continue;
        if (!joined.empty()) joined.push_back(',');
        joined.append(badge.id);
    }
    Mod::get()->setSavedValue<std::string>(kKeyBadges, joined);
}

void ProgressionService::commitSnapshot() {
    auto const ctx = ownContext();
    Mod::get()->setSavedValue<int64_t>(kKeyExp, ctx.exp);
    storeBadges(ctx);
    paimon::requestDeferredModSave();
}

std::optional<ProgressDelta> ProgressionService::consumeDelta() {
    auto const ctx = ownContext();
    auto* mod = Mod::get();

    int64_t const previous = mod->getSavedValue<int64_t>(kKeyExp, -1);
    mod->setSavedValue<int64_t>(kKeyExp, ctx.exp);

    if (previous < 0) {
        storeBadges(ctx);
        log::info("[Progression] Snapshot primed at {} exp (level {})", ctx.exp, ctx.level);
        return std::nullopt;
    }
    if (ctx.exp <= previous) {
        storeBadges(ctx);
        return std::nullopt;
    }

    auto const before = storedBadges();
    std::unordered_set<std::string> seen(before.begin(), before.end());

    ProgressDelta delta;
    delta.gainedExp = ctx.exp - previous;
    delta.totalExp = ctx.exp;
    delta.fromLevel = levelForExp(previous);
    delta.toLevel = ctx.level;
    for (auto const& badge : allBadges()) {
        if (!isUnlocked(badge, ctx)) continue;
        if (seen.count(badge.id)) continue;
        delta.newBadges.push_back(&badge);
    }

    storeBadges(ctx);
    paimon::requestDeferredModSave();
    return delta;
}

} // namespace paimon::progression
