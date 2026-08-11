#include "IconPresetStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>

#include <algorithm>
#include <chrono>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

constexpr char const* kSavedKey = "icon-copy-presets";

std::vector<IconPreset> g_presets;
bool g_loaded = false;
uint32_t g_nextID = 1;

void ensureLoaded() {
    if (g_loaded) return;
    g_loaded = true;
    g_presets = Mod::get()->getSavedValue<std::vector<IconPreset>>(kSavedKey, {});
    for (auto const& preset : g_presets) g_nextID = std::max(g_nextID, preset.id + 1);
}

void store() {
    Mod::get()->setSavedValue<std::vector<IconPreset>>(kSavedKey, g_presets);
}

int64_t nowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string trimmed(std::string text) {
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

IconPreset* find(uint32_t id) {
    auto const spot = std::find_if(g_presets.begin(), g_presets.end(),
                                   [id](IconPreset const& preset) { return preset.id == id; });
    return spot == g_presets.end() ? nullptr : &*spot;
}

}  // anonymous namespace

IconSet currentPlayerSet() {
    IconSet set;
    auto* gm = GameManager::get();
    if (!gm) return set;

    set.copiedAt = nowSeconds();
    set.cube = gm->getPlayerFrame();
    set.ship = gm->getPlayerShip();
    set.ball = gm->getPlayerBall();
    set.ufo = gm->getPlayerBird();
    set.wave = gm->getPlayerDart();
    set.robot = gm->getPlayerRobot();
    set.spider = gm->getPlayerSpider();
    set.swing = gm->getPlayerSwing();
    set.jetpack = gm->getPlayerJetpack();
    set.trail = gm->getPlayerStreak();
    set.deathEffect = gm->getPlayerDeathEffect();
    set.color1 = gm->getPlayerColor();
    set.color2 = gm->getPlayerColor2();
    set.glowColor = gm->getPlayerGlowColor();
    set.glow = gm->getPlayerGlow();
    return set;
}

std::vector<IconPreset> const& presets() {
    ensureLoaded();
    return g_presets;
}

uint32_t addPreset(std::string name, IconSet set) {
    ensureLoaded();
    if (g_presets.size() >= kMaxPresets) return 0;

    IconPreset preset;
    preset.id = g_nextID++;
    preset.savedAt = nowSeconds();
    preset.set = std::move(set);
    preset.set.username = trimmed(std::move(name));
    preset.set.copiedAt = preset.savedAt;
    // Nobody's snapshot, so the row keeps its profile button to itself.
    preset.set.accountID = 0;

    uint32_t const id = preset.id;
    g_presets.insert(g_presets.begin(), std::move(preset));
    store();
    return id;
}

void renamePreset(uint32_t id, std::string name) {
    ensureLoaded();
    auto* preset = find(id);
    if (!preset) return;

    auto clean = trimmed(std::move(name));
    if (clean.empty() || clean == preset->set.username) return;
    preset->set.username = std::move(clean);
    store();
}

void removePreset(uint32_t id) {
    ensureLoaded();
    auto const match = [id](IconPreset const& preset) { return preset.id == id; };
    auto const tail = std::remove_if(g_presets.begin(), g_presets.end(), match);
    if (tail == g_presets.end()) return;
    g_presets.erase(tail, g_presets.end());
    store();
}

std::string suggestPresetName() {
    ensureLoaded();
    for (size_t number = 1; number <= kMaxPresets + 1; ++number) {
        auto candidate = fmt::format("Style {}", number);
        auto const taken = [&](IconPreset const& preset) { return preset.name() == candidate; };
        if (std::none_of(g_presets.begin(), g_presets.end(), taken)) return candidate;
    }
    return "Style";
}

}  // namespace paimon::iconcopy

using paimon::iconcopy::IconPreset;
using paimon::iconcopy::IconSet;

geode::Result<IconPreset> matjson::Serialize<IconPreset>::fromJson(matjson::Value const& value) {
    if (!value.isObject()) return geode::Err("Expected object");

    IconPreset preset;
    preset.id = static_cast<uint32_t>(value["id"].asInt().unwrapOr(0));
    preset.savedAt = value["saved-at"].asInt().unwrapOr(0);

    auto icons = value["icons"].as<IconSet>();
    if (icons.isErr()) return geode::Err(icons.unwrapErr());
    preset.set = icons.unwrap();
    return geode::Ok(std::move(preset));
}

matjson::Value matjson::Serialize<IconPreset>::toJson(IconPreset const& preset) {
    auto value = matjson::Value::object();
    value["id"] = preset.id;
    value["saved-at"] = preset.savedAt;
    value["icons"] = matjson::Value(preset.set);
    return value;
}
