#include <Geode/Geode.hpp>
#include <Geode/modify/CustomSongWidget.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace geode::prelude;
using namespace cocos2d;

class $modify(PaimonCustomSongWidget, CustomSongWidget) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("CustomSongWidget::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCClippingNode> m_clipper  = nullptr;
        int                 m_levelID  = 0;
        bool                m_bgSwapped = false;
        bool                m_retryScheduled = false;
        paimon::SubscriptionHandle m_bgEventHandle = 0;
    };

    LevelInfoLayer* findLevelInfoLayer() {
        for (auto* node = this->getParent(); node; node = node->getParent()) {
            if (auto* lil = typeinfo_cast<LevelInfoLayer*>(node)) {
                return lil;
            }
        }
        return nullptr;
    }

    bool shouldManageBlur() {
        if (m_isMusicLibrary || m_isInCell) {
            return false;
        }
        return findLevelInfoLayer() != nullptr;
    }

    // Crea clipper redondeado
    CCClippingNode* buildClipper(CCSize const& sz) {
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(sz.width, sz.height);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(sz);
        clip->setID("paimon-song-clip"_spr);
        return clip;
    }

    // Reemplaza fondo GD con clipper
    bool swapBg() {
        if (m_fields->m_clipper && m_fields->m_clipper->getParent() == this) {
            m_fields->m_bgSwapped = true;
            return true;
        }
        if (m_fields->m_bgSwapped && !m_fields->m_clipper) {
            m_fields->m_bgSwapped = false;
        }
        if (m_fields->m_bgSwapped) return true;
        // Fallback a nodo con ID "bg"
        CCNode* bgNode = m_bgSpr;
        if (!bgNode) bgNode = this->getChildByID("bg");
        if (!bgNode) {
            log::debug("[PaimonCSW] swapBg: no bg node found");
            return false;
        }
        m_fields->m_bgSwapped = true;

        CCSize  bgSz     = bgNode->getScaledContentSize();
        CCPoint bgPos    = bgNode->getPosition();
        CCPoint bgAnchor = bgNode->getAnchorPoint();
        int     bgZ      = bgNode->getZOrder();

        log::info("[PaimonCSW] swapBg: pos({},{}) anchor({},{}) size {}x{} (unscaled {}x{}) z={}",
            bgPos.x, bgPos.y, bgAnchor.x, bgAnchor.y,
            bgSz.width, bgSz.height,
            bgNode->getContentSize().width, bgNode->getContentSize().height,
            bgZ);

        if (bgSz.width < 5.f || bgSz.height < 5.f) {
            log::debug("[PaimonCSW] swapBg: bg size too small, waiting");
            m_fields->m_bgSwapped = false;
            return false;
        }

        bgNode->setVisible(false);

        auto clip = buildClipper(bgSz);
        clip->setAnchorPoint(bgAnchor);
        clip->setPosition(bgPos);

        // Dark fallback semitransparente
        auto fallback = paimon::SpriteHelper::createDarkPanel(bgSz.width, bgSz.height, 160, 6.f);
        fallback->setAnchorPoint({0.f, 0.f});
        fallback->setPosition({0.f, 0.f});
        fallback->setID("paimon-song-dark-fallback"_spr);
        clip->addChild(fallback, 0);

        this->addChild(clip, bgZ);
        m_fields->m_clipper = clip;

        log::info("[PaimonCSW] swapBg: clipper created & added OK");
        return true;
    }

    bool ensureClipper() {
        if (m_fields->m_clipper && m_fields->m_clipper->getParent() == this) {
            return true;
        }
        if (m_fields->m_clipper && m_fields->m_clipper->getParent() != this) {
            m_fields->m_clipper = nullptr;
            m_fields->m_bgSwapped = false;
        }
        return swapBg();
    }

    // Aplica blur al thumbnail
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

        auto blurred = BlurSystem::getInstance()->createPaimonBlurSprite(texture, sz, 6.0f);
        if (!blurred) {
            log::warn("[PaimonCSW] createBlurredSprite returned null (size {}x{})",
                sz.width, sz.height);
            return;
        }

        float sx = sz.width  / blurred->getContentSize().width;
        float sy = sz.height / blurred->getContentSize().height;
        blurred->setScale(std::max(sx, sy));
        blurred->setAnchorPoint({0.5f, 0.5f});
        blurred->setPosition(sz / 2);
        blurred->setID("paimon-song-blur"_spr);

        // Crossfade entre blurs
        if (auto old = m_fields->m_clipper->getChildByID("paimon-song-blur"_spr)) {
            old->setID("paimon-song-blur-old"_spr);
            old->runAction(CCSequence::create(
                CCFadeOut::create(0.3f),
                CCRemoveSelf::create(),
                nullptr
            ));
        }

        blurred->setOpacity(0);
        m_fields->m_clipper->addChild(blurred, -1);
        blurred->runAction(CCFadeTo::create(0.3f, 230));

        log::info("[PaimonCSW] blur applied! size={}x{} scale={:.2f}",
            sz.width, sz.height, std::max(sx, sy));
    }

    // Busca nivel en la jerarquia
    GJGameLevel* findLevel() {
        if (auto* lil = findLevelInfoLayer()) {
            return lil->m_level;
        }
        if (auto* pl = PlayLayer::get()) {
            log::info("[PaimonCSW] findLevel: using PlayLayer fallback");
            return pl->m_level;
        }
        log::debug("[PaimonCSW] findLevel: no owning level context yet");
        return nullptr;
    }

    // Solicita carga de thumbnail
    void tryApplyBlur() {
        if (!shouldManageBlur()) {
            return;
        }

        if (!ensureClipper()) {
            log::debug("[PaimonCSW] tryApplyBlur: clipper not ready yet");
            return;
        }

        auto* level = findLevel();
        if (!level) return;

        int levelID = level->m_levelID.value();
        if (levelID <= 0) {
            log::warn("[PaimonCSW] tryApplyBlur: invalid levelID={}", levelID);
            return;
        }
        if (levelID == m_fields->m_levelID) return; // already in-flight or done
        m_fields->m_levelID = levelID;

        log::info("[PaimonCSW] tryApplyBlur: requesting thumbnail for levelID={}", levelID);

        // Intenta cache RAM primero
        auto ramTex = paimon::cache::ThumbnailCache::get().getFromRam(levelID, false);
        if (ramTex.has_value() && ramTex.value()) {
            log::info("[PaimonCSW] RAM cache HIT for {}", levelID);
            applyBlurredThumbnail(ramTex.value());
            return;
        }

        log::info("[PaimonCSW] RAM cache miss, starting async load for {}", levelID);

        WeakRef<PaimonCustomSongWidget> weakSelf = this;
        ThumbnailLoader::get().requestLoad(
            levelID,
            fmt::format("{}.png", levelID),
            [weakSelf, levelID](CCTexture2D* tex, bool ok) {
                auto selfRef = weakSelf.lock();
                auto* w = static_cast<PaimonCustomSongWidget*>(selfRef.data());
                if (!w || !w->getParent()) {
                    log::warn("[PaimonCSW] callback: widget no longer in scene");
                    return;
                }
                if (w->m_fields->m_levelID != levelID) {
                    log::warn("[PaimonCSW] callback: levelID mismatch ({} vs {})",
                        levelID, w->m_fields->m_levelID);
                    return;
                }
                if (ok && tex) {
                    log::info("[PaimonCSW] async load OK for {}", levelID);
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

    // Reintento programado si onEnter fallo
    void retryBlur(float) {
        m_fields->m_retryScheduled = false;
        if (m_fields->m_levelID > 0) return; // already resolved
        if (!shouldManageBlur()) return;
        log::info("[PaimonCSW] retryBlur: trying again...");
        tryApplyBlur();
    }

    // Hooks
    $override
    bool init(SongInfoObject* songInfo, CustomSongDelegate* delegate,
              bool showSongSelect, bool showPlayMusic, bool showDownload,
              bool isRobtopSong, bool unkBool, bool isMusicLibrary, int unk)
    {
        if (!CustomSongWidget::init(songInfo, delegate, showSongSelect,
                                     showPlayMusic, showDownload,
                                     isRobtopSong, unkBool, isMusicLibrary, unk))
            return false;

        if (isMusicLibrary || m_isInCell) {
            log::debug("[PaimonCSW] init: skipping non-target widget (isMusicLibrary={}, isInCell={})",
                isMusicLibrary, m_isInCell);
            return true;
        }

        log::info("[PaimonCSW] init: m_bgSpr={} widgetSize={}x{} isRobtopSong={} isMusicLibrary={}",
            (void*)m_bgSpr, getContentSize().width, getContentSize().height,
            isRobtopSong, isMusicLibrary);

        // Suscribe a evento de cambio de thumbnail
        if (m_fields->m_bgEventHandle == 0) {
            WeakRef<PaimonCustomSongWidget> weakSelf = this;
            m_fields->m_bgEventHandle = paimon::EventBus::get().subscribe<paimon::ThumbnailBackgroundChangedEvent>(
                [weakSelf](paimon::ThumbnailBackgroundChangedEvent const& e) {
                    log::info("[PaimonCSW] event received: levelID={} tex={}", e.levelID, (void*)e.texture);
                    auto ref = weakSelf.lock();
                    auto* w = static_cast<PaimonCustomSongWidget*>(ref.data());
                    if (!w) { log::warn("[PaimonCSW] event: weakRef dead"); return; }
                    if (!w->getParent()) { log::warn("[PaimonCSW] event: no parent"); return; }
                    if (!e.texture || e.levelID <= 0) { log::warn("[PaimonCSW] event: bad payload"); return; }

                    // Resuelve levelID desde evento si es necesario
                    if (w->m_fields->m_levelID <= 0) {
                        auto* level = w->findLevel();
                        if (level && level->m_levelID.value() == e.levelID) {
                            w->m_fields->m_levelID = e.levelID;
                            log::info("[PaimonCSW] late levelID resolve from event: {}", e.levelID);
                        } else {
                            log::warn("[PaimonCSW] event: cannot resolve levelID");
                            return;
                        }
                    }

                    if (w->m_fields->m_levelID != e.levelID) return;

                    log::info("[PaimonCSW] bg sync event for levelID={}, applying blur...", e.levelID);
                    w->applyBlurredThumbnail(e.texture);
                });
            log::info("[PaimonCSW] subscribed to ThumbnailBackgroundChangedEvent, handle={}", m_fields->m_bgEventHandle);
        }

        return true;
    }

    $override
    void onEnter() {
        CustomSongWidget::onEnter();

        if (!shouldManageBlur()) return;

        log::info("[PaimonCSW] onEnter");

        ensureClipper();

        // Oculta bg si fue re-mostrandose
        if (m_fields->m_bgSwapped) {
            if (m_bgSpr) m_bgSpr->setVisible(false);
            if (auto* bgById = this->getChildByID("bg")) bgById->setVisible(false);
        }

        tryApplyBlur();

        // Reintento si findLevel fallo
        if (m_fields->m_levelID <= 0 && !m_fields->m_retryScheduled) {
            m_fields->m_retryScheduled = true;
            this->scheduleOnce(
                schedule_selector(PaimonCustomSongWidget::retryBlur), 0.1f);
        }
    }

    $override
    void onExit() {
        // Limpia suscripcion en onExit
        if (m_fields->m_bgEventHandle != 0) {
            paimon::EventBus::get().unsubscribe(m_fields->m_bgEventHandle);
            m_fields->m_bgEventHandle = 0;
        }
        CustomSongWidget::onExit();
    }

    $override
    void songStateChanged() {
        // Guard: evita crash si m_customSongCell esta corrupto
        if (!m_songInfoObject || !this->getParent() || m_isMusicLibrary) {
            return;
        }

        CustomSongWidget::songStateChanged();
    }

    $override
    void updateSongInfo() {
        CustomSongWidget::updateSongInfo();

        if (!shouldManageBlur()) return;

        ensureClipper();

        // Oculta bg si fue re-creado
        if (m_fields->m_bgSwapped) {
            if (m_bgSpr) m_bgSpr->setVisible(false);
            if (auto* bgById = this->getChildByID("bg")) bgById->setVisible(false);
        }

        tryApplyBlur();
    }
};

