#include "../framework/compat/NativeUI.hpp"
#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/ui/NodeEvent.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/official-slots/services/SlotLevels.hpp"
#include "../features/official-slots/services/OfficialSlotStore.hpp"
#include "../features/official-slots/services/SlotListRefresh.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongConfig.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/menu-loop/services/MenuLoopManager.hpp"
#include "../utils/AudioInterop.hpp"
#include "../utils/EditorContext.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include "../framework/HookConventions.hpp"
#include "../ui/PageSliderInput.hpp"
#include <functional>
#include <unordered_map>

using namespace geode::prelude;
using namespace Shaders;

namespace {
    // separate fade actions from the persistent slow zoom.
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
        if (fmod && !sm.getPauseSongPositionTracking() &&
            fmod->getActiveMusic(0) == sm.getCurrentSong()) {
            sm.setPauseSongPositionTracking(false);
            return;
        }

        sm.restoreLastMenuLoopPosition();
        sm.setShouldRestoreMenuLoopPoint(false);
        sm.setPauseSongPositionTracking(false);
    }
}

// keep gamemanager from overriding dynamic/profile songs while allowing later observers.
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

        if (paimon::isDynamicSongInteropActive() && !dsm->isActive()) {
            paimon::setDynamicSongInteropActive(false);
        }
        if (paimon::isDynamicSongInteropActive() && dsm->isInValidLayer()) {
            notifyBlocked("paimon-dynamic");
            return;
        }
        if (dsm->hasSuspendedPlayback()) {
            notifyBlocked("paimon-dynamic-suspended");
            return;
        }
        if (paimon::isProfileMusicInteropActive()) {
            auto& pmm = ProfileMusicManager::get();
            if (!pmm.isPlaying() && !pmm.isFadingOut() &&
                !AudioContextCoordinator::get().isProfileOpen()) {
                paimon::setProfileMusicInteropActive(false);
            } else {
                notifyBlocked("paimon-profile-music");
                return;
            }
        }
        if (paimon::isVideoAudioInteropActive()) {
            notifyBlocked("paimon-video-audio");
            return;
        }
        GameManager::fadeInMenuMusic();
        restoreMenuLoopPositionIfNeeded();
    }
};

// block gd music restarts on transitions; late (not last) keeps later observers working
class $modify(PaimonFMODAudioEngine, FMODAudioEngine) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("FMODAudioEngine::playMusic", geode::Priority::Late);
    }

    $override
    void playMusic(std::string path, bool shouldLoop, float fadeInTime, int channel) {
        bool passthrough = Mod::get()->getSavedValue<bool>("music-hook-passthrough", false);
        // downloaded Songs uses this same entry point for editor previews.
        // audio ownership from a previous menu must not suppress those calls.
        if (passthrough || paimon::isEditorScene()) {
            FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
            return;
        }
        auto requestedPath = static_cast<std::string>(path);
        auto* gm = GameManager::get();
        std::string menuTrack = gm ? std::string(gm->getMenuMusicFile()) : std::string();
        bool isMenuTrack = !menuTrack.empty() && requestedPath == menuTrack;

        if (DynamicSongManager::s_selfPlayMusic) {
            FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
            return;
        }
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
        Ref<paimon::ui::PageSliderInput> m_pageSliderInput = nullptr;
        WeakRef<CCNode> m_soundtrackButton;
        float m_sliderBgWidth = 0.f;
        float m_sliderLeftX = 0.f;
        float m_sliderThumbWidth = 0.f;
        int m_currentLevelID = 0;
        int m_audioPageId = 0;
        std::uint64_t m_audioRequest = 0;
        Ref<GJGameLevel> m_audioLevel = nullptr;
        bool m_audioLoading = false;
        bool m_audioFailed = false;
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
        // init is bound by Geode on this class. Inherited lifecycle callbacks
        // are not, so restore the selector context here on every native return.
        AudioContextCoordinator::get().activateLevelSelect(0, false);

        auto win = CCDirector::get()->getWinSize();

        int levelID = p0 + 1;
        m_fields->m_currentLevelID = levelID;

        auto* soundtrackButton = this->getChildByIDRecursive("download-soundtrack-button");
        m_fields->m_soundtrackButton = soundtrackButton;
        m_fields->m_waitingForSoundtrack = this->isVisibleInTree(soundtrackButton);
        
        // keep gd bg until the thumbnail is ready
        this->updateThumbnailBackground(levelID);

        if (m_scrollLayer) {
            paimon::compat::setPageIndicatorsVisible(m_scrollLayer, false);

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

            int const totalPages = this->visiblePageCount();
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
                m_fields->m_sliderLeftX = sliderLeftX;
                m_fields->m_sliderThumbWidth = thumbW;

                auto* input = paimon::ui::PageSliderInput::create(sliderW, [this](float progress) {
                    this->scrubToPage(progress);
                });
                if (input) {
                    input->setID("paimon-page-slider-input"_spr);
                    input->setPosition({sliderLeftX, sliderY - input->getContentHeight() * 0.5f});
                    input->setThumbState(thumbW, 0.f);
                    slider->addChild(input, 2);
                    m_fields->m_pageSliderInput = input;
                }
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

        // style only the first unclaimed vanilla list border
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

        this->addOnExitCallback([this] {
            if (!AudioContextCoordinator::get().isGameplayActive()) {
                cleanupDynamicSong();
            }
        });

        return true;
    }

    GJGameLevel* visiblePageLevel() {
        auto* scroll = m_scrollLayer;
        if (!scroll || !scroll->m_dynamic || !scroll->m_dynamicObjects || !scroll->m_extendedLayer ||
            scroll->m_dynamicObjects->count() == 0 || scroll->getContentWidth() <= 0.f) return nullptr;
        int const page = static_cast<int>(std::round(
            -scroll->m_extendedLayer->getPositionX() / scroll->getContentWidth()));
        return typeinfo_cast<GJGameLevel*>(
            scroll->m_dynamicObjects->objectAtIndex(scroll->getRelativePageForNum(page)));
    }

    int resolveVisibleLevelID() {
        if (!m_scrollLayer) return m_fields->m_currentLevelID;

        CCLayer* pagesLayer = m_scrollLayer->m_extendedLayer;
        if (!pagesLayer) return m_fields->m_currentLevelID;

        float width = m_scrollLayer->getContentSize().width;
        if (width <= 0.f) return m_fields->m_currentLevelID;

        int page = static_cast<int>(std::round(-pagesLayer->getPositionX() / width));

        // official slots can reorder or hide levels, so read the list instead of assuming index + 1
        auto* levels = m_scrollLayer->m_dynamicObjects;
        if (m_scrollLayer->m_dynamic && levels && levels->count() > 0) {
            int index = m_scrollLayer->getRelativePageForNum(page);
            auto* level = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(index));
            int const sourceId = paimon::officialslots::pageSourceLevelId(level);
            return sourceId > 0 ? sourceId : -1;
        }

        const int totalLevels = 22;
        const int emptySections = 2;
        const int cycleSize = totalLevels + emptySections;

        int cycleIndex = (page % cycleSize + cycleSize) % cycleSize;
        if (cycleIndex < totalLevels) {
            return cycleIndex + 1;
        }

        return -1;
    }

    int visiblePageCount() const {
        if (m_scrollLayer && m_scrollLayer->m_dynamic && m_scrollLayer->m_dynamicObjects) {
            return std::max(1, static_cast<int>(m_scrollLayer->m_dynamicObjects->count()));
        }
        return 24;
    }

    void scrubToPage(float progress) {
        if (!m_scrollLayer || m_fields->m_exitAnimated) return;
        int const target = static_cast<int>(std::round(
            std::clamp(progress, 0.f, 1.f) * (this->visiblePageCount() - 1)));
        if (m_scrollLayer->m_page == target && !m_scrollLayer->m_pageMoving) return;

        // instantMoveToPage cancels the native page animation before moving.
        // repaint the recycled cards as well when the catalogue is dynamic.
        if (m_scrollLayer->m_dynamic) {
            paimon::officialslots::selectVisiblePage(this, target);
        } else {
            m_scrollLayer->instantMoveToPage(target);
        }
    }

    void syncLevelSelectSong(bool force = false) {
        using namespace paimon::officialslots;
        if (m_fields->m_audioCleanedUp) return;

        auto& coordinator = AudioContextCoordinator::get();
        if (coordinator.isGameplayActive() || coordinator.isProfileOpen()) return;
        if (!Mod::get()->getSettingValue<bool>("dynamic-song") || !paimon::dynsong::config().inLevelSelect) return;

        auto* visible = this->visiblePageLevel();
        int const pageId = visible ? static_cast<int>(visible->m_levelID) : 0;
        if (pageId != m_fields->m_audioPageId) {
            ++m_fields->m_audioRequest;
            m_fields->m_audioPageId = pageId;
            m_fields->m_audioLevel = nullptr;
            m_fields->m_audioLoading = false;
            m_fields->m_audioFailed = false;
        }
        auto* dsm = DynamicSongManager::get();
        if (pageId <= 0) {
            if (coordinator.getDynamicContextLayer() == DynSongLayer::LevelSelect &&
                coordinator.getCurrentLevelSelectID() != 0) {
                dsm->stopSong();
                coordinator.activateLevelSelect(0, false);
            }
            return;
        }
        if (!m_fields->m_audioLevel) {
            if (m_fields->m_audioLoading || m_fields->m_audioFailed) return;
            if (auto slotId = SlotLevelCache::get().slotIdForLevel(visible)) {
                auto slot = SlotStore::get().find(*slotId);
                if (!slot || !slot->enabled) return;
                m_fields->m_audioLoading = true;
                auto const request = m_fields->m_audioRequest;
                WeakRef<PaimonLevelSelectLayer> weak(this);
                loadSlotLevel(*slot, [weak, request, pageId](GJGameLevel* ready) {
                    auto ref = weak.lock();
                    auto* self = ref ? static_cast<PaimonLevelSelectLayer*>(ref.data()) : nullptr;
                    if (!self || !self->getParent() || self->m_fields->m_audioRequest != request) return;
                    auto* current = self->visiblePageLevel();
                    if (!current || current->m_levelID != pageId) return;
                    self->m_fields->m_audioLoading = false;
                    self->m_fields->m_audioFailed = !ready;
                    self->m_fields->m_audioLevel = ready;
                    if (ready) self->syncLevelSelectSong(true);
                });
                return;
            }
            m_fields->m_audioLevel = visible;
        }

        auto* level = m_fields->m_audioLevel.data();
        int const levelID = level->m_levelID;
        bool const sameContext = coordinator.getDynamicContextLayer() == DynSongLayer::LevelSelect &&
            coordinator.getCurrentLevelSelectLevel() == level;
        if (sameContext && dsm->isStreamingPreviewPending()) return;
        bool needsSync = force ||
            !sameContext ||
            coordinator.getCurrentLevelSelectID() != levelID ||
            !dsm->isActive() ||
            dsm->getCurrentPlayingLevelID() != levelID ||
            !dsm->verifyPlayback();
        if (!needsSync) return;

        coordinator.activateLevelSelectLevel(level, true);
    }

    void forcePlayMusic(float) {
        int levelID = this->resolveVisibleLevelID();
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

        // recover bg rendering on paths that skip the enter callback
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

            this->syncLevelSelectSong(true);
        }

        if (m_fields->m_pageSliderThumb && m_scrollLayer && m_scrollLayer->m_extendedLayer) {
            int const totalPages = this->visiblePageCount();
            float pageWidth = m_scrollLayer->getContentSize().width;
            if (pageWidth > 0.f) {
                float scrollX = -m_scrollLayer->m_extendedLayer->getPositionX();
                float exactPage = scrollX / pageWidth;
                float looped = std::fmod(std::fmod(exactPage, static_cast<float>(totalPages)) + totalPages, static_cast<float>(totalPages));
                float progress = totalPages > 1 ? looped / static_cast<float>(totalPages - 1) : 0.f;
                if (progress > 1.f) progress = 1.f;
                float const thumbW = std::min(m_fields->m_sliderBgWidth,
                    std::max(m_fields->m_sliderBgWidth / totalPages, 10.f));
                auto* input = m_fields->m_pageSliderInput.data();
                if (input) {
                    input->setThumbState(thumbW, progress);
                    if (input->isDragging()) progress = input->progress();
                }
                float maxTravel = m_fields->m_sliderBgWidth - thumbW;
                float targetX = m_fields->m_sliderLeftX + thumbW * 0.5f + progress * maxTravel;
                auto* thumb = m_fields->m_pageSliderThumb.data();
                thumb->setScaleX(thumbW / m_fields->m_sliderThumbWidth);
                float curX = thumb->getPositionX();
                float newX;
                if ((input && input->isDragging()) || std::fabs(targetX - curX) > maxTravel * 0.5f) {
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

        if (!m_fields->m_bgSprite || !m_fields->m_cachedDynamicSong) return;
        if (++m_fields->m_meteringFrameCounter < 3) return;
        m_fields->m_meteringFrameCounter = 0;

        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return;

        FMOD::ChannelGroup* masterGroup = nullptr;
        engine->m_system->getMasterChannelGroup(&masterGroup);
        if (!masterGroup) return;

        FMOD::DSP* headDSP = nullptr;
        masterGroup->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &headDSP);
        if (!headDSP) return;

        if (!m_fields->m_meteringEnabled) {
            headDSP->setMeteringEnabled(false, true);
            m_fields->m_meteringEnabled = true;
        }

        FMOD_DSP_METERING_INFO meteringInfo = {};
        headDSP->getMeteringInfo(nullptr, &meteringInfo);

        float peak = 0.f;
        int const chCount = std::clamp<int>(meteringInfo.numchannels, 0, 32);
        for (int i = 0; i < chCount; i++) {
            if (meteringInfo.peaklevel[i] > peak) peak = meteringInfo.peaklevel[i];
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

        m_fields->m_bgSprite->setColor({cVal, cVal, cVal});

        if (m_fields->m_sharpBgSprite) {
            GLubyte sharpVal = static_cast<GLubyte>(cVal * 0.9f);
            m_fields->m_sharpBgSprite->setColor({sharpVal, sharpVal, sharpVal});
        }
    }
    

    void updateThumbnailBackground(int levelID) {
        if (levelID == m_fields->m_appliedLevelID && m_fields->m_bgSprite) return;

        if (levelID <= 0) {
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

    // fade an outgoing snapshot over the incoming background
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

        // snapshot the outgoing bg except on first apply
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
        m_fields->m_sharpBgSprite = ensureBgSprite(m_fields->m_sharpBgSprite.data(), tex, texSize, -11, true);

        if (blurTex && m_fields->m_transitionFinished) {
            auto blurSize = blurTex->getContentSize();
            m_fields->m_bgSprite = ensureBgSprite(m_fields->m_bgSprite.data(), blurTex, blurSize, -10, false);
        } else if (m_fields->m_bgSprite) {
            m_fields->m_bgSprite->setVisible(false);
        }

        // hide vanilla bg only after a real thumbnail is visible
        if (tex && m_fields->m_transitionFinished) {
            this->hideVanillaBackgroundWithFade();
        }
    }

    // creates the fill sprite on first use, otherwise swaps its texture;
    // both background layers share create/position/action/tail handling.
    CCSprite* ensureBgSprite(CCSprite* existing, CCTexture2D* tex, CCSize const& texSize, float zOrder, bool dimmed) {
        if (!tex || texSize.width <= 0.f || texSize.height <= 0.f) return existing;
        auto win = CCDirector::get()->getWinSize();
        float scale = std::max(win.width / texSize.width, win.height / texSize.height);
        CCSprite* spr = existing;
        if (!spr) {
            spr = CCSprite::createWithTexture(tex);
            if (!spr) return nullptr;
            spr->setPosition(win / 2);
            spr->setZOrder(zOrder);
            if (dimmed) spr->setColor({80, 80, 80});
            this->addChild(spr);
            spr->runAction(CCRepeatForever::create(CCSequence::create(
                CCScaleTo::create(10.0f, scale * 1.3f),
                CCScaleTo::create(10.0f, scale),
                nullptr
            )));
        } else {
            spr->setTexture(tex);
            spr->setTextureRect(CCRect{0, 0, texSize.width, texSize.height});
        }
        spr->stopActionByTag(kBgFadeActionTag);
        spr->setScale(scale);
        spr->setVisible(true);
        spr->setOpacity(255);
        return spr;
    }
    // fade vanilla bg nodes without touching ours or other mods'
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

    // ease our ui out with gd's outgoing scene transition
    void animateExit() {
        if (m_fields->m_exitAnimated) return;
        m_fields->m_exitAnimated = true;

        if (auto* input = m_fields->m_pageSliderInput.data()) {
            input->cancelDrag();
            input->setTouchEnabled(false);
        }

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
