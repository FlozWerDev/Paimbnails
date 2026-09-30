#include "VersusModes.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

namespace paimon::versus {

namespace {

using F = Format;

constexpr std::array<FormatDef, kFormatCount> kFormats = {{
    {F::Race,          "race",       "Race",            ModeAny,        360, 0,  true,  false},
    {F::SuddenDeath,   "sudden",     "Sudden Death",    ModeAny,        180, 1,  true,  false},
    {F::Attempts,      "attempts",   "Best Attempt",    ModeClassic,    240, 10, true,  false},
    {F::TimeAttack,    "timeattack", "Time Attack",     ModePlatformer, 300, 0,  true,  false},
    {F::Ladder,        "ladder",     "Ladder",          ModeAny,        420, 0,  true,  false},
    {F::Roulette,      "roulette",   "Roulette",        ModeAny,        360, 0,  true,  true},
    {F::TugOfWar,      "tug",        "Tug of War",      ModeAny,        360, 0,  true,  false},
    {F::KingOfTheHill, "king",       "King of the Hill",ModeAny,        420, 0,  true,  false},
    {F::Relay,         "relay",      "Relay",           ModePlatformer, 480, 0,  true,  false},
    {F::Friendly,      "friendly",   "Friendly",        ModeAny,        0,   0,  false, false},
}};

} // namespace

char const* modeId(Mode mode) {
    return mode == Mode::Platformer ? "platformer" : "classic";
}

Mode modeFromId(std::string const& id) {
    return id == "platformer" ? Mode::Platformer : Mode::Classic;
}

char const* formatId(Format format) {
    return formatAt(format).key;
}

Format formatFromId(std::string const& id) {
    if (auto const* def = findFormat(id)) return def->id;
    return Format::Race;
}

char const* rarityId(Rarity rarity) {
    switch (rarity) {
        case Rarity::Common:    return "common";
        case Rarity::Rare:      return "rare";
        case Rarity::Epic:      return "epic";
        case Rarity::Legendary: return "legendary";
    }
    return "common";
}

std::array<FormatDef, kFormatCount> const& allFormats() {
    return kFormats;
}

FormatDef const& formatAt(Format format) {
    return kFormats[std::min<size_t>(static_cast<size_t>(format), kFormatCount - 1)];
}

FormatDef const* findFormat(std::string const& key) {
    for (auto const& def : kFormats) {
        if (key == def.key) return &def;
    }
    return nullptr;
}

std::vector<FormatDef const*> rankedFormats(Mode mode) {
    uint8_t const bit = mode == Mode::Platformer ? ModePlatformer : ModeClassic;
    std::vector<FormatDef const*> out;
    for (auto const& def : kFormats) {
        if (def.ranked && (def.modes & bit)) out.push_back(&def);
    }
    return out;
}

// the deck is the whole of roulette; with the cards module off it would be a
// race under another name, so it leaves the queue instead.
std::vector<FormatDef const*> queueableFormats(Mode mode) {
    auto formats = rankedFormats(mode);
    if (paimon::modules::isEnabled("paimbnails.versus.cards")) return formats;
    std::erase_if(formats, [](FormatDef const* def) { return def->cards; });
    return formats;
}

std::vector<FormatDef const*> friendlyFormats(Mode mode) {
    auto formats = queueableFormats(mode);
    formats.push_back(&formatAt(Format::Friendly));
    return formats;
}

std::string formatName(FormatDef const& def) {
    return Localization::get().getString(std::string("versus.format.") + def.key);
}

std::string formatWinCondition(FormatDef const& def) {
    return Localization::get().getString(std::string("versus.win.") + def.key);
}

std::string formatSprite(FormatDef const& def) {
    // friendly has no glyph of its own; it borrows the emblem.
    if (def.id == Format::Friendly) {
        return geode::Mod::get()->expandSpriteName("paim_vsSwords.png");
    }
    return geode::Mod::get()->expandSpriteName(std::string("paim_vsMode_") + def.key + ".png");
}

int segmentForPercent(float percent) {
    if (percent >= 100.f) return 3;
    if (percent >= 75.f) return 2;
    if (percent >= 50.f) return 1;
    if (percent >= 25.f) return 0;
    return -1;
}

} // namespace paimon::versus
