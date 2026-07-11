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

// Prevent GameManager from overriding dynamic/profile songs. Priority::Late
// (not Last) so other mods can still observe/decide after us (VeryLate/Last),
// keeping coordination cooperative.
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

        // Notify other mods that we're taking over audio so they can react
        // (e.g. an external music mod can pause).
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
        // Persistent sprites: created once, only the texture is swapped
        // (no per-swipe create/destroy).
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCSprite> m_sharpBgSprite = nullptr;
        Ref<CCNode> m_bottomGradient = nullptr;
        Ref<CCNode> m_pageSliderThumb = nullptr;
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
        float m_transitionFallbackTime = 0.f;
        // True once we've faded the vanilla GD background out (one-shot, after
        // the level thumbnail finished loading post entry-transition).
        bool m_vanillaHidden = false;
        // Texture cache: original texture and precomputed blur per levelID.
        // Main levels (1-22) never change, so the blur is computed once and
        // reused on every swipe.
        std::unordered_map<int, Ref<CCTexture2D>> m_blurCache;
        std::unordered_map<int, Ref<CCTexture2D>> m_texCache;
        // Last applied levelID, to skip redundant work
        int m_appliedLevelID = 0;
    };

    $override
    bool init(int p0) {
        if (!LevelSelectLayer::init(p0)) return false;

        auto win = CCDirector::get()->getWinSize();

        // page 0 = level 1 (Stereo Madness)
        int levelID = p0 + 1;
        m_fields->m_currentLevelID = levelID;
        
        // level background — keep GD's ORIGINAL background and ground visible
        // for now. We only fade the vanilla background out once the level's
        // thumbnail asset is fully loaded (see hideVanillaBackgroundWithFade,
        // triggered from applyBackground), so the screen stays 100% original
        // until there is a thumbnail ready to transition to.
        this->updateThumbnailBackground(levelID);

        // hide BoomScrollLayer dots and replace with a slider
        if (m_scrollLayer) {
            m_scrollLayer->togglePageIndicators(false);

            // hide the original page-buttons (left/right arrows) by known ID only;
            // a position heuristic would hide other mods' bottom menus (BetterInfo,
            // More Icons, etc.). node-ids standardizes these IDs.
            static constexpr char const* kArrowMenuIDs[] = {
                "page-buttons", "prev-page-menu", "next-page-menu",
                "prev-next-menu", "arrow-menu", " arrows-menu",
                "prev-btn", "next-btn", "prev-arrow", "next-arrow"
            };
            auto isArrowMenuID = [](std::string const& id) {
                for (auto* candidate : kArrowMenuIDs) {
                    if (id == candidate) return true;
                }
                return false;
            };

            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (!child) continue;
                if (isArrowMenuID(child->getID())) {
                    child->setVisible(false);
                }
            }
            // recurse through all children in case the menu is nested;
            // still only hide recognizable IDs.
            std::function<void(CCNode*)> hideArrowMenus = [&](CCNode* node) {
                if (!node) return;
                for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                    if (!child) continue;
                    if (isArrowMenuID(child->getID())) {
                        child->setVisible(false);
                        continue;
                    }
                    hideArrowMenus(child);
                }
            };
            hideArrowMenus(this);

            // horizontal slider reflecting the page position
            float sliderW = win.width * 0.6f;
            float sliderH = 6.f;
            float sliderLeftX = 115.f;
            float sliderY = 32.f;

            // slider background (left-center anchor)
            auto sliderBg = paimon::SpriteHelper::createRoundedRect(
                sliderW, sliderH, sliderH * 0.5f,
                {0.1f, 0.1f, 0.1f, 0.6f}
            );
            if (sliderBg) {
                sliderBg->setAnchorPoint({0.f, 0.5f});
                sliderBg->setPosition({sliderLeftX, sliderY});
                sliderBg->setZOrder(50);
                this->addChild(sliderBg);
            }

            // slider thumb (active bar)
            const int totalPages = 24; // 22 levels + 2 empty sections
            float thumbW = std::max(sliderW / totalPages, 10.f);
            auto thumb = paimon::SpriteHelper::createRoundedRect(
                thumbW, sliderH, sliderH * 0.5f,
                {1.f, 1.f, 1.f, 0.85f}
            );
            if (thumb) {
                thumb->setID("paimon-slider-thumb"_spr);
                thumb->setAnchorPoint({0.5f, 0.5f});
                thumb->setZOrder(51);
                // initial position: centered at the left of the slider
                float thumbStartX = sliderLeftX + thumbW / 2;
                thumb->setPosition({thumbStartX, sliderY});
                this->addChild(thumb);
                m_fields->m_pageSliderThumb = thumb;
                m_fields->m_sliderBgWidth = sliderW;
                m_fields->m_sliderStartX = sliderLeftX + thumbW / 2;
                m_fields->m_sliderThumbWidth = thumbW;
            }
        }

        // bottom gradient for text/UI legibility
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

        // Style the levels-list: dark borders + 1px taller top and bottom.
        // Only touch the first vanilla CCScale9Sprite matching the expected
        // geometry with no custom ID; if another mod (or node-ids) already
        // assigned an ID, leave it alone to avoid tinting mods like BetterInfo
        // or alternate themes.
        {
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (!child) continue;
                auto* s9 = typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(child);
                if (!s9) continue;
                // If it has an ID, assume it's "marked" (by node-ids or another mod); leave it.
                if (!std::string(s9->getID()).empty()) continue;

                auto size = s9->getContentSize();
                auto pos  = s9->getPosition();
                if (size.width <= win.width * 0.5f) continue;
                if (pos.y <= win.height * 0.2f || pos.y >= win.height * 0.8f) continue;

                // 1px taller top and bottom
                s9->setContentSize({size.width, size.height + 2.f});
                s9->setPosition({pos.x, pos.y - 1.f});
                // dark borders; mark it so re-init doesn't reprocess
                s9->setColor({30, 30, 30});
                s9->setOpacity(200);
                s9->setID("paimon-levels-list-bg"_spr);
                break; // only the first match
            }
        }
        
        this->scheduleOnce(schedule_selector(PaimonLevelSelectLayer::forcePlayMusic), 0.0f);
        this->schedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));

        // Perf: cache dynamic-song setting once at init
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
        // Transition finished: safe to render the blur FBO now. (Re)apply the
        // current level's background; if the thumbnail is in RAM it shows
        // instantly, otherwise an async load applies it later.
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

    void checkPageLoop(float dt) {
        if (!m_scrollLayer) return;

        // Safety net: if onEnterTransitionDidFinish never fired (no-transition
        // paths), enable background render after ~0.7s (past the typical 0.5s
        // fade) so we're never left without a background.
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

        // update slider thumb position (loop-aware)
        if (m_fields->m_pageSliderThumb && m_scrollLayer) {
            const int totalPages = 24;
            float pageWidth = m_scrollLayer->getContentSize().width;
            if (pageWidth > 0.f) {
                float scrollX = -m_scrollLayer->m_extendedLayer->getPositionX();
                int rawPage = static_cast<int>(std::round(scrollX / pageWidth));
                // loop: scrolling is cyclic, so the slider wraps to the start
                int loopedPage = ((rawPage % totalPages) + totalPages) % totalPages;
                float progress = static_cast<float>(loopedPage) / static_cast<float>(totalPages - 1);
                // centered thumb: from sliderStartX to sliderStartX + sliderBgWidth - thumbWidth
                float maxTravel = m_fields->m_sliderBgWidth - m_fields->m_sliderThumbWidth;
                float thumbX = m_fields->m_sliderStartX + progress * maxTravel;
                m_fields->m_pageSliderThumb->setPosition({thumbX, m_fields->m_pageSliderThumb->getPosition().y});
            }
        }

        m_fields->m_pageCheckTimer += dt;
        if (m_fields->m_pageCheckTimer >= 0.05f) {
            m_fields->m_pageCheckTimer = 0.f;
            this->syncLevelSelectSong();
        }

        // music "pulse" effect logic
        if (m_fields->m_bgSprite && m_fields->m_cachedDynamicSong) {
             // Perf: throttle FMOD metering to every 3 frames
             if (++m_fields->m_meteringFrameCounter < 3) return;
             m_fields->m_meteringFrameCounter = 0;
             // master channel group to read peaks
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
                         
                         // smoothing: fast attack, slow decay
                         if (peak > m_fields->m_smoothedPeak) {
                             m_fields->m_smoothedPeak = peak;
                         } else {
                             m_fields->m_smoothedPeak -= dt * 1.5f; // decay speed
                             if (m_fields->m_smoothedPeak < 0.f) m_fields->m_smoothedPeak = 0.f;
                         }
                         
                         // reduce sensitivity by 30%
                         float val = m_fields->m_smoothedPeak * 0.7f;

                         // brightness: 80 base -> 255 at peak
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
        // Skip redundant work if this level is already showing
        if (levelID == m_fields->m_appliedLevelID && m_fields->m_bgSprite) return;

        bool isMainLevel = (levelID >= 1 && levelID <= 22);

        if (!isMainLevel) {
             this->applyBackground(nullptr, levelID);
             return;
        }

        // Fast path 1: texture already cached locally (from a previous swipe)
        auto localIt = m_fields->m_texCache.find(levelID);
        if (localIt != m_fields->m_texCache.end() && localIt->second) {
            this->applyBackground(localIt->second.data(), levelID);
            return;
        }

        // Fast path 2: texture in ThumbnailLoader's global RAM cache
        if (auto* cached = ThumbnailLoader::get().tryGetCachedTexture(levelID, false)) {
            m_fields->m_texCache[levelID] = cached;
            this->applyBackground(cached, levelID);
            return;
        }

        // Async path: request load, then cache and apply on arrival
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
    
    
    void applyBackground(CCTexture2D* tex, int levelID = -1) {
        auto win = CCDirector::get()->getWinSize();
        m_fields->m_appliedLevelID = levelID;

        // No texture = empty sections; hide sprites
        if (!tex) {
            if (m_fields->m_sharpBgSprite) m_fields->m_sharpBgSprite->setVisible(false);
            if (m_fields->m_bgSprite) m_fields->m_bgSprite->setVisible(false);
            return;
        }

        // Resolve the blur texture (cached or new)
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

        // Compute cover-fill scale
        auto texSize = tex->getContentSize();
        float scaleX = win.width / texSize.width;
        float scaleY = win.height / texSize.height;
        float scale = std::max(scaleX, scaleY);

        // Sharp sprite (always visible, even during the transition)
        if (!m_fields->m_sharpBgSprite) {
            auto spr = CCSprite::createWithTexture(tex);
            spr->setPosition(win / 2);
            spr->setZOrder(-11);
            spr->setColor({80, 80, 80});
            this->addChild(spr);
            m_fields->m_sharpBgSprite = spr;
            // slow zoom animation (applied once, runs forever)
            spr->runAction(CCRepeatForever::create(CCSequence::create(
                CCScaleTo::create(10.0f, scale * 1.3f),
                CCScaleTo::create(10.0f, scale),
                nullptr
            )));
        } else {
            // Just swap the texture, zero allocation
            m_fields->m_sharpBgSprite->setTexture(tex);
            m_fields->m_sharpBgSprite->setTextureRect(CCRect{0, 0, texSize.width, texSize.height});
        }
        m_fields->m_sharpBgSprite->setScale(scale);
        m_fields->m_sharpBgSprite->setVisible(true);
        m_fields->m_sharpBgSprite->setOpacity(255);

        // Blur sprite (post-transition only, needs a working FBO)
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
            m_fields->m_bgSprite->setScale(bScale);
            m_fields->m_bgSprite->setVisible(true);
            m_fields->m_bgSprite->setOpacity(255);
        } else if (m_fields->m_bgSprite) {
            m_fields->m_bgSprite->setVisible(false);
        }

        // Now that a real thumbnail is actually on screen (and the entry
        // transition is over), smoothly fade the vanilla GD background out to
        // reveal it. Until this point everything stayed 100% original.
        if (tex && m_fields->m_transitionFinished) {
            this->hideVanillaBackgroundWithFade();
        }
    }

    // Fade the vanilla GD background/ground out (one-shot) to smoothly reveal
    // the level thumbnail sitting behind them. Leaves our own thumbnail/blur/
    // gradient nodes and other mods' nodes untouched.
    void hideVanillaBackgroundWithFade() {
        if (m_fields->m_vanillaHidden) return;
        m_fields->m_vanillaHidden = true;

        auto fadeAndHide = [](CCNode* node, float dur) {
            if (!node || !node->isVisible()) return;
            node->stopAllActions();
            // setCascadeOpacityEnabled / opacity only exist on RGBA nodes.
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
                // Can't fade this node's opacity; just hide it once the fade
                // window has elapsed so it disappears together with the rest.
                node->runAction(CCSequence::create(
                    CCDelayTime::create(dur),
                    CCHide::create(),
                    nullptr));
            }
        };

        constexpr float kDur = 0.5f;
        CCNode* mySharp = m_fields->m_sharpBgSprite.data();
        CCNode* myBlur  = m_fields->m_bgSprite.data();
        CCNode* myGrad  = m_fields->m_bottomGradient.data();

        if (auto* children = this->getChildren()) {
            for (auto* node : CCArrayExt<CCNode*>(children)) {
                if (!node) continue;
                // Never touch our own background nodes...
                if (node == mySharp || node == myBlur || node == myGrad) continue;
                // ...nor other mods' background nodes.
                if (paimon::compat::LevelSelectLocator::isForeignModNode(node)) continue;

                bool isVanillaBg = node->getZOrder() < -1;
                bool isGround = (typeinfo_cast<GJGroundLayer*>(node) != nullptr);
                if (isVanillaBg || isGround) {
                    fadeAndHide(node, kDur);
                }
            }
        }
        // Explicitly handle GD's known members in case they sit at z >= -1.
        fadeAndHide(m_backgroundSprite, kDur);
        fadeAndHide(m_groundLayer, kDur);
    }


    void cleanupDynamicSong() {
        if (m_fields->m_audioCleanedUp) return;
        m_fields->m_audioCleanedUp = true;
        AudioContextCoordinator::get().deactivateLevelSelect(true);
    }

    $override
    void onBack(CCObject* sender) {
        cleanupDynamicSong();
        LevelSelectLayer::onBack(sender);
    }
    
    $override
    void keyBackClicked() {
        cleanupDynamicSong();
        LevelSelectLayer::keyBackClicked();
    }
};
