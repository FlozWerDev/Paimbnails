#pragma once

#include <Geode/Geode.hpp>

class GJGarageLayer;

namespace paimon::icon_maker::garage {

// Re-applies exact colors on the garage's main preview after GD re-tints it.
void onPlayerColorChanged(GJGarageLayer* layer);

// Total pages for `type` including appended Icon Maker icons at the end.
// Returns 0 when Icon Maker does not extend this tab (e.g. disabled, More Icons active,
// unsupported tab, or zero custom icons for this type).
int totalPagesFor(IconType type);

// Vanilla page count for `type` (36 icons per page).
int vanillaPagesFor(IconType type);

// Called inside GJGarageLayer::setupPage wrapper. Returns true if it handled
// the entire setupPage call (including calling GJGarageLayer::setupPage internally).
void injectCustomIconsOnPage(GJGarageLayer* layer, int requestedPage, IconType type,
                             bool isExtraCustomPage);

// Called before vanilla GJGarageLayer::onSelect so picking a vanilla icon un-equips
// any active custom icon for that gamemode.
void onVanillaIconSelected(GJGarageLayer* layer, cocos2d::CCObject* sender);

// Refresh garage preview & current icon bar page (e.g. after returning from Icon Maker).
void refreshGarage(GJGarageLayer* layer);

}  // namespace paimon::icon_maker::garage
