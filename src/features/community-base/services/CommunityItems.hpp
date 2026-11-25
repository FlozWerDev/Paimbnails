#pragma once

// what players send to paimon base and what paimon shop takes back from it:
// presets, icon gradients, custom more icons icons and comment backgrounds.

#include "CommunityClient.hpp"
#include "../../paimon-shop/data/ShopPreset.hpp"
#include "../../global-icon/GlobalIconTypes.hpp"
#include <Geode/Geode.hpp>
#include <functional>
#include <string>

namespace paimon::community::items {

using Done = std::function<void(bool ok, std::string const& message)>;

// gamemodes that have gradient and custom icon slots (cube..jetpack).
std::vector<IconType> const& gamemodes();
char const* gamemodeName(IconType type);
char const* typeName(std::string const& type);

// the local player has a saved gradient / wears a custom icon for this gamemode.
bool hasGradient(IconType type);
bool hasCustomIcon(IconType type);

void publishPreset(shop::Preset const& preset, Done done);
// the player's own gradient for `type` (player 1), with its png images.
void submitGradient(IconType type, std::string const& title, std::string const& description, Done done);
// the more icons icon the player wears for `type`, texture and sheet.
void submitIcon(IconType type, std::string const& title, std::string const& description, Done done);

// a shared gradient item as global icon metadata, so the gradient helpers can use it.
paimon::globalicon::GlobalIconMeta gradientMeta(matjson::Value const& item);
// `apply`: overwrite the player's slot for that gamemode; otherwise add it to the library.
void takeGradient(matjson::Value const& item, bool apply, Done done);

// a shared icon item as a global icon slot (urls of its texture and sheet).
paimon::globalicon::GlobalIconSlot iconSlot(matjson::Value const& item);
// registers it in more icons for this session (previews).
void previewIcon(matjson::Value const& item, std::function<void(std::string const& registeredName)> done);
// copies it into the more icons folder so it stays after a restart; `use` also wears it.
void takeIcon(matjson::Value const& item, bool use, Done done);

// downloads a preset item (configuration and preview) as a paimon shop preset.
void fetchPreset(matjson::Value const& item, std::function<void(geode::Result<shop::Preset>)> done);

} // namespace paimon::community::items
