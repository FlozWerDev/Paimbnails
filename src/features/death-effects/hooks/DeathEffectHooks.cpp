#include "../services/DeathEffectManager.hpp"
#include "../ui/DeathEffectPopup.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../gameplay-performance/GameplayPerformance.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace {

bool isDeathSound(gd::string const& path) {
    auto value = geode::utils::string::toLower(std::string(path));
    std::replace(value.begin(), value.end(), '\\', '/');
    auto slash = value.find_last_of('/');
    auto filename = slash == std::string::npos ? value : value.substr(slash + 1);
    return filename == "explode_11.ogg";
}

CCSprite* createDeathEffectsIcon() {
    auto icon = paimon::SpriteHelper::safeCreateWithFrameName("edit_eSFXBtn_001.png");
    if (!icon) {
        icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
    }
    if (!icon) return nullptr;

    auto circle = CircleButtonSprite::create(
        icon,
        CircleBaseColor::DarkAqua,
        CircleBaseSize::Medium
    );
    if (!circle) return nullptr;

    auto label = CCLabelBMFont::create("SFX", "bigFont.fnt");
    label->setScale(0.28f);
    label->setPosition({circle->getContentWidth() / 2.f, 8.f});
    circle->addChild(label, 2);
    return circle;
}

} // namespace

class $modify(PaimonDeathEffectsPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "PauseLayer::customSetup");
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        if (!paimon::modules::isEnabled("paimbnails.deatheffects.gameplay")) return;

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("left-button-menu"));
        if (!menu || menu->getChildByID("death-effects-button"_spr)) return;

        auto* sprite = createDeathEffectsIcon();
        if (!sprite) return;

        auto* button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(PaimonDeathEffectsPauseLayer::onDeathEffects)
        );
        if (!button) return;

        button->setID("death-effects-button"_spr);
        menu->addChild(button);
        menu->updateLayout();
    }

    void onDeathEffects(CCObject*) {
        if (auto* popup = paimon::death_effects::DeathEffectPopup::create()) {
            popup->show();
        }
    }
};

class $modify(PaimonDeathEffectsAudioEngine, FMODAudioEngine) {
    $override
    int playEffect(gd::string path, float speed, float unknown, float volume) {
        if (!paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kModVisualsModuleId) && isDeathSound(path) &&
            paimon::modules::isEnabled("paimbnails.deatheffects.gameplay") &&
            paimon::death_effects::DeathEffectManager::get().playDeath(
                this, speed, volume
            )) {
            return 1;
        }
        return FMODAudioEngine::playEffect(path, speed, unknown, volume);
    }
};

class $modify(PaimonDeathEffectsPlayLayer, PlayLayer) {
    $override
    void resetLevel() {
        if (!paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kModVisualsModuleId)) {
            paimon::death_effects::DeathEffectManager::get().handleLevelReset();
        }
        PlayLayer::resetLevel();
    }

    $override
    void onExit() {
        paimon::death_effects::DeathEffectManager::get().handleLevelExit();
        PlayLayer::onExit();
    }
};
