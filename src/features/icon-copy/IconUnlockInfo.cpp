#include "IconUnlockInfo.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/AchievementManager.hpp>
#include <Geode/binding/GJStoreItem.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameStatsManager.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

std::string lowered(std::string_view text) {
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Goal text, ordered longest-first so "secret coin" beats "coin".
struct StatArt {
    char const* needle;
    char const* frame;
};
constexpr StatArt kStatArt[] = {
    {"secret coin", "GJ_coinsIcon_001.png"},
    {"user coin", "GJ_coinsIcon2_001.png"},
    {"silver coin", "GJ_coinsIcon2_001.png"},
    {"moon", "GJ_moonsIcon_001.png"},
    {"star", "GJ_starsIcon_001.png"},
    {"diamond", "GJ_diamondsIcon_001.png"},
    {"orb", "currencyOrbIcon_001.png"},
    {"demon", "GJ_demonIcon_001.png"},
    {"shard", "bonusShardSmall_001.png"},
    {"key", "GJ_bigKey_001.png"},
    {"coin", "GJ_coinsIcon_001.png"},
    {"level", "GJ_completesIcon_001.png"},
    {"map pack", "GJ_completesIcon_001.png"},
    {"friend", "GJ_sFriendsIcon_001.png"},
    {"like", "GJ_likesIcon_001.png"},
};

UnlockRequirement parseRequirement(std::string const& sentence) {
    UnlockRequirement requirement;
    if (sentence.empty()) return requirement;

    auto const text = lowered(sentence);
    for (auto const& art : kStatArt) {
        if (text.find(art.needle) != std::string::npos) {
            requirement.sprite = art.frame;
            break;
        }
    }

// Parse the first number, ignoring commas.
    std::string digits;
    for (char c : sentence) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
        } else if (c == ',' && !digits.empty()) {
            continue;
        } else if (!digits.empty()) {
            break;
        }
    }
    if (!digits.empty() && digits.size() <= 9) {
        requirement.amount = std::stoi(digits);
    }
    return requirement;
}

char const* shopName(ShopType type) {
    switch (type) {
        case ShopType::Normal: return "The Shop";
        case ShopType::Secret: return "The Secret Shop";
        case ShopType::Community: return "Community Shop";
        case ShopType::Mechanic: return "The Mechanic";
        case ShopType::Diamond: return "The Diamond Shop";
        case ShopType::Paths: return "The Paths";
    }
    return "A shop";
}

// Shop currency and its balance key.
struct ShopCurrency {
    char const* frame;
    char const* name;
    char const* stat;
};

ShopCurrency shopCurrency(ShopType type) {
    switch (type) {
        case ShopType::Normal:
        case ShopType::Secret: return {"currencyOrbIcon_001.png", "orbs", "14"};
        default: return {"currencyDiamondIcon_001.png", "diamonds", "13"};
    }
}

// Walk GD's startup table; GJStoreItem lookup is inlined on Windows.
GJStoreItem* findStoreItem(int iconID, UnlockType unlock) {
    auto* stats = GameStatsManager::sharedState();
    if (!stats || !stats->m_storeItemArray) return nullptr;

    for (auto* object : CCArrayExt<CCObject*>(stats->m_storeItemArray)) {
        auto* item = typeinfo_cast<GJStoreItem*>(object);
        if (!item) continue;
        if (item->m_typeID.value() == iconID &&
            item->m_unlockType.value() == static_cast<int>(unlock)) {
            return item;
        }
    }
    return nullptr;
}

// Match achievement keys by meaning because GD changes their names.
struct AchievementText {
    std::string title;
    std::string goal;
};

AchievementText readAchievement(CCDictionary* dict) {
    AchievementText text;
    if (!dict) return text;

    auto* keys = dict->allKeys();
    if (!keys) return text;

    std::string spare;
    for (auto* object : CCArrayExt<CCObject*>(keys)) {
        auto* key = typeinfo_cast<CCString*>(object);
        if (!key) continue;
        auto* value = typeinfo_cast<CCString*>(dict->objectForKey(key->getCString()));
        if (!value) continue;

        auto const name = lowered(key->getCString());
        std::string const content = value->getCString();
        if (content.empty()) continue;

        if (name.find("title") != std::string::npos) {
            text.title = content;
        } else if (name.find("unachieved") != std::string::npos) {
            text.goal = content;
        } else if (name.find("desc") != std::string::npos && spare.empty()) {
            spare = content;
        }
    }

    if (text.goal.empty()) text.goal = spare;
    if (text.goal.empty()) {
// Unknown keys still show a title, but omit goal text and sprite.
        std::string names;
        for (auto* object : CCArrayExt<CCObject*>(keys)) {
            auto* key = typeinfo_cast<CCString*>(object);
            if (!key) continue;
            if (!names.empty()) names += ", ";
            names += key->getCString();
        }
        log::debug("no goal text in achievement dict, keys were: {}", names);
    }
    return text;
}

bool fillFromAchievement(UnlockInfo& info, int iconID, UnlockType unlock) {
    auto* achievements = AchievementManager::sharedState();
    if (!achievements) return false;

    auto const key = achievements->achievementForUnlock(iconID, unlock);
    if (key.empty()) return false;

    auto const text = readAchievement(achievements->getAchievementsWithID(key.c_str()));
    if (text.title.empty() && text.goal.empty()) return false;

    info.source = "Achievement";
    info.name = text.title.empty() ? "Achievement reward" : text.title;
    info.detail = text.goal;
    info.requirement = parseRequirement(text.goal);
    info.progress = std::clamp(achievements->percentForAchievement(key.c_str()), 0, 100);
    return true;
}

bool fillFromShop(UnlockInfo& info, int iconID, UnlockType unlock) {
    auto* item = findStoreItem(iconID, unlock);
    if (!item) return false;

    auto const currency = shopCurrency(item->m_shopType);
    int const price = item->m_price.value();

    info.source = shopName(item->m_shopType);
    info.name = "Store purchase";
    info.detail = fmt::format("Purchase this icon from {}.", info.source);
    info.requirement = {currency.frame, price};

// Shop progress is the amount of its price currently owned.
    auto* stats = GameStatsManager::sharedState();
    if (!stats || price <= 0) return true;

    int const balance = stats->getStat(currency.stat);
    info.progress = static_cast<int>(
        std::clamp(static_cast<long long>(balance) * 100 / price, 0LL, 100LL));
    info.hint = balance >= price
        ? fmt::format("You have {} {}, enough to buy it now.", balance, currency.name)
        : fmt::format("You have {} of the {} {} it costs.", balance, price, currency.name);
    return true;
}

// Handle chests, the vault, and other one-off unlocks.
bool fillFromSpecial(UnlockInfo& info, int iconID, UnlockType unlock) {
    auto* stats = GameStatsManager::sharedState();
    if (!stats) return false;

    auto const description = stats->getSpecialUnlockDescription(iconID, unlock, false);
    if (description.empty()) return false;

    info.source = "Secret";
    info.name = "Special unlock";
    info.detail = description;
    info.requirement = parseRequirement(info.detail);
    return true;
}

}

UnlockInfo unlockInfoFor(int iconID, IconType type) {
    UnlockInfo info;

    auto* manager = GameManager::get();
    if (!manager) {
        info.source = "Unknown";
        info.name = "Unlock information unavailable";
        info.detail = "The game is not ready to answer for this one";
        return info;
    }

    auto const unlock = manager->iconTypeToUnlockType(type);

// Combine the store/chest table with GameManager's achievement and milestone
// unlock records; GameStatsManager alone misses the latter.
    info.owned = manager->isIconUnlocked(iconID, type);
    info.equipped = manager->activeIconForType(type) == iconID;
    info.total = manager->countForType(type);

// Default starter icons are not recorded in either table.
    if (iconID < (type == IconType::Cube ? 5 : 2)) {
        info.source = "Starter";
        info.name = "Starter icon";
        info.detail = "Everyone has this one from the first launch";
        info.progress = 100;
        return info;
    }

    if (!fillFromAchievement(info, iconID, unlock) &&
        !fillFromShop(info, iconID, unlock) &&
        !fillFromSpecial(info, iconID, unlock)) {
        info.source = "Unknown";
        info.name = "Unlock source unavailable";
        info.detail = "The game lists no source for this one";
    }

// Ownership settles the question even if achievement progress or orb balance
// no longer matches the original unlock route.
    if (info.owned) info.progress = 100;
    return info;
}

}
