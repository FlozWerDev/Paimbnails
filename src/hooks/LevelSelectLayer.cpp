#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/menu-loop/services/MenuLoopManager.hpp"
#include "../utils/AudioInterop.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include "../framework/HookConventions.hpp"
#include <functional>
#include <unordered_map>

using namespace geode::prelude;
using namespace Shaders;

namespace {
    // Separate fade actions from the persistent slow zoom.
    constexpr int kBgFadeActionTag = 0x50A1;

    inline void restoreMenuLoopPositionIfNeeded() {
        auto& sm = paimon::menuloop::MenuLoopManager::get();
        if (sm.isOriginalMenuLoop() || sm.isOverride()) return;

        auto* colon = sm.getColonMenuLoopStartTime();
        if ((colon && colon->getSettingValue<bool>("enable")) || !sm.getShouldRestoreMenuLoopPoint()) {
            sm.setPauseSongPositionTracking(false);
            return;
        }

        auto* fmod = FMODAudioEngine::get();
        if (fmod && !sm.getPauseSongPositionTracking()) {
            auto oldTrack = fmod->getActiveMusic(0);
            if (oldTrack == sm.getCurrentSong()) {
                sm.setPauseSongPositionTracking(false);
                return;
            }
        }

        sm.restoreLastMenuLoopPosition();
        sm.setShouldRestoreMenuLoopPoint(false);
        sm.setPauseSongPositionTracking(false);
    }
}

// Keep GameManager from overriding dynamic/profile songs while allowing later observers.
class $modify(PaimonGameManager, GameManager) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("GameManager::fadeInMenuMusic", geode::Priority::Late);
    }

    $override
    void fadeInMenuMusic() {
        bool passthrough = Mod::get()->getSavedValue<bool>("music-hook-passthrough", false);
        if (passthrough) {
            GameManager::fadeInMenuMusic();
            restoreMenuLoopPositionIfNeeded();
            return;
        }

        auto* dsm = DynamicSongManager::get();

        auto notifyBlocked = [](char const* reason) {
            paimon::EventBus::get().publish(paimon::AudioOwnerChangedEvent{
                "menu", reason, 0
            });
        };

        if (paimon::isDynamicSongInteropActive() && dsm->isInValidLayer()) {
            notifyBlocked("paimon-dynamic");
            return;
        }
        if (dsm->hasSuspendedPlayback()) {
            notifyBlocked("paimon-dynamic-suspended");
            return;
        }
        if (paimon::isDynamicSongInteropActive() && !dsm->isActive()) paimon::setDynamicSongInteropActive(false);
        if (paimon::isProfileMusicInteropActive()) {
            notifyBlocked("paimon-profile-music");
            return;
        }
        if (paimon::isVideoAudioInteropActive()) {
            notifyBlocked("paimon-video-audio");
            return;
        }
        GameManager::fadeInMenuMusic();
        restoreMenuLoopPositionIfNeeded();
    }
};

// Prevent GD from restarting music on transitions; also handles music
// transitions (save/restore position). Priority::Late (not Last) — see above.
class $modify(PaimonFMODAudioEngine, FMODAudioEngine) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("FMODAudioEngine::playMusic", geode::Priority::Late);
    }

    $override
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        bool passthrough = Mod::get()->getSavedValue<bool>("music-hook-passthrough", false);
        if (passthrough) {
            FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
            return;
        }
        auto requestedPath = static_cast<std::string>(path);
        std::string menuTrack = GameManager::get() ? std::string(GameManager::get()->getMenuMusicFile()) : std::string();
        bool isMenuTrack = !menuTrack.empty() && requestedPath == menuTrack;

        if (!DynamicSongManager::s_selfPlayMusic) {
            auto* dsm = DynamicSongManager::get();

            if (paimon::isVideoAudioInteropActive() && isMenuTrack) {
                return;
            }

            if (paimon::isProfileMusicInteropActive()) {
                if (isMenuTrack) {
                    return;
                }

                ProfileMusicManager::get().forceStop();
                FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
                return;
            }

            if (paimon::isDynamicSongInteropActive() && dsm->isInValidLayer()) {
                return;
            }
            if (dsm->hasSuspendedPlayback() && isMenuTrack) {
                return;
            }
        }
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
    }
};

class $modify(PaimonLevelSelectLayer, LevelSelectLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelSelectLayer::init");
    }

    struct Fields {
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCSprite> m_sharpBgSprite = nullptr;
        Ref<CCSprite> m_fadeOverlay = nullptr;
        Ref<CCNode> m_bottomGradient = nullptr;
        Ref<CCNodeRGBA> m_pageSlider = nullptr;
        Ref<CCNode> m_pageSliderThumb = nullptr;
        WeakRef<CCNode> m_soundtrackButton;
        float m_sliderBgWidth = 0.f;
        float m_sliderStartX = 0.f;
        float m_sliderThumbWidth = 0.f;
        int m_currentLevelID = 0;
        float m_pageCheckTimer = 0.f;
        float m_smoothedPeak = 0.f;
        int m_verifyFrameCounter = 0;
        bool m_meteringEnabled = false;
        bool m_audioCleanedUp = false;
        bool m_cachedDynamicSong = false;
        int m_meteringFrameCounter = 0;
        bool m_transitionFinished = false;
        bool m_exitAnimated = false;
        bool m_waitingForSoundtrack = false;
        float m_transitionFallbackTime = 0.f;
        bool m_vanillaHidden = false;
        std::unordered_map<int, Ref<CCTexture2D>> m_blurCache;
        std::unordered_map<int, Ref<CCTexture2D>> m_texCache;
        int m_appliedLevelID = 0;
    };

    $override
    bool init(int p0) {
        if (!LevelSelectLayer::init(p0)) return false;


        auto win = CCDirector::get()->getWinSize();

        int levelID = p0 + 1;
        m_fields->m_currentLevelID = levelID;

        auto* soundtrackButton = this->getChildByIDRecursive("download-soundtrack-button");
        m_fields->m_soundtrackButton = soundtrackButton;
        m_fields->m_waitingForSoundtrack = this->isVisibleInTree(soundtrackButton);
        
        // level background — keep GD's ORIGINAL background and ground visible
        // for now. We only fade the vanilla background out once the level's
        // thumbnail asset is fully loaded (see hideVanillaBackgroundWithFade,
        // triggered from applyBackground), so the screen stays 100% original
        // until there is a thumbnail ready to transition to.
        this->updateThumbnailBackground(levelID);

        if (m_scrollLayer) {
            m_scrollLayer->togglePageIndicators(false);

            float sliderW = win.width * 0.6f;
            float sliderH = 6.f;
            float sliderLeftX = 115.f;
            float sliderY = 32.f;

            auto slider = CCNodeRGBA::create();
            slider->setID("paimon-page-slider"_spr);
            slider->setCascadeOpacityEnabled(true);
            float sliderTargetY = m_fields->m_waitingForSoundtrack ? -10.f : 0.f;
            slider->setPositionY(sliderTargetY - 4.f);
            slider->setOpacity(0);
            this->addChild(slider, 50);
            m_fields->m_pageSlider = slider;

            auto sliderBg = paimon::SpriteHelper::createRoundedRect(
                sliderW, sliderH, sliderH * 0.5f,
                {0.1f, 0.1f, 0.1f, 0.6f}
            );
            if (sliderBg) {
                sliderBg->setAnchorPoint({0.f, 0.5f});
                sliderBg->setPosition({sliderLeftX, sliderY});
                slider->addChild(sliderBg);
            }

            const int totalPages = 24;
            float thumbW = std::max(sliderW / totalPages, 10.f);
            auto thumb = paimon::SpriteHelper::createRoundedRect(
                thumbW, sliderH, sliderH * 0.5f,
                {1.f, 1.f, 1.f, 0.85f}
            );
            if (thumb) {
                thumb->setID("paimon-slider-thumb"_spr);
                thumb->setAnchorPoint({0.5f, 0.5f});
                float thumbStartX = sliderLeftX + thumbW / 2;
                thumb->setPosition({thumbStartX, sliderY});
                slider->addChild(thumb, 1);
                m_fields->m_pageSliderThumb = thumb;
                m_fields->m_sliderBgWidth = sliderW;
                m_fields->m_sliderStartX = sliderLeftX + thumbW / 2;
                m_fields->m_sliderThumbWidth = thumbW;
            }

            slider->runAction(CCSequence::create(
                CCDelayTime::create(0.08f),
                CCSpawn::create(
                    CCFadeTo::create(0.24f, 255),
                    CCEaseSineOut::create(CCMoveTo::create(0.3f, {0.f, sliderTargetY})),
                    nullptr
                ),
                nullptr
            ));
        }

        {
            float gradH = win.height * 0.2f;
            auto grad = CCLayerGradient::create(
                {0, 0, 0, 0},
                {0, 0, 0, 140}
            );
            if (grad) {
                grad->setContentSize({win.width, gradH});
                grad->setPosition({0, 0});
                grad->setZOrder(-8);
                grad->setOpacity(0);
                this->addChild(grad);
                grad->runAction(CCFadeTo::create(0.8f, 255));
                m_fields->m_bottomGradient = grad;
            }
        }

        // Style only the first unclaimed vanilla list border.
        {
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (!child) continue;
                auto* s9 = typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(child);
                if (!s9) continue;
                if (!std::string(s9->getID()).empty()) continue;

                auto size = s9->getContentSize();
                auto pos  = s9->getPosition();
                if (size.width <= win.width * 0.5f) continue;
                if (pos.y <= win.height * 0.2f || pos.y >= win.height * 0.8f) continue;

                s9->setContentSize({size.width, size.height + 2.f});
                s9->setPosition({pos.x, pos.y - 1.f});
                s9->setColor({30, 30, 30});
                s9->setOpacity(200);
                s9->setID("paimon-levels-list-bg"_spr);
                break;
            }
        }
        
        this->scheduleOnce(schedule_selector(PaimonLevelSelectLayer::forcePlayMusic), 0.0f);
        this->schedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));

        m_fields->m_cachedDynamicSong = Mod::get()->getSettingValue<bool>("dynamic-song");
        
        return true;
    }

    $override
    void onEnter() {
        LevelSelectLayer::onEnter();
        int levelID = this->resolveVisibleLevelID();
        m_fields->m_currentLevelID = levelID;
        if (levelID > 0 && levelID <= 22) {
            this->syncLevelSelectSong(true);
        }
    }

    $override
    void onEnterTransitionDidFinish() {
        LevelSelectLayer::onEnterTransitionDidFinish();
        // Render blur only after the transition; RAM hits apply immediately.
        m_fields->m_transitionFinished = true;
        this->updateThumbnailBackground(m_fields->m_currentLevelID);
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));
        this->unschedule(schedule_selector(PaimonLevelSelectLayer::forcePlayMusic));
        LevelSelectLayer::onExit();
    }

    int resolveVisibleLevelID() {
        if (!m_scrollLayer) return m_fields->m_currentLevelID;

        CCLayer* pagesLayer = m_scrollLayer->m_extendedLayer;
        if (!pagesLayer) return m_fields->m_currentLevelID;

        float width = m_scrollLayer->getContentSize().width;
        if (width <= 0.f) return m_fields->m_currentLevelID;

        int page = static_cast<int>(std::round(-pagesLayer->getPositionX() / width));

        const int totalLevels = 22;
        const int emptySections = 2;
        const int cycleSize = totalLevels + emptySections;

        int cycleIndex = (page % cycleSize + cycleSize) % cycleSize;
        if (cycleIndex < totalLevels) {
            return cycleIndex + 1;
        }

        return -1;
    }

    void syncLevelSelectSong(bool force = false) {
        if (m_fields->m_audioCleanedUp) return;

        auto& coordinator = AudioContextCoordinator::get();
        if (coordinator.isGameplayActive() || coordinator.isProfileOpen()) return;

        int levelID = m_fields->m_currentLevelID;
        if (levelID <= 0 || levelID > 22) return;

        auto* dsm = DynamicSongManager::get();
        bool needsSync = force ||
            coordinator.getDynamicContextLayer() != DynSongLayer::LevelSelect ||
            coordinator.getCurrentLevelSelectID() != levelID ||
            !dsm->isActive() ||
            dsm->getCurrentPlayingLevelID() != levelID ||
            !dsm->verifyPlayback();
        if (!needsSync) return;

        coordinator.activateLevelSelect(levelID, true);
    }

    void forcePlayMusic(float dt) {
         int levelID = this->resolveVisibleLevelID();
         if (levelID <= 0) levelID = 1;
         m_fields->m_currentLevelID = levelID;
         
         this->syncLevelSelectSong(true);
    }

    bool isVisibleInTree(CCNode* node) const {
        if (!node || !node->getParent()) return false;

        for (auto* current = node; current; current = current->getParent()) {
            if (!current->isVisible()) return false;
            if (current == this) return true;
        }
        return false;
    }

    void centerPageSlider() {
        auto slider = m_fields->m_pageSlider;
        if (!slider) return;

        slider->stopAllActions();
        slider->runAction(CCSpawn::create(
            CCFadeTo::create(0.24f, 255),
            CCEaseSineOut::create(CCMoveTo::create(0.3f, {0.f, 0.f})),
            nullptr
        ));
    }

    void checkPageLoop(float dt) {
        if (!m_scrollLayer) return;

        if (m_fields->m_waitingForSoundtrack) {
            auto soundtrackButton = m_fields->m_soundtrackButton.lock();
            if (!this->isVisibleInTree(soundtrackButton)) {
                m_fields->m_waitingForSoundtrack = false;
                this->centerPageSlider();
            }
        }

        // Recover background rendering on paths that skip the enter callback.
        if (!m_fields->m_transitionFinished) {
            m_fields->m_transitionFallbackTime += dt;
            if (m_fields->m_transitionFallbackTime >= 0.7f) {
                m_fields->m_transitionFinished = true;
                this->updateThumbnailBackground(m_fields->m_currentLevelID);
            }
        }

        int levelID = this->resolveVisibleLevelID();
        
        if (m_fields->m_currentLevelID != levelID) {
            m_fields->m_currentLevelID = levelID;
            m_fields->m_pageCheckTimer = 0.f;

            this->updateThumbnailBackground(levelID);

            if (levelID > 0 && levelID <= 22) {
                this->syncLevelSelectSong(true);
            }
        }

        if (m_fields->m_pageSliderThumb && m_scrollLayer && m_scrollLayer->m_extendedLayer) {
            const int totalPages = 24;
            float pageWidth = m_scrollLayer->getContentSize().width;
            if (pageWidth > 0.f) {
                float scrollX = -m_scrollLayer->m_extendedLayer->getPositionX();
                float exactPage = scrollX / pageWidth;
                float looped = std::fmod(std::fmod(exactPage, static_cast<float>(totalPages)) + totalPages, static_cast<float>(totalPages));
                float progress = looped / static_cast<float>(totalPages - 1);
                if (progress > 1.f) progress = 1.f;
                float maxTravel = m_fields->m_sliderBgWidth - m_fields->m_sliderThumbWidth;
                float targetX = m_fields->m_sliderStartX + progress * maxTravel;
                auto* thumb = m_fields->m_pageSliderThumb.data();
                float curX = thumb->getPositionX();
                float newX;
                if (std::fabs(targetX - curX) > maxTravel * 0.5f) {
                    newX = targetX;
                } else {
                    newX = curX + (targetX - curX) * std::min(1.f, dt * 14.f);
                }
                thumb->setPositionX(newX);
            }
        }

        m_fields->m_pageCheckTimer += dt;
        if (m_fields->m_pageCheckTimer >= 0.05f) {
            m_fields->m_pageCheckTimer = 0.f;
            this->syncLevelSelectSong();
        }

        if (m_fields->m_bgSprite && m_fields->m_cachedDynamicSong) {
             if (++m_fields->m_meteringFrameCounter < 3) return;
             m_fields->m_meteringFrameCounter = 0;
              auto engine = FMODAudioEngine::sharedEngine();
              if (engine && engine->m_system) {
                 FMOD::ChannelGroup* masterGroup = nullptr;
                 engine->m_system->getMasterChannelGroup(&masterGroup);
                 
                 if (masterGroup) {
                     FMOD::DSP* headDSP = nullptr;
                     masterGroup->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &headDSP);
                     
                     if (headDSP) {
                         if (!m_fields->m_meteringEnabled) {
                             headDSP->setMeteringEnabled(false, true);
                             m_fields->m_meteringEnabled = true;
                         }

                         FMOD_DSP_METERING_INFO meteringInfo = {};
                         headDSP->getMeteringInfo(nullptr, &meteringInfo);
                         
                         float peak = 0.f;
                         if (meteringInfo.numchannels > 0) {
                             for (int i=0; i<meteringInfo.numchannels; i++) {
                                 if (meteringInfo.peaklevel[i] > peak) peak = meteringInfo.peaklevel[i];
                             }
                         }
                         
                         if (peak > m_fields->m_smoothedPeak) {
                             m_fields->m_smoothedPeak = peak;
                         } else {
                              m_fields->m_smoothedPeak -= dt * 1.5f;
                             if (m_fields->m_smoothedPeak < 0.f) m_fields->m_smoothedPeak = 0.f;
                         }
                         
                         float val = m_fields->m_smoothedPeak * 0.7f;

                         float brightnessVal = 80.f + (val * 175.f);
                         if (brightnessVal > 255.f) brightnessVal = 255.f;
                         GLubyte cVal = static_cast<GLubyte>(brightnessVal);

                         if (m_fields->m_bgSprite) {
                             m_fields->m_bgSprite->setColor({cVal, cVal, cVal});
                         }
                         
                         if (m_fields->m_sharpBgSprite) {
                             GLubyte sharpVal = static_cast<GLubyte>(cVal * 0.9f); 
                             m_fields->m_sharpBgSprite->setColor({sharpVal, sharpVal, sharpVal});
                         }
                     }
                 }
             }
        }
    }
    

    void updateThumbnailBackground(int levelID) {
        if (levelID == m_fields->m_appliedLevelID && m_fields->m_bgSprite) return;

        bool isMainLevel = (levelID >= 1 && levelID <= 22);

        if (!isMainLevel) {
             this->applyBackground(nullptr, levelID);
             return;
        }

        auto localIt = m_fields->m_texCache.find(levelID);
        if (localIt != m_fields->m_texCache.end() && localIt->second) {
            this->applyBackground(localIt->second.data(), levelID);
            return;
        }

        if (auto* cached = ThumbnailLoader::get().tryGetCachedTexture(levelID, false)) {
            m_fields->m_texCache[levelID] = cached;
            this->applyBackground(cached, levelID);
            return;
        }

        std::string fileName = fmt::format("{}.png", levelID);
        Ref<LevelSelectLayer> self = this;

        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool success) {
            auto* layer = static_cast<PaimonLevelSelectLayer*>(self.data());
            if (!layer || !layer->getParent()) return;
            if (success && tex) {
                layer->m_fields->m_texCache[levelID] = tex;
            }
            if (layer->m_fields->m_currentLevelID == levelID) {
                layer->applyBackground(success ? tex : nullptr, levelID);
            }
        }, ThumbnailLoader::PriorityHero, false);
    }
    
    
    // Fade an outgoing snapshot over the incoming background.
    void showFadeOverlayFrom(CCSprite* src) {
        auto* tex = src->getTexture();
        if (!tex) return;
        auto win = CCDirector::get()->getWinSize();
        auto texSize = tex->getContentSize();

        auto overlay = m_fields->m_fadeOverlay;
        if (!overlay) {
            overlay = CCSprite::createWithTexture(tex);
            if (!overlay) return;
            overlay->setPosition(win / 2);
            overlay->setZOrder(-9);
            this->addChild(overlay);
            m_fields->m_fadeOverlay = overlay;
        } else {
            overlay->setTexture(tex);
            overlay->setTextureRect(CCRect{0, 0, texSize.width, texSize.height});
        }
        overlay->setScale(src->getScale());
        overlay->setColor(src->getColor());
        overlay->stopAllActions();
        overlay->setVisible(true);
        overlay->setOpacity(src->getOpacity());
        overlay->runAction(CCSequence::create(
            CCEaseSineOut::create(CCFadeTo::create(0.35f, 0)),
            CCHide::create(),
            nullptr
        ));
    }

    void applyBackground(CCTexture2D* tex, int levelID = -1) {
        auto win = CCDirector::get()->getWinSize();

        int prevLevelID = m_fields->m_appliedLevelID;
        m_fields->m_appliedLevelID = levelID;

        // Capture the outgoing background except on the first application.
        if (levelID != prevLevelID && prevLevelID != 0 && m_fields->m_transitionFinished) {
            CCSprite* fadeSrc = nullptr;
            if (auto* b = m_fields->m_bgSprite.data(); b && b->isVisible() && b->getOpacity() > 0) {
                fadeSrc = b;
            } else if (auto* s = m_fields->m_sharpBgSprite.data(); s && s->isVisible() && s->getOpacity() > 0) {
                fadeSrc = s;
            }
            if (fadeSrc) this->showFadeOverlayFrom(fadeSrc);
        }

        if (!tex) {
            auto fadeHide = [](CCSprite* s) {
                if (!s || !s->isVisible()) return;
                s->stopActionByTag(kBgFadeActionTag);
                auto* seq = CCSequence::create(
                    CCFadeTo::create(0.25f, 0),
                    CCHide::create(),
                    nullptr
                );
                seq->setTag(kBgFadeActionTag);
                s->runAction(seq);
            };
            fadeHide(m_fields->m_sharpBgSprite.data());
            fadeHide(m_fields->m_bgSprite.data());
            return;
        }

        CCTexture2D* blurTex = nullptr;
        if (m_fields->m_transitionFinished && levelID > 0) {
            auto blurIt = m_fields->m_blurCache.find(levelID);
            if (blurIt != m_fields->m_blurCache.end() && blurIt->second) {
                blurTex = blurIt->second.data();
            } else {
                auto* blurSprite = BlurSystem::getInstance()->createPaimonBlurSprite(tex, win, 2.0f);
                if (blurSprite) {
                    blurTex = blurSprite->getTexture();
                    m_fields->m_blurCache[levelID] = blurTex;
                }
            }
        }

        auto texSize = tex->getContentSize();
        float scaleX = win.width / texSize.width;
        float scaleY = win.height / texSize.height;
        float scale = std::max(scaleX, scaleY);

        if (!m_fields->m_sharpBgSprite) {
            auto spr = CCSprite::createWithTexture(tex);
            spr->setPosition(win / 2);
            spr->setZOrder(-11);
            spr->setColor({80, 80, 80});
            this->addChild(spr);
            m_fields->m_sharpBgSprite = spr;
            spr->runAction(CCRepeatForever::create(CCSequence::create(
                CCScaleTo::create(10.0f, scale * 1.3f),
                CCScaleTo::create(10.0f, scale),
                nullptr
            )));
        } else {
            m_fields->m_sharpBgSprite->setTexture(tex);
            m_fields->m_sharpBgSprite->setTextureRect(CCRect{0, 0, texSize.width, texSize.height});
        }
        m_fields->m_sharpBgSprite->stopActionByTag(kBgFadeActionTag);
        m_fields->m_sharpBgSprite->setScale(scale);
        m_fields->m_sharpBgSprite->setVisible(true);
        m_fields->m_sharpBgSprite->setOpacity(255);

        if (blurTex && m_fields->m_transitionFinished) {
            auto blurSize = blurTex->getContentSize();
            float bScaleX = win.width / blurSize.width;
            float bScaleY = win.height / blurSize.height;
            float bScale = std::max(bScaleX, bScaleY);

            if (!m_fields->m_bgSprite) {
                auto spr = CCSprite::createWithTexture(blurTex);
                spr->setPosition(win / 2);
                spr->setZOrder(-10);
                this->addChild(spr);
                m_fields->m_bgSprite = spr;
                spr->runAction(CCRepeatForever::create(CCSequence::create(
                    CCScaleTo::create(10.0f, bScale * 1.3f),
                    CCScaleTo::create(10.0f, bScale),
                    nullptr
                )));
            } else {
                m_fields->m_bgSprite->setTexture(blurTex);
                m_fields->m_bgSprite->setTextureRect(CCRect{0, 0, blurSize.width, blurSize.height});
            }
            m_fields->m_bgSprite->stopActionByTag(kBgFadeActionTag);
            m_fields->m_bgSprite->setScale(bScale);
            m_fields->m_bgSprite->setVisible(true);
            m_fields->m_bgSprite->setOpacity(255);
        } else if (m_fields->m_bgSprite) {
            m_fields->m_bgSprite->setVisible(false);
        }

        // Hide vanilla background only after a real thumbnail is visible.
        if (tex && m_fields->m_transitionFinished) {
            this->hideVanillaBackgroundWithFade();
        }
    }

    // Fade vanilla background nodes without touching our or other mods' nodes.
    void hideVanillaBackgroundWithFade() {
        if (m_fields->m_vanillaHidden) return;
        m_fields->m_vanillaHidden = true;

        auto fadeAndHide = [](CCNode* node, float dur) {
            if (!node || !node->isVisible()) return;
            node->stopAllActions();
            bool canFade = false;
            if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(node)) {
                rgba->setCascadeOpacityEnabled(true);
                canFade = true;
            } else if (auto* rgbaLayer = typeinfo_cast<CCLayerRGBA*>(node)) {
                rgbaLayer->setCascadeOpacityEnabled(true);
                canFade = true;
            }
            if (canFade) {
                node->runAction(CCSequence::create(
                    CCFadeOut::create(dur),
                    CCHide::create(),
                    nullptr));
            } else {
                node->runAction(CCSequence::create(
                    CCDelayTime::create(dur),
                    CCHide::create(),
                    nullptr));
            }
        };

        constexpr float kDur = 0.5f;
        CCNode* mySharp   = m_fields->m_sharpBgSprite.data();
        CCNode* myBlur    = m_fields->m_bgSprite.data();
        CCNode* myGrad    = m_fields->m_bottomGradient.data();
        CCNode* myOverlay = m_fields->m_fadeOverlay.data();

        if (auto* children = this->getChildren()) {
            for (auto* node : CCArrayExt<CCNode*>(children)) {
                if (!node) continue;
                if (node == mySharp || node == myBlur || node == myGrad || node == myOverlay) continue;
                if (paimon::compat::LevelSelectLocator::isForeignModNode(node)) continue;

                bool isVanillaBg = node->getZOrder() < -1;
                bool isGround = (typeinfo_cast<GJGroundLayer*>(node) != nullptr);
                if (isVanillaBg || isGround) {
                    fadeAndHide(node, kDur);
                }
            }
        }
        fadeAndHide(m_backgroundSprite, kDur);
        fadeAndHide(m_groundLayer, kDur);
    }


    void cleanupDynamicSong() {
        if (m_fields->m_audioCleanedUp) return;
        m_fields->m_audioCleanedUp = true;
        AudioContextCoordinator::get().deactivateLevelSelect(true);
    }

    // Ease our UI out with GD's outgoing scene transition.
    void animateExit() {
        if (m_fields->m_exitAnimated) return;
        m_fields->m_exitAnimated = true;

        if (auto* slider = m_fields->m_pageSlider.data()) {
            slider->stopAllActions();
            slider->runAction(CCSpawn::create(
                CCFadeTo::create(0.2f, 0),
                CCEaseSineIn::create(CCMoveTo::create(0.25f, {0.f, -12.f})),
                nullptr
            ));
        }

        if (auto* grad = m_fields->m_bottomGradient.data()) {
            grad->stopAllActions();
            grad->runAction(CCFadeTo::create(0.25f, 0));
        }

        auto fadeBg = [](CCSprite* s) {
            if (!s || !s->isVisible()) return;
            s->stopActionByTag(kBgFadeActionTag);
            auto* fade = CCEaseSineIn::create(CCFadeTo::create(0.35f, 0));
            fade->setTag(kBgFadeActionTag);
            s->runAction(fade);
        };
        fadeBg(m_fields->m_bgSprite.data());
        fadeBg(m_fields->m_sharpBgSprite.data());
        if (auto* overlay = m_fields->m_fadeOverlay.data(); overlay && overlay->isVisible()) {
            overlay->stopAllActions();
            overlay->runAction(CCFadeTo::create(0.25f, 0));
        }
    }

    $override
    void onBack(CCObject* sender) {
        cleanupDynamicSong();
        animateExit();
        LevelSelectLayer::onBack(sender);
    }

    $override
    void keyBackClicked() {
        cleanupDynamicSong();
        animateExit();
        LevelSelectLayer::keyBackClicked();
    }
};
