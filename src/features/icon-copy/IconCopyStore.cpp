#include "IconCopyStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GameManager.hpp>

#include "../../core/modules/ModuleRegistry.hpp"

#include <algorithm>
#include <chrono>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

constexpr char const* kSavedKey = "icon-copy-sets";
constexpr size_t kMaxEntries = 60;

std::vector<IconSet> g_sets;
bool g_loaded = false;

void ensureLoaded() {
    if (g_loaded) return;
    g_loaded = true;
    g_sets = Mod::get()->getSavedValue<std::vector<IconSet>>(kSavedKey, {});
}

void store() {
    Mod::get()->setSavedValue<std::vector<IconSet>>(kSavedKey, g_sets);
}

int64_t nowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// Two snapshots belong to the same user. Account ids win; names are the
// fallback for entries copied before the id was known.
bool sameUser(IconSet const& a, IconSet const& b) {
    if (a.accountID > 0 && b.accountID > 0) return a.accountID == b.accountID;
    return !a.username.empty() && a.username == b.username;
}

}  // anonymous namespace

int IconSet::iconFor(IconType type) const {
    switch (type) {
        case IconType::Ship: return ship;
        case IconType::Ball: return ball;
        case IconType::Ufo: return ufo;
        case IconType::Wave: return wave;
        case IconType::Robot: return robot;
        case IconType::Spider: return spider;
        case IconType::Swing: return swing;
        case IconType::Jetpack: return jetpack;
        case IconType::Special: return trail;
        case IconType::DeathEffect: return deathEffect;
        default: return cube;
    }
}

std::string IconSet::label() const {
    if (!username.empty()) return username;
    if (accountID > 0) return fmt::format("Account {}", accountID);
    return "Unknown";
}

bool enabled() {
    return modules::isEnabled("paimbnails.iconcopy.profile");
}

IconSet snapshot(GJUserScore* score) {
    IconSet set;
    if (!score) return set;

    set.username = score->m_userName;
    set.accountID = score->m_accountID;
    set.copiedAt = nowSeconds();
    set.cube = score->m_playerCube;
    set.ship = score->m_playerShip;
    set.ball = score->m_playerBall;
    set.ufo = score->m_playerUfo;
    set.wave = score->m_playerWave;
    set.robot = score->m_playerRobot;
    set.spider = score->m_playerSpider;
    set.swing = score->m_playerSwing;
    set.jetpack = score->m_playerJetpack;
    set.trail = score->m_playerStreak;
    set.deathEffect = score->m_playerExplosion;
    set.color1 = score->m_color1;
    set.color2 = score->m_color2;
    set.glowColor = score->m_color3;
    set.glow = score->m_glowEnabled;
    return set;
}

void apply(IconSet const& set) {
    auto* gm = GameManager::get();
    if (!gm) return;

    gm->setPlayerFrame(std::max(set.cube, 1));
    gm->setPlayerShip(std::max(set.ship, 1));
    gm->setPlayerBall(std::max(set.ball, 1));
    gm->setPlayerBird(std::max(set.ufo, 1));
    gm->setPlayerDart(std::max(set.wave, 1));
    gm->setPlayerRobot(std::max(set.robot, 1));
    gm->setPlayerSpider(std::max(set.spider, 1));
    gm->setPlayerSwing(std::max(set.swing, 1));
    gm->setPlayerJetpack(std::max(set.jetpack, 1));

    gm->setPlayerColor(set.color1);
    gm->setPlayerColor2(set.color2);
    gm->setPlayerGlow(set.glow);
    // -1 means the profile had no custom glow color; writing it would put an
    // out-of-palette index into the save, so leave ours alone in that case.
    if (set.glowColor >= 0) gm->setPlayerColor3(set.glowColor);

    // Trails don't travel in the profile response, so they usually stay at 0.
    if (set.trail > 0) gm->setPlayerStreak(set.trail);
    if (set.deathEffect > 0) {
        gm->setPlayerDeathEffect(set.deathEffect);
        gm->loadDeathEffect(set.deathEffect);
    }
}

std::vector<IconSet> const& entries() {
    ensureLoaded();
    return g_sets;
}

void add(IconSet set) {
    ensureLoaded();
    if (set.copiedAt == 0) set.copiedAt = nowSeconds();

    // One entry per user: copying again refreshes the old snapshot.
    auto const dupe = [&](IconSet const& other) { return sameUser(set, other); };
    g_sets.erase(std::remove_if(g_sets.begin(), g_sets.end(), dupe), g_sets.end());

    g_sets.insert(g_sets.begin(), std::move(set));
    if (g_sets.size() > kMaxEntries) g_sets.resize(kMaxEntries);
    store();
}

void removeUser(IconSet const& set) {
    ensureLoaded();
    auto const match = [&](IconSet const& other) { return sameUser(set, other); };
    auto const tail = std::remove_if(g_sets.begin(), g_sets.end(), match);
    if (tail == g_sets.end()) return;
    g_sets.erase(tail, g_sets.end());
    store();
}

void clear() {
    ensureLoaded();
    g_sets.clear();
    store();
}

}  // namespace paimon::iconcopy

using paimon::iconcopy::IconSet;

geode::Result<IconSet> matjson::Serialize<IconSet>::fromJson(matjson::Value const& value) {
    if (!value.isObject()) return geode::Err("Expected object");

    IconSet set;
    set.username = value["username"].asString().unwrapOr("");
    set.accountID = static_cast<int>(value["account-id"].asInt().unwrapOr(0));
    set.copiedAt = value["copied-at"].asInt().unwrapOr(0);
    set.cube = static_cast<int>(value["cube"].asInt().unwrapOr(1));
    set.ship = static_cast<int>(value["ship"].asInt().unwrapOr(1));
    set.ball = static_cast<int>(value["ball"].asInt().unwrapOr(1));
    set.ufo = static_cast<int>(value["ufo"].asInt().unwrapOr(1));
    set.wave = static_cast<int>(value["wave"].asInt().unwrapOr(1));
    set.robot = static_cast<int>(value["robot"].asInt().unwrapOr(1));
    set.spider = static_cast<int>(value["spider"].asInt().unwrapOr(1));
    set.swing = static_cast<int>(value["swing"].asInt().unwrapOr(1));
    set.jetpack = static_cast<int>(value["jetpack"].asInt().unwrapOr(1));
    set.trail = static_cast<int>(value["trail"].asInt().unwrapOr(0));
    set.deathEffect = static_cast<int>(value["death-effect"].asInt().unwrapOr(0));
    set.color1 = static_cast<int>(value["color1"].asInt().unwrapOr(0));
    set.color2 = static_cast<int>(value["color2"].asInt().unwrapOr(3));
    set.glowColor = static_cast<int>(value["glow-color"].asInt().unwrapOr(-1));
    set.glow = value["glow"].asBool().unwrapOr(false);
    return geode::Ok(std::move(set));
}

matjson::Value matjson::Serialize<IconSet>::toJson(IconSet const& set) {
    auto value = matjson::Value::object();
    value["username"] = set.username;
    value["account-id"] = set.accountID;
    value["copied-at"] = set.copiedAt;
    value["cube"] = set.cube;
    value["ship"] = set.ship;
    value["ball"] = set.ball;
    value["ufo"] = set.ufo;
    value["wave"] = set.wave;
    value["robot"] = set.robot;
    value["spider"] = set.spider;
    value["swing"] = set.swing;
    value["jetpack"] = set.jetpack;
    value["trail"] = set.trail;
    value["death-effect"] = set.deathEffect;
    value["color1"] = set.color1;
    value["color2"] = set.color2;
    value["glow-color"] = set.glowColor;
    value["glow"] = set.glow;
    return value;
}
