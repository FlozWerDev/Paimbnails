#include "GradientCache.hpp"
#include "GradientUtils.hpp"
#include "services/GradientAnimationManager.hpp"

#include <Geode/loader/Event.hpp>

constexpr char const* kSeparate2PMigration = "icon-gradients-separate-2p-default-v2";

using namespace geode::prelude;
using namespace paimon::icon_gradients;

// Plain on/off settings mirrored straight into the cache snapshot.
struct SettingMirror {
    char const* key;
    void (*setter)(bool);
};

constexpr SettingMirror kSettingMirrors[] = {
    {kSettingDisable2P, &GradientCache::set2PDisabled},
    {kSettingFlip2P, &GradientCache::set2PFlip},
    {kSettingMenu, &GradientCache::setMenuGradientsEnabled},
};

$on_mod(Loaded) {

    if (!Mod::get()->getSavedValue<bool>(kSeparate2PMigration, false)) {
        Mod::get()->setSettingValue<bool>(kSettingSeparate2P, true);
        Mod::get()->setSavedValue<bool>(kSeparate2PMigration, true);
    }

    GradientUtils::migrateLegacyStorage();

    auto* mod = Mod::get();

    GradientCache::setModDisabled(!mod->getSettingValue<bool>(kSettingEnabled));
    for (auto& mirror : kSettingMirrors)
        mirror.setter(mod->getSettingValue<bool>(mirror.key));
    GradientCache::set2PSeparate(mod->getSettingValue<bool>(kSettingSeparate2P));
    GradientCache::get().m_increaseLineTolerance = mod->getSettingValue<bool>(kSettingIncreaseTolerance);

    listenForSettingChanges<bool>(kSettingIncreaseTolerance, [](bool value) {
        GradientCache::get().m_increaseLineTolerance = value;
    });

    listenForSettingChanges<bool>(kSettingEnabled, [](bool value) {
        GradientCache::setModDisabled(!value);
        GradientAnimationManager::get().refreshPrograms();
    });

    for (auto& mirror : kSettingMirrors) {
        listenForSettingChanges<bool>(mirror.key, [setter = mirror.setter](bool value) {
            setter(value);
        });
    }

    listenForSettingChanges<bool>(kSettingSeparate2P, [](bool value) {
        GradientCache::set2PSeparate(value);
        Mod::get()->setSavedValue<bool>(kSeparate2PMigration, true);
    });
}

GradientCache& GradientCache::get() {
    static GradientCache instance;
    return instance;
}

void GradientCache::setModDisabled(bool disabled) {
    get().m_disabled = disabled;
    set2PSeparate(Mod::get()->getSettingValue<bool>(kSettingSeparate2P));
}

bool GradientCache::isModDisabled() {
    return get().m_disabled;
}

void GradientCache::setMenuGradientsEnabled(bool enabled) {
    get().m_menuGradients = enabled;
}

bool GradientCache::isMenuGradientsEnabled() {
    return get().m_menuGradients;
}

void GradientCache::set2PDisabled(bool disabled) {
    get().m_p2disabled = disabled;
    set2PSeparate(Mod::get()->getSettingValue<bool>(kSettingSeparate2P));
}

bool GradientCache::is2PDisabled() {
    return get().m_p2disabled;
}

void GradientCache::set2PSeparate(bool separate) {
    get().m_p2separate = separate && !get().m_disabled && !get().m_p2disabled;
}

bool GradientCache::is2PSeparate() {
    return get().m_p2separate;
}

void GradientCache::set2PFlip(bool flip) {
    get().m_p2flip = flip;
}

bool GradientCache::is2PFlip() {
    return get().m_p2flip;
}

IconType GradientCache::getLastSelected() {
    return get().m_lastSelected;
}

void GradientCache::setLastSelected(IconType type) {
    get().m_lastSelected = type;
}

GradientConfig GradientCache::getCopiedConfig() {
    return get().m_copiedConfig;
}

void GradientCache::setCopiedConfig(GradientConfig config) {
    get().m_copiedConfig = config;
}
