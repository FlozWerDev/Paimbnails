#pragma once

#include "VersusTypes.hpp"

#include <Geode/cocos/include/ccTypes.h>
#include <array>
#include <string>
#include <vector>

namespace paimon::versus {

// cards affect presentation and duel state while preserving physics and the state synchronized by globed.
struct CardDef {
    CardId id;
    char const* key;      // sprite suffix and wire name
    char const* name;
    Rarity rarity;
    CardTarget target;
    float duration;       // seconds; 0 means instant or until consumed
    uint8_t modes;
};

inline constexpr size_t kCardCount = static_cast<size_t>(CardId::Count);

// two in hand. a third one pushes the oldest out with a second of warning, so
// holding a legendary through a hard section is an actual decision.
inline constexpr int kHandSize = 2;

std::array<CardDef, kCardCount> const& allCards();
CardDef const& cardAt(CardId id);

std::vector<CardDef const*> cardsOfRarity(Rarity rarity);

// localized one-liner, e.g. "rival - fog top and bottom - 6s".
std::string cardEffectText(CardDef const& def);
std::string cardTargetName(CardTarget target);
std::string rarityName(Rarity rarity);

cocos2d::ccColor3B rarityBody(Rarity rarity);
cocos2d::ccColor3B rarityRim(Rarity rarity);

// sprite frame names, already expanded with the mod id.
std::string cardGlyphSprite(CardDef const& def);
std::string cardPlateSprite();
std::string cardRingSprite();
std::string cardBackSprite();

// rarity weights in tenths of a percent, shifted toward the player who is
// behind. `deficit` is how far back they are in percent points, 0 when ahead.
std::array<int, 4> rarityWeights(float deficit, bool catchUp);

} // namespace paimon::versus
