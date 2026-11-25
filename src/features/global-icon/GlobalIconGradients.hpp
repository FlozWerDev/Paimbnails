#pragma once

// icon gradients shared through a global icon: download the images their
// points use, turn the shared document into the local save format and paint
// another player's profile or copy them into the local player's slots.

#include "GlobalIconTypes.hpp"
#include "../icon-gradients/GradientTypes.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>

class ProfilePage;

namespace paimon::globalicon {

using GradientPaths = std::unordered_map<std::string, std::string>; // sha256 -> utf8 local path

// downloads every gradient image of `meta` into the global icon cache.
void ensureGradientImages(GlobalIconMeta const& meta, geode::CopyableFunction<void(GradientPaths const&)> cb);

// shared gradients with "asset:<sha>" replaced by local paths; points whose
// image is missing keep only their color.
matjson::Value localizeGradients(matjson::Value const& shared, GradientPaths const& paths);

// paints a profile that is not the local player's with their shared gradients.
void renderProfileGradients(ProfilePage* page);

// the gradient for `type` out of a localized document keyed by "icon-gradients-<type>".
paimon::icon_gradients::Gradient sharedGradient(matjson::Value const& localized, IconType type);

// copies shared gradients into the local player: `apply` overwrites the
// matching gradient slots, otherwise every color is added to the saved
// gradients library. images are copied to a folder that is never pruned.
// returns how many gradient slots or library entries were written.
int copyGradients(GlobalIconMeta const& meta, GradientPaths const& cachePaths, bool apply);

} // namespace paimon::globalicon
