#include "MoreIconsBridge.hpp"

#include "../../../framework/compat/ModCompat.hpp"

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

// File + MoreIcons quality flag for the current texture quality.
void pickQuality(CompiledIcon const& compiled,
                 std::filesystem::path& outPng, std::filesystem::path& outPlist,
                 cocos2d::TextureQuality& outQuality) {
    float factor = CCDirector::sharedDirector()->getContentScaleFactor();
    if (factor >= 4.f) {
        outPng = compiled.uhd.png;
        outPlist = compiled.uhd.plist;
        outQuality = cocos2d::kTextureQualityHigh;
    } else if (factor >= 2.f) {
        outPng = compiled.hd.png;
        outPlist = compiled.hd.plist;
        outQuality = cocos2d::kTextureQualityMedium;
    } else {
        outPng = compiled.sd.png;
        outPlist = compiled.sd.plist;
        outQuality = cocos2d::kTextureQualityLow;
    }
}

}  // anonymous namespace

bool MoreIconsBridge::available() {
    return paimon::compat::ModCompat::isMoreIconsLoaded();
}

std::string MoreIconsBridge::registeredName(std::string_view slotId) {
    return "paimbicon-" + std::string(slotId);
}

geode::Result<> MoreIconsBridge::registerIcon(IconProject const& project,
                                              CompiledIcon const& compiled) {
    if (!available()) return Err("More Icons no esta instalado");

    std::filesystem::path png, plist;
    cocos2d::TextureQuality quality = cocos2d::kTextureQualityHigh;
    pickQuality(compiled, png, plist, quality);

    auto regName = registeredName(project.id);

    // Wrap external icon edits in pre/refresh (API requirement).
    more_icons::preRefreshIcons();
    auto* existing = more_icons::getIcon(regName, project.type);
    if (existing) {
        more_icons::updateIcon(existing);
        more_icons::refreshIcons();
        return Ok();
    }
    auto* info = more_icons::addIcon(
        regName, project.name, project.type, png, plist,
        quality, "flozwer.paimbnails2", "Paimbnails");
    more_icons::refreshIcons();

    if (!info) {
        return Err("More Icons rechazo el icono (addIcon fallo)");
    }
    log::info("[icon-maker] icono '{}' registrado en More Icons", regName);
    return Ok();
}

bool MoreIconsBridge::applyIcon(IconProject const& project) {
    if (!available()) return false;
    auto* info = more_icons::getIcon(registeredName(project.id), project.type);
    if (!info) return false;
    more_icons::setIcon(info, project.type);
    return true;
}

void MoreIconsBridge::clearIcon(IconType type, std::string_view slotId) {
    if (!available()) return;
    auto* active = more_icons::activeIcon(type);
    if (active && active->getName() == registeredName(slotId)) {
        more_icons::setIcon(nullptr, type);
    }
}

bool MoreIconsBridge::hasForeignActive(IconType type) {
    if (!available()) return false;
    auto* active = more_icons::activeIcon(type);
    if (!active) return false;
    return active->getName().rfind("paimbicon-", 0) != 0;
}

bool MoreIconsBridge::isOurActive(IconType type) {
    return !activeOursSlotId(type).empty();
}

std::string MoreIconsBridge::activeOursSlotId(IconType type) {
    if (!available()) return {};
    auto* active = more_icons::activeIcon(type);
    if (!active) return {};
    auto const& name = active->getName();
    constexpr std::string_view kPrefix = "paimbicon-";
    if (name.rfind(kPrefix, 0) != 0) return {};
    return name.substr(kPrefix.size());
}

}  // namespace paimon::icon_maker
