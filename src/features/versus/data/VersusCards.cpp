#include "VersusCards.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

namespace paimon::versus {

namespace {

using R = Rarity;
using T = CardTarget;
using C = CardId;

constexpr std::array<CardDef, kCardCount> kCards = {{
    {C::Fog,        "fog",        "Fog",        R::Common,    T::Rival, 6.0f,  ModeAny},
    {C::Quake,      "quake",      "Quake",      R::Common,    T::Rival, 5.0f,  ModeAny},
    {C::Weight,     "weight",     "Ballast",    R::Common,    T::Rival, 10.0f, ModeAny},
    {C::Noise,      "noise",      "Static",     R::Common,    T::Rival, 8.0f,  ModeAny},
    {C::Eye,        "eye",        "Eye",        R::Common,    T::Self,  15.0f, ModeAny},
    {C::Dice,       "dice",       "Dice",       R::Common,    T::Both,  0.0f,  ModeAny},
    {C::Magnet,     "magnet",     "Blackout",   R::Common,    T::Both,  15.0f, ModeAny},
    {C::ZoomIn,     "zoomin",     "Close Up",   R::Rare,      T::Rival, 7.0f,  ModeAny},
    {C::ZoomOut,    "zoomout",    "Wide Shot",  R::Rare,      T::Rival, 7.0f,  ModeAny},
    {C::Mask,       "mask",       "Domino",     R::Rare,      T::Rival, 5.0f,  ModeAny},
    {C::Lock,       "lock",       "Padlock",    R::Rare,      T::Rival, 12.0f, ModeAny},
    {C::Checkpoint, "checkpoint", "Beacon",     R::Rare,      T::Self,  0.0f,  ModePlatformer},
    {C::Dispel,     "dispel",     "Purge",      R::Rare,      T::Self,  0.0f,  ModeAny},
    {C::Bolt,       "bolt",       "Spark",      R::Rare,      T::Self,  0.0f,  ModeAny},
    // Inverting inputs is unplayable in classic, so the mirror only flips the
    // camera; the read is scrambled without making the level impossible.
    {C::Mirror,     "mirror",     "Mirror",     R::Epic,      T::Rival, 5.0f,  ModeAny},
    {C::Freeze,     "freeze",     "Frost",      R::Epic,      T::Rival, 0.35f, ModeAny},
    {C::Shield,     "shield",     "Shield",     R::Epic,      T::Self,  0.0f,  ModeAny},
    {C::Heart,      "heart",      "Heart",      R::Epic,      T::Self,  0.0f,  ModeClassic},
    {C::Ghost,      "ghost",      "Wraith",     R::Epic,      T::Self,  10.0f, ModeAny},
    {C::Hourglass,  "hourglass",  "Hourglass",  R::Epic,      T::Both,  20.0f, ModeAny},
    {C::Reflect,    "reflect",    "Rebound",    R::Legendary, T::Self,  0.0f,  ModeAny},
    {C::Swap,       "swap",       "Swap",       R::Legendary, T::Both,  0.0f,  ModeAny},
    {C::Skull,      "skull",      "Skull",      R::Legendary, T::Rival, 0.0f,  ModePlatformer},
    {C::Bomb,       "bomb",       "Bomb",       R::Legendary, T::Rival, 4.0f,  ModeAny},
}};

} // namespace

std::array<CardDef, kCardCount> const& allCards() {
    return kCards;
}

CardDef const& cardAt(CardId id) {
    return kCards[std::min<size_t>(static_cast<size_t>(id), kCardCount - 1)];
}

std::vector<CardDef const*> cardsOfRarity(Rarity rarity) {
    std::vector<CardDef const*> out;
    for (auto const& def : kCards) {
        if (def.rarity == rarity) out.push_back(&def);
    }
    return out;
}

std::string cardTargetName(CardTarget target) {
    switch (target) {
        case CardTarget::Rival: return Localization::get().getString("versus.target.rival");
        case CardTarget::Self:  return Localization::get().getString("versus.target.self");
        case CardTarget::Both:  return Localization::get().getString("versus.target.both");
    }
    return {};
}

std::string rarityName(Rarity rarity) {
    return Localization::get().getString(std::string("versus.rarity.") + rarityId(rarity));
}

std::string cardEffectText(CardDef const& def) {
    auto body = Localization::get().getString(std::string("versus.card.") + def.key);
    auto line = cardTargetName(def.target) + " - " + body;
    if (def.duration > 0.f) {
        line += fmt::format(" - {:.2g}s", def.duration);
    }
    return line;
}

cocos2d::ccColor3B rarityBody(Rarity rarity) {
    switch (rarity) {
        case Rarity::Common:    return {150, 162, 185};
        case Rarity::Rare:      return { 58, 150, 214};
        case Rarity::Epic:      return {150,  96, 226};
        case Rarity::Legendary: return {238, 160,  40};
    }
    return {255, 255, 255};
}

cocos2d::ccColor3B rarityRim(Rarity rarity) {
    switch (rarity) {
        case Rarity::Common:    return {205, 214, 230};
        case Rarity::Rare:      return {150, 214, 255};
        case Rarity::Epic:      return {208, 168, 255};
        case Rarity::Legendary: return {255, 224, 150};
    }
    return {255, 255, 255};
}

std::string cardGlyphSprite(CardDef const& def) {
    return geode::Mod::get()->expandSpriteName(std::string("paim_vsCard_") + def.key + ".png");
}

std::string cardPlateSprite() {
    return geode::Mod::get()->expandSpriteName("paim_vsCard.png");
}

std::string cardRingSprite() {
    return geode::Mod::get()->expandSpriteName("paim_vsCardRing.png");
}

std::string cardBackSprite() {
    return geode::Mod::get()->expandSpriteName("paim_vsCardBack.png");
}

std::array<int, 4> rarityWeights(float deficit, bool catchUp) {
    std::array<int, 4> weights = {520, 300, 140, 40};
    if (!catchUp || deficit <= 0.f) return weights;

    // Full swing at a 25 point gap; past that it stops helping, or a blowout
    // would hand the loser a legendary every milestone.
    float const t = std::min(deficit / 25.f, 1.f) * 0.35f;
    int const moved = static_cast<int>(weights[0] * t);
    weights[0] -= moved;
    weights[1] += moved / 6;
    weights[2] += moved / 2;
    weights[3] += moved - moved / 6 - moved / 2;
    return weights;
}

} // namespace paimon::versus
