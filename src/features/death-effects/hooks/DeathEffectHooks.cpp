#include "../services/DeathEffectManager.hpp"
#include "../ui/DeathEffectPopup.hpp"
#include "../visuals/DeathAnimation.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/PauseMenuNodes.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace {

bool s_deathVfxFiredThisAttempt = false;

bool isDeathSound(std::string const& path) {
    auto value = geode::utils::string::toLower(std::string(path));
    std::replace(value.begin(), value.end(), '\\', '/');
    auto slash = value.find_last_of('/');
    auto filename = slash == std::string::npos ? value : value.substr(slash + 1);
    return filename == "explode_11.ogg";
}

// explode_11 is also the menu blast and the cursor click effect, so the swap
// only belongs where the player can actually die.
bool inGameplay() {
    return PlayLayer::get() != nullptr || LevelEditorLayer::get() != nullptr;
}

CCSprite* createDeathVfxIcon() {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
    if (!icon) return nullptr;

    return CircleButtonSprite::create(
        icon,
        CircleBaseColor::DarkPurple,
        CircleBaseSize::Medium
    );
}

CCSprite* createDeathEffectsIcon() {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("edit_eSFXBtn_001.png");
    if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
    if (!icon) return nullptr;

    return CircleButtonSprite::create(
        icon,
        CircleBaseColor::DarkAqua,
        CircleBaseSize::Medium
    );
}

} // namespace

class $modify(PaimonDeathEffectsPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "PauseLayer::customSetup");
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        if (!paimon::modules::isEnabled("paimbnails.deatheffects.gameplay")) return;

        auto* menu = paimon::findPauseButtonMenu(this, "left-button-menu");
        if (!menu) menu = paimon::findPauseButtonMenu(this, "right-button-menu");
        if (!menu) {
            log::warn("[DeathEffects] No suitable menu found in PauseLayer");
            return;
        }

        if (!this->getChildByIDRecursive("death-animation-button"_spr)) {
            if (auto* vfxSpr = createDeathVfxIcon()) {
                auto* item = CCMenuItemSpriteExtra::create(
                    vfxSpr, this,
                    menu_selector(PaimonDeathEffectsPauseLayer::onDeathAnimation)
                );
                if (item) {
                    item->setID("death-animation-button"_spr);
                    menu->addChild(item);
                }
            }
        }

        if (!this->getChildByIDRecursive("death-effects-button"_spr)) {
            if (auto* sfxSpr = createDeathEffectsIcon()) {
                auto* button = CCMenuItemSpriteExtra::create(
                    sfxSpr,
                    this,
                    menu_selector(PaimonDeathEffectsPauseLayer::onDeathEffects)
                );
                if (button) {
                    button->setID("death-effects-button"_spr);
                    menu->addChild(button);
                }
            }
        }

        menu->updateLayout();
    }

    void onDeathAnimation(CCObject*) {
        if (auto popup = paimon::death_effects::DeathAnimationPopup::create()) popup->show();
    }

    void onDeathEffects(CCObject*) {
        if (auto* popup = paimon::death_effects::DeathEffectPopup::create()) {
            popup->show();
        }
    }
};

class $modify(PaimonDeathEffectsAudioEngine, FMODAudioEngine) {
    $override
    int playEffect(std::string path, float speed, float unknown, float volume) {
        if (inGameplay() && isDeathSound(path) &&
            paimon::modules::isEnabled("paimbnails.deatheffects.gameplay")) {
            float deathPct = -1.f;
            bool isNewBest = false;
            if (auto* play = PlayLayer::get()) {
                deathPct = play->getCurrentPercent();
                if (play->m_level && !play->m_isPracticeMode) {
                    isNewBest = deathPct > static_cast<float>(play->m_level->m_normalPercent.value());
                }
            }
            if (paimon::death_effects::DeathEffectManager::get().playDeath(
                    this, speed, volume, deathPct, isNewBest)) {
                if (!paimon::death_effects::DeathEffectManager::get().layerWithVanilla()) {
                    return 1;
                }
            }
        }
        return FMODAudioEngine::playEffect(path, speed, unknown, volume);
    }
};

class $modify(PaimonDeathEffectsPlayLayer, PlayLayer) {
    $override
    void resetLevel() {
        s_deathVfxFiredThisAttempt = false;
        paimon::death_effects::DeathEffectManager::get().handleLevelReset();
        paimon::death_effects::clearGameplayDeathEffects(this);
        PlayLayer::resetLevel();
        if (paimon::modules::isEnabled("paimbnails.deatheffects.gameplay")) {
            paimon::death_effects::prewarmAnimation();
        }
    }

    $override
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool const wasDead = this->m_playerDied || (player && player->m_isDead);
        PlayLayer::destroyPlayer(player, object);
        if (!wasDead && !s_deathVfxFiredThisAttempt && player &&
            (player == this->m_player1 || player == this->m_player2) &&
            (this->m_playerDied || player->m_isDead) &&
            this->m_objectLayer && player->getParent() &&
            paimon::modules::isEnabled("paimbnails.deatheffects.gameplay")) {
            auto position = this->m_objectLayer->convertToNodeSpace(
                player->getParent()->convertToWorldSpace(player->getPosition()));
            if (paimon::death_effects::triggerGameplayDeathEffects(this, player, position)) {
                s_deathVfxFiredThisAttempt = true;
            }
        }
    }

    $override
    void onExit() {
        s_deathVfxFiredThisAttempt = false;
        paimon::death_effects::clearGameplayDeathEffects(this);
        paimon::death_effects::DeathEffectManager::get().handleLevelExit();
        PlayLayer::onExit();
    }
};

// hook the visual emission itself: cancelled collisions (noclip) never emit it.
class $modify(PaimonDeathAnimationPlayer, PlayerObject) {
    void playDeathEffect() {
        auto play = PlayLayer::get();
        if (play && !s_deathVfxFiredThisAttempt &&
            (this == play->m_player1 || this == play->m_player2) &&
            play->m_objectLayer && getParent() &&
            paimon::modules::isEnabled("paimbnails.deatheffects.gameplay")) {
            auto position = play->m_objectLayer->convertToNodeSpace(
                getParent()->convertToWorldSpace(getPosition()));
            bool const spawned = paimon::death_effects::triggerGameplayDeathEffects(
                play, this, position);
            if (spawned) {
                s_deathVfxFiredThisAttempt = true;
                if (paimon::death_effects::shouldSuppressVanillaExplosion()) return;
            }
        }
        PlayerObject::playDeathEffect();
    }
};
