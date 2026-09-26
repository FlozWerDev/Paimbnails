#include <Geode/Geode.hpp>
#include <Geode/modify/CustomSongWidget.hpp>
#include "../framework/HookConventions.hpp"
#include "../utils/EditorContext.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include "CustomSongWidgetLifecycle.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <string_view>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace geode::prelude;
using namespace cocos2d;

namespace {
// Vanilla frame kept; blur fills inner area.
constexpr float kPlateBorderInset = 2.5f;
constexpr float kPlateInnerRadius = 5.f;
}

class $modify(PaimonCustomSongWidget, CustomSongWidget) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "CustomSongWidget::init");
        // After node-ids, without claiming Last.
        paimon::hooks::afterModOrElseNodeIdsLate(
            self, "CustomSongWidget::updateSongInfo", "prevter.compact-pause-menu"
        );
    }

    struct Fields {
        Ref<CCClippingNode> m_clipper  = nullptr;
        int                 m_levelID  = 0;
        bool                m_clipperBuilt = false;
        bool                m_retryScheduled = false;
        paimon::SubscriptionHandle m_bgEventHandle = 0;
        CCSize              m_clipSize = {0, 0};
        CCPoint             m_clipPos  = {0, 0};
        uint32_t            m_callbackGeneration = 0;
        CustomSongWidget*   m_owner = nullptr;

        ~Fields() {
            if (!m_owner) return;
            // teardown runs after release; no widget access here
            if (paimon::isRuntimeShuttingDown()) {
                m_owner = nullptr;
                return;
            }
            ++m_callbackGeneration;
            m_levelID = 0;
            if (m_bgEventHandle != 0) {
                paimon::EventBus::get().unsubscribe(m_bgEventHandle);
                m_bgEventHandle = 0;
            }
            paimon::csw::Lifecycle::unregisterWidget(m_owner);
            m_owner = nullptr;
        }
    };

    CustomSongWidget* asBase() {
        return static_cast<CustomSongWidget*>(static_cast<PaimonCustomSongWidget*>(this));
    }

    bool isUnderEditorHierarchy() {
        for (auto* node = this->getParent(); node; node = node->getParent()) {
            if (typeinfo_cast<LevelEditorLayer*>(node)) return true;
            if (typeinfo_cast<EditorUI*>(node)) return true;
            if (typeinfo_cast<CustomSongLayer*>(node)) return true;
        }
        return false;
    }

    bool isPassthroughMode() {
        return paimon::isEditorScene() || m_isMusicLibrary || m_isInCell || isUnderEditorHierarchy();
    }

    LevelInfoLayer* findLevelInfoLayer() {
        for (auto* node = this->getParent(); node; node = node->getParent()) {
            if (auto* lil = typeinfo_cast<LevelInfoLayer*>(node)) {
                return lil;
            }
        }
        return nullptr;
    }

    bool shouldManageBlur() {
        if (isPassthroughMode()) return false;
        return findLevelInfoLayer() != nullptr;
    }

    int resolveSongID() const {
        if (m_customSongID > 0) return m_customSongID;
        if (m_songInfoObject) return m_songInfoObject->m_songID;
        return 0;
    }

    bool hasPlaceholderSongLabels() const {
        if (!m_songLabel || !m_artistLabel) return false;
        auto const title = std::string_view(m_songLabel->getString());
        auto const artist = std::string_view(m_artistLabel->getString());
        if (title == "SONG TITLE" || artist == "ARTIST NAME") return true;
        if (resolveSongID() <= 0) return false;
        if (m_songInfoObject) {
            return m_songInfoObject->m_songName.empty() || m_songInfoObject->m_artistName.empty();
        }
        return title.empty() || artist.empty();
    }

    void refreshSongLabelsFromCache() {
        if (!m_songLabel || !m_artistLabel) return;
        int const songID = resolveSongID();
        if (songID <= 0) return;

        auto* mdm = MusicDownloadManager::sharedState();
        if (!mdm) return;

        auto* info = mdm->getSongInfoObject(songID);
        if (!info) return;

        if (!info->m_songName.empty()) {
            m_songLabel->setString(info->m_songName.c_str());
        }
        if (!info->m_artistName.empty()) {
            m_artistLabel->setString(info->m_artistName.c_str());
        }
    }

    void requestSongMetadataIfNeeded() {
        if (!hasPlaceholderSongLabels()) return;
        refreshSongLabelsFromCache();
        if (!hasPlaceholderSongLabels()) return;
        getSongInfoIfUnloaded();
    }

    CCClippingNode* buildClipper(CCSize const& sz) {
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(
            sz.width, sz.height, kPlateInnerRadius);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(sz);
        clip->setID("paimon-song-clip"_spr);
        return clip;
    }

    // m_bgSpr is recreated; verify it is still mounted.
    bool isValidChild(CCNode* child) {
        if (!child) return false;
        auto* children = this->getChildren();
        if (!children) return false;
        int count = children->count();
        for (int i = 0; i < count; ++i) {
            if (children->objectAtIndex(i) == static_cast<CCObject*>(child)) return true;
        }
        return false;
    }

    bool swapBg() {
        if (m_fields->m_clipper && m_fields->m_clipper->getParent() == this) {
            m_fields->m_clipperBuilt = true;
            return true;
        }
        if (m_fields->m_clipperBuilt && !m_fields->m_clipper) {
            m_fields->m_clipperBuilt = false;
        }
        if (m_fields->m_clipperBuilt) return true;
        CCNode* bgNode = (m_bgSpr && isValidChild(m_bgSpr)) ? m_bgSpr : nullptr;
        if (!bgNode) bgNode = this->getChildByID("bg");
        if (!bgNode) {
            log::debug("[PaimonCSW] swapBg: no bg node found");
            return false;
        }
        m_fields->m_clipperBuilt = true;

        CCSize  bgSz     = bgNode->getScaledContentSize();
        CCPoint bgPos    = bgNode->getPosition();
        CCPoint bgAnchor = bgNode->getAnchorPoint();
        int     bgZ      = bgNode->getZOrder();

        if (bgSz.width < 5.f || bgSz.height < 5.f) {
            log::debug("[PaimonCSW] swapBg: bg size too small, waiting");
            m_fields->m_clipperBuilt = false;
            return false;
        }

        CCPoint bgOrigin = {
            bgPos.x - bgAnchor.x * bgSz.width,
            bgPos.y - bgAnchor.y * bgSz.height
        };

        m_fields->m_clipSize = CCSize(
            std::max(8.f, bgSz.width  - kPlateBorderInset * 2.f),
            std::max(8.f, bgSz.height - kPlateBorderInset * 2.f)
        );
        m_fields->m_clipPos = CCPoint(
            bgOrigin.x + kPlateBorderInset,
            bgOrigin.y + kPlateBorderInset
        );

        auto clip = buildClipper(m_fields->m_clipSize);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setPosition(m_fields->m_clipPos);

        this->addChild(clip, bgZ);
        m_fields->m_clipper = clip;
        return true;
    }

    bool ensureClipper() {
        if (m_fields->m_clipper) {
            if (m_fields->m_clipper->getParent() == this) return true;
            m_fields->m_clipper = nullptr;
            m_fields->m_clipperBuilt = false;
        }
        return swapBg();
    }

    void attachBlurredSprite(CCSprite* blurred, CCSize const& sz) {
        if (!blurred || !m_fields->m_clipper) return;

        float sx = sz.width / blurred->getContentSize().width;
        float sy = sz.height / blurred->getContentSize().height;
        blurred->setScale(std::max(sx, sy));
        blurred->setAnchorPoint({0.5f, 0.5f});
        blurred->setPosition(sz / 2);
        blurred->setID("paimon-song-blur"_spr);

        if (auto old = m_fields->m_clipper->getChildByID("paimon-song-blur"_spr)) {
            old->setID("paimon-song-blur-old"_spr);
            old->runAction(CCSequence::create(
                CCFadeOut::create(0.3f),
                CCRemoveSelf::create(),
                nullptr
            ));
        }

        blurred->setOpacity(0);
        m_fields->m_clipper->addChild(blurred, 1);
        blurred->runAction(CCFadeTo::create(0.3f, 255));

        // keep text readable
        if (!m_fields->m_clipper->getChildByID("paimon-song-dark-overlay"_spr)) {
            auto* dark = CCLayerColor::create(ccc4(0, 0, 0, 110));
            if (dark) {
                dark->setContentSize(sz);
                dark->setAnchorPoint({0.f, 0.f});
                dark->setPosition({0.f, 0.f});
                dark->setID("paimon-song-dark-overlay"_spr);
                m_fields->m_clipper->addChild(dark, 2);
            }
        }
    }

    // async blur to avoid stalling a frame
    void applyBlurredThumbnail(CCTexture2D* texture) {
        if (!texture) {
            log::warn("[PaimonCSW] applyBlur: texture is null");
            return;
        }

        if (!ensureClipper()) {
            log::warn("[PaimonCSW] applyBlur: clipper not available");
            return;
        }

        CCSize sz = m_fields->m_clipper->getContentSize();

        auto* widget = asBase();
        uint32_t const generation = m_fields->m_callbackGeneration;

        BlurSystem::getInstance()->buildPaimonBlurAsync(
            texture,
            sz,
            6.0f,
            std::string{},
            [widget, generation, sz](CCSprite* blurred) {
                if (!paimon::csw::Lifecycle::isAlive(widget)) return;
                auto* w = static_cast<PaimonCustomSongWidget*>(widget);
                if (!w->getParent() || !w->m_fields->m_clipper) return;
                if (w->m_fields->m_callbackGeneration != generation) return;
                if (!blurred) {
                    log::warn("[PaimonCSW] async blur returned null");
                    return;
                }
                w->attachBlurredSprite(blurred, sz);
            });
    }

    GJGameLevel* findLevel() {
        if (auto* lil = findLevelInfoLayer()) {
            return lil->m_level;
        }
        if (auto* pl = PlayLayer::get()) {
            return pl->m_level;
        }
        return nullptr;
    }

    void tryApplyBlur() {
        if (!shouldManageBlur()) return;

        if (!ensureClipper()) return;

        auto* level = findLevel();
        if (!level) return;

        int levelID = level->m_levelID.value();
        if (levelID <= 0) {
            log::warn("[PaimonCSW] tryApplyBlur: invalid levelID={}", levelID);
            return;
        }

        bool const alreadyBound = (levelID == m_fields->m_levelID);
        bool const hasBlur = m_fields->m_clipper
            && m_fields->m_clipper->getChildByID("paimon-song-blur"_spr) != nullptr;
        if (alreadyBound && hasBlur) return;
        m_fields->m_levelID = levelID;

        // reuse LevelInfoLayer texture to stay in sync
        if (paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID == levelID) {
            if (auto* lastTex = paimon::ThumbnailBackgroundChangedEvent::getLastTexture()) {
                applyBlurredThumbnail(lastTex);
                return;
            }
        }

        auto ramTex = paimon::cache::ThumbnailCache::get().getFromRam(levelID, false);
        if (ramTex.has_value() && ramTex.value()) {
            applyBlurredThumbnail(ramTex.value());
            return;
        }

        auto* widget = asBase();
        uint32_t const generation = m_fields->m_callbackGeneration;
        ThumbnailLoader::get().requestLoad(
            levelID,
            fmt::format("{}.png", levelID),
            [widget, generation, levelID](CCTexture2D* tex, bool ok) {
                if (!paimon::csw::Lifecycle::isAlive(widget)) {
                    log::warn("[PaimonCSW] callback: widget no longer registered");
                    return;
                }
                auto* w = static_cast<PaimonCustomSongWidget*>(widget);
                if (!w->getParent()) {
                    log::warn("[PaimonCSW] callback: widget no longer in scene");
                    return;
                }
                if (w->m_fields->m_callbackGeneration != generation) {
                    log::warn("[PaimonCSW] callback: stale generation");
                    return;
                }
                if (w->m_fields->m_levelID != levelID) {
                    log::warn("[PaimonCSW] callback: levelID mismatch ({} vs {})",
                        levelID, w->m_fields->m_levelID);
                    return;
                }
                if (ok && tex) {
                    w->applyBlurredThumbnail(tex);
                } else {
                    log::warn("[PaimonCSW] async load FAILED for {} (ok={} tex={})",
                        levelID, ok, (void*)tex);
                }
            },
            ThumbnailLoader::PriorityVisiblePrefetch,
            false
        );
    }

    void retryBlur(float) {
        m_fields->m_retryScheduled = false;
        if (!paimon::csw::Lifecycle::isAlive(asBase())) return;
        if (!getParent()) return;
        if (!shouldManageBlur()) return;
        bool const hasBlur = m_fields->m_clipper
            && m_fields->m_clipper->getChildByID("paimon-song-blur"_spr) != nullptr;
        if (m_fields->m_levelID > 0 && hasBlur) return;
        tryApplyBlur();
    }

    $override
    bool init(SongInfoObject* songInfo, CustomSongDelegate* delegate,
              bool showSongSelect, bool showPlayMusic, bool showDownload,
              bool isRobtopSong, bool unkBool, bool isMusicLibrary, int unk)
    {
        if (!CustomSongWidget::init(songInfo, delegate, showSongSelect,
                                     showPlayMusic, showDownload,
                                     isRobtopSong, unkBool, isMusicLibrary, unk))
            return false;

        if (isPassthroughMode()) {
            return true;
        }

        m_fields.self();
        m_fields->m_owner = asBase();
        paimon::csw::Lifecycle::registerWidget(m_fields->m_owner);

        log::info("[PaimonCSW] init: m_bgSpr={} widgetSize={}x{} isRobtopSong={} isMusicLibrary={}",
            (void*)m_bgSpr, getContentSize().width, getContentSize().height,
            isRobtopSong, isMusicLibrary);

        if (m_fields->m_bgEventHandle == 0) {
            auto* widget = asBase();
            m_fields->m_bgEventHandle = paimon::EventBus::get().subscribe<paimon::ThumbnailBackgroundChangedEvent>(
                [widget](paimon::ThumbnailBackgroundChangedEvent const& e) {
                    if (!paimon::csw::Lifecycle::isAlive(widget)) {
                        log::warn("[PaimonCSW] event: widget dead");
                        return;
                    }
                    auto* w = static_cast<PaimonCustomSongWidget*>(widget);
                    if (!w->getParent()) { log::warn("[PaimonCSW] event: no parent"); return; }
                    if (paimon::csw::Lifecycle::shouldSkipDelegateCall(widget)) return;
                    if (!e.texture || e.levelID <= 0) { log::warn("[PaimonCSW] event: bad payload"); return; }

                    if (w->m_fields->m_levelID <= 0) {
                        auto* level = w->findLevel();
                        if (!level || level->m_levelID.value() != e.levelID) {
                            log::warn("[PaimonCSW] event: cannot resolve levelID");
                            return;
                        }
                        w->m_fields->m_levelID = e.levelID;
                    }

                    if (w->m_fields->m_levelID != e.levelID) return;

                    w->applyBlurredThumbnail(e.texture);
                });
        }

        return true;
    }

    $override
    void loadSongInfoFinished(SongInfoObject* object) {
        CustomSongWidget::loadSongInfoFinished(object);
        if (isPassthroughMode()) return;

        auto* widget = asBase();
        if (!paimon::csw::Lifecycle::isAlive(widget)) return;
        if (paimon::csw::Lifecycle::shouldSkipDelegateCall(widget)) return;

        refreshSongLabelsFromCache();
        if (shouldManageBlur()) {
            tryApplyBlur();
        }
    }

    $override
    void songStateChanged() {
        if (isPassthroughMode()) {
            CustomSongWidget::songStateChanged();
            return;
        }

        auto* widget = asBase();
        if (paimon::csw::Lifecycle::shouldSkipDelegateCall(widget)) {
            return;
        }

        CustomSongWidget::songStateChanged();

        if (!shouldManageBlur()) return;
        requestSongMetadataIfNeeded();
    }

    $override
    void updateSongInfo() {
        if (isPassthroughMode()) {
            CustomSongWidget::updateSongInfo();
            return;
        }

        // GD calls this from init() before parenting
        CustomSongWidget::updateSongInfo();

        auto* widget = asBase();
        if (!paimon::csw::Lifecycle::isAlive(widget)) return;
        if (paimon::csw::Lifecycle::shouldSkipDelegateCall(widget)) return;

        if (!shouldManageBlur()) return;
        requestSongMetadataIfNeeded();
        if (!this->getParent()) return;

        ensureClipper();

        if (m_fields->m_clipper && m_fields->m_clipSize.width > 0) {
            auto curSz = m_fields->m_clipper->getContentSize();
            if (curSz.width != m_fields->m_clipSize.width ||
                curSz.height != m_fields->m_clipSize.height) {
                m_fields->m_clipper->setContentSize(m_fields->m_clipSize);
                m_fields->m_clipper->setPosition(m_fields->m_clipPos);
                if (auto* blurSpr = m_fields->m_clipper->getChildByID("paimon-song-blur"_spr)) {
                    blurSpr->setPosition(m_fields->m_clipSize / 2);
                    float sx = m_fields->m_clipSize.width / blurSpr->getContentSize().width;
                    float sy = m_fields->m_clipSize.height / blurSpr->getContentSize().height;
                    blurSpr->setScale(std::max(sx, sy));
                }
            }
        }

        tryApplyBlur();

        // Retry lives here, not in onEnter().
        if (m_fields->m_levelID <= 0 && !m_fields->m_retryScheduled) {
            m_fields->m_retryScheduled = true;
            this->scheduleOnce(
                schedule_selector(PaimonCustomSongWidget::retryBlur), 0.1f);
        }
    }
};
