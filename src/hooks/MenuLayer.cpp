#include <Geode/modify/MenuLayer.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/utils/cocos.hpp>
#include "../framework/state/SessionState.hpp"
#include <Geode/utils/file.hpp>
#include "../features/pet/services/PetManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/volume-scroll/services/VolumeScrollManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../layers/PaiConfigLayer.hpp"
#include "../layers/PaimonHubLayer.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/DominantColors.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include "../utils/RetainedLazyTextureLoad.hpp"
#include "../utils/ShapeStencil.hpp"
#include "../core/Settings.hpp"
#include "../features/profiles/services/ProfilePicCustomizer.hpp"
#include "../features/profiles/services/ProfilePicRenderer.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../utils/AudioInterop.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/Localization.hpp"
#include "../video/VideoPlayer.hpp"
#include "../features/forum/services/ForumApi.hpp"
#include "../features/guide/services/PaimonGuideService.hpp"
#include "../features/guide/GuideEvents.hpp"
#include "../features/guide/ui/AnimatedPaimon.hpp"
#include "../features/guide/ui/PaimonGuideChatPopup.hpp"
#include "../utils/ThreadTracker.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <random>
#include <filesystem>
#include <string>

using namespace geode::prelude;

namespace {
    bool s_menuServicesInitialized = false;
    bool s_menuServicesScheduled = false;
}

// declared in PetHook.cpp; registers the pet ticker with the scheduler
extern void initPetTicker();

// declared in CursorHook.cpp; registers the cursor ticker with the scheduler
extern void initCursorTicker();

// declared in VolumeScrollHook.cpp; registers the volume-overlay ticker with the scheduler
extern void initVolumeScrollTicker();

static CCNode* buildProfileClipContainer(
    CCNode* imageNode,
    std::string const& /*shapeName*/,
    float targetSize,
    ProfilePicConfig const& picCfg
) {
    if (!imageNode) return nullptr;
    return paimon::profile_pic::composeProfilePicture(imageNode, targetSize, picCfg);
}

class $modify(PaimonMenuLayer, MenuLayer) {
    static void onModify(auto& self) {
        // MenuLayer uses node IDs and backgrounds from geode.node-ids
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    struct Fields {
        // Ref<> avoids dangling pointers
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCLayerColor> m_bgOverlay = nullptr;
        bool m_adaptiveColors = false;
        // Perf: cached hub button reference to avoid getChildByIDRecursive per frame.
        // WeakRef<>: if another mod removes the button, lock() returns null
        // instead of a dangling pointer (no use-after-free).
        WeakRef<CCNode> m_hubBtnCached;
        bool m_hubBtnSearched = false;
        // Perf: throttle adaptive colors to avoid per-frame tree traversal
        int m_adaptiveFrameCounter = 0;
        ccColor3B m_lastAdaptiveColor = {255, 255, 255};
        // Perf: throttle badge check
        int m_badgeFrameCounter = 0;
        // Async load of the static custom background. Decodes the image on a
        // background thread (LazySprite) so entering the menu doesn't stall the
        // frame. The retained loader cancels a previous load if the background
        // changes or the menu is left first.
        paimon::image::RetainedLazyTextureLoad m_bgStaticLoad;
    };

    // Apply adaptive colors
    void applyAdaptiveColor(ccColor3B color) {
        auto tintNode = [color](CCNode* node) {
             if (!node) return;
             if (auto btn = typeinfo_cast<ButtonSprite*>(node)) {
                 btn->setColor(color);
             } 
             else if (auto spr = typeinfo_cast<CCSprite*>(node)) {
                 spr->setColor(color);
             }
             else if (auto lbl = typeinfo_cast<CCLabelBMFont*>(node)) {
                 lbl->setColor(color);
             }
        };

        // Tint children of known menus
        static char const* menuIDs[] = {
            "main-menu", "profile-menu", "right-side-menu"
        };
        for (auto const& menuID : menuIDs) {
            if (auto menu = this->getChildByID(menuID)) {
                for (auto* btn : CCArrayExt<CCMenuItem*>(menu->getChildren())) {
                    if (!btn) continue;
                    for (auto* kid : CCArrayExt<CCNode*>(btn->getChildren())) {
                        tintNode(kid);
                    }
                }
            }
        }

        if (auto lbl = typeinfo_cast<CCLabelBMFont*>(this->getChildByID("player-username"))) {
            lbl->setColor(color);
        }
    } 

    // Builds the Paimon logo button (opens PaimonHubLayer). Shared by the
    // bottom-menu path and the own-menu fallback to avoid duplicating the
    // sprite/button construction.
    CCMenuItemSpriteExtra* createPaimonHubButton() {
        auto logoSpr = CCSprite::create("Logo.png"_spr);
        if (!logoSpr || logoSpr->isUsingFallback()) {
            logoSpr = CCSprite::create("paim_Paimon.png"_spr);
        }
        if (!logoSpr) return nullptr;

        auto bgBtn = CCScale9Sprite::create("GJ_button_01.png");
        bgBtn->setContentSize({44.f, 44.f});
        auto sz = logoSpr->getContentSize();
        float targetH = 46.f;
        logoSpr->setScale(sz.height > 0 ? targetH / sz.height : 0.36f);
        logoSpr->setPosition(bgBtn->getContentSize() / 2);
        bgBtn->addChild(logoSpr);

        auto btn = CCMenuItemSpriteExtra::create(bgBtn, this, menu_selector(PaimonMenuLayer::onPaimonHub));
        btn->setID("paimon-hub-btn"_spr);
        return btn;
    }

    $override
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }
        log::info("[MenuLayer] init");

        // Watchdog interop flags
        // Reset the "Paimon owns the audio" flags on returning to MenuLayer.
        // Without this watchdog, a rare error path (profile popup closed mid-
        // crossfade, video aborting between scenes, etc.) can leave a flag stuck
        // and block all game music until the client restarts. On MenuLayer entry
        // no Paimon manager should own the audio channel, so a stuck flag is a leak.
        {
            using namespace paimon;
            bool anyStuck =
                isProfileMusicInteropActive() ||
                isDynamicSongInteropActive()  ||
                isVideoAudioInteropActive();
            if (anyStuck) {
                log::warn("[InteropWatchdog] flags stuck on MenuLayer entry: profile={} dynamic={} video={} — clearing",
                          isProfileMusicInteropActive(),
                          isDynamicSongInteropActive(),
                          isVideoAudioInteropActive());
                setProfileMusicInteropActive(false);
                setDynamicSongInteropActive(false);
                setVideoAudioInteropActive(false);
            }
        }

        // Initialize MenuLayer-bound services after the first menu frame. The
        // global bootstrap is registered canonically with $on_game(Loaded).
        if (!s_menuServicesInitialized && !s_menuServicesScheduled) {
            s_menuServicesScheduled = true;
            this->scheduleOnce(schedule_selector(PaimonMenuLayer::deferredMenuServicesInit), 0.35f);
        }

        // Heartbeat: immediate ping at start + every 60s to keep online status
        paimon::forum::ForumApi::get().sendHeartbeat([](paimon::forum::Result<bool> result) {
            log::debug("[Heartbeat] Initial heartbeat sent: {}", result.ok && result.data);
        });
        this->schedule(schedule_selector(PaimonMenuLayer::tickHeartbeat), 60.f);

        // Clear list context on returning to the menu
        paimon::SessionState::get().currentListID = 0;

        // Schedule update for adaptive colors
        this->scheduleUpdate();

        // Reopen the verification popup if needed
        if (paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.reopenQueue)) {
            this->scheduleOnce(schedule_selector(PaimonMenuLayer::openVerificationQueue), 0.6f);
        }

        // Add the mod logo button to the bottom menu (opens PaimonHubLayer).
        // Check it doesn't already exist to avoid duplicates.
        if (!this->getChildByID("paimon-hub-btn"_spr) && !this->getChildByID("paimon-fallback-bottom-menu"_spr)) {
            if (auto bottomMenu = this->getChildByID("bottom-menu")) {
                if (auto btn = createPaimonHubButton()) {
                    bottomMenu->addChild(btn);
                    bottomMenu->updateLayout();
                }
            } else {
                // Fallback: own menu with RowLayout
                auto winSize = CCDirector::get()->getWinSize();
                auto menu = CCMenu::create();
                menu->setContentSize({winSize.width, 40.f});
                menu->setAnchorPoint({0.f, 0.f});
                menu->ignoreAnchorPointForPosition(false);
                menu->setPosition({0.f, 8.f});
                menu->setLayout(RowLayout::create()
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setGap(8.f));
                menu->setID("paimon-fallback-bottom-menu"_spr);

                if (auto btn = createPaimonHubButton()) {
                    menu->addChild(btn);
                    menu->updateLayout();
                }

                this->addChild(menu);
            }
        }

        this->updateBackground();
        this->updateProfileButton();

        // Paimon hidden behind the title (clickable)
        if (auto* title = this->getChildByID("main-title")) {
            auto paimonSpr = CCSprite::create("paim_Paimon.png"_spr);
            if (paimonSpr) {
                auto titleSize = title->getContentSize();
                auto titleScale = title->getScale();
                float titleW = titleSize.width * titleScale;
                float titleH = titleSize.height * titleScale;

                // Scale Paimon smaller than the title
                float paimonMaxH = titleH * 0.7f;
                float sprH = paimonSpr->getContentSize().height;
                if (sprH <= 0.f) sprH = 1.f;
                float paimonScale = paimonMaxH / sprH;
                paimonSpr->setScale(paimonScale);

                // Random position within the title
                static std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> distX(-titleW * 0.35f, titleW * 0.35f);
                std::uniform_real_distribution<float> distY(-titleH * 0.15f, titleH * 0.15f);
                std::uniform_real_distribution<float> distRot(-45.f, 45.f);

                auto titlePos = title->getPosition();
                float px = titlePos.x + distX(rng);
                float py = titlePos.y + distY(rng);
                float rot = distRot(rng);

                // Create clickable button
                auto paimonBtn = CCMenuItemSpriteExtra::create(
                    paimonSpr, this, menu_selector(PaimonMenuLayer::onPaimonClick));
                paimonBtn->setRotation(rot);
                paimonBtn->setID("paimon-hidden-btn"_spr);

                // Container menu for the button (easter egg)
                auto paimonMenu = CCMenu::create();
                paimonMenu->setPosition(ccp(px, py));
                paimonMenu->setContentSize(paimonSpr->getScaledContentSize());
                paimonMenu->setID("paimon-hidden-menu"_spr);
                paimonMenu->addChild(paimonBtn);

                bool guideOn = paimon::guide::PaimonGuideService::get().isEnabled();
                if (guideOn) {
                    paimonSpr->setOpacity(0);

                    // AnimatedPaimon uses the same scale as the static sprite to match.
                    auto* animated = paimon::guide::AnimatedPaimon::create(paimonScale);
                    if (animated) {
                        animated->setLively(true);
                        animated->play(paimon::guide::AnimatedPaimon::Animation::Idle);
                        animated->setRotation(rot);
                        // Center on the button (same origin as the sprite)
                        animated->setAnchorPoint({0.5f, 0.5f});
                        animated->setPosition(paimonBtn->getContentSize() * 0.5f);
                        animated->setID("paimon-hidden-animated"_spr);
                        paimonBtn->addChild(animated, 1);

                        WeakRef<paimon::guide::AnimatedPaimon> weakAnim(animated);
                        auto bubbleTick = CallFuncExt::create([weakAnim] {
                            if (auto anim = weakAnim.lock()) {
                                // 30% chance each tick
                                static std::mt19937 brng(std::random_device{}());
                                std::uniform_int_distribution<int> chance(0, 99);
                                if (chance(brng) < 30) {
                                    auto txt = Localization::get().getString("pai.guide.bubble");
                                    anim->showBubble(txt, 3.f);
                                }
                            }
                        });
                        animated->runAction(CCRepeatForever::create(
                            CCSequence::create(
                                CCDelayTime::create(30.f),
                                bubbleTick,
                                nullptr
                            )
                        ));
                    }
                } else {
                    paimonSpr->setOpacity(180);
                }

                // zOrder 1: behind the title
                this->addChild(paimonMenu, 1);
            }
        }

        return true;
    }

    $override
    void update(float dt) {
        MenuLayer::update(dt);
        
        if (m_fields->m_adaptiveColors && m_fields->m_bgSprite) {
             // Perf: only update adaptive colors every 4 frames (~15 FPS is enough for color sync)
             if (++m_fields->m_adaptiveFrameCounter >= 4) {
                 m_fields->m_adaptiveFrameCounter = 0;
                 if (auto gif = typeinfo_cast<AnimatedGIFSprite*>(static_cast<CCSprite*>(m_fields->m_bgSprite))) {
                     auto colors = gif->getCurrentFrameColors();
                     ccColor3B newColor = {colors.first.r, colors.first.g, colors.first.b};
                     // Only apply if color actually changed
                     if (newColor.r != m_fields->m_lastAdaptiveColor.r ||
                         newColor.g != m_fields->m_lastAdaptiveColor.g ||
                         newColor.b != m_fields->m_lastAdaptiveColor.b) {
                         m_fields->m_lastAdaptiveColor = newColor;
                         this->applyAdaptiveColor(newColor);
                     }
                 }
             }
        }

        // If the photo editor saved changes, refresh the profile button
        if (ProfilePicCustomizer::get().isDirty()) {
            ProfilePicCustomizer::get().setDirty(false);
            this->updateProfileButton();
        }

        // Perf: check badge only every 60 frames (~1 second at 60fps)
        if (++m_fields->m_badgeFrameCounter >= 60) {
            m_fields->m_badgeFrameCounter = 0;
            this->applyUpdateBadge();
        }

    }

    // Add/remove a red dot on the paimon-hub-btn based on UpdateChecker state. Idempotent.
    void applyUpdateBadge() {
        // Perf: cache the hub button reference instead of recursive search each call.
        // WeakRef<>::lock() returns null if the node was destroyed (safe).
        Ref<CCNode> btnRef;
        if (!m_fields->m_hubBtnSearched) {
            auto* found = this->getChildByIDRecursive("paimon-hub-btn"_spr);
            m_fields->m_hubBtnCached = found;
            m_fields->m_hubBtnSearched = true;
            btnRef = found;
        } else {
            btnRef = m_fields->m_hubBtnCached.lock();
        }
        auto* btn = btnRef.data();
        if (!btn || !btn->getParent()) {
            // Button may have been removed/re-added, retry next time
            m_fields->m_hubBtnSearched = false;
            return;
        }

        bool hasUpdate = paimon::updates::UpdateChecker::get().hasUpdate();
        auto existing = btn->getChildByID("paimon-hub-update-badge"_spr);

        if (hasUpdate && !existing) {
            // Red dot in the button's top-right corner
            auto badge = CCSprite::create("GJ_completesIcon_001.png");
            CCNode* badgeNode = nullptr;
            if (badge && !badge->isUsingFallback()) {
                badge->setColor({255, 60, 60});
                badgeNode = badge;
            }
            if (!badgeNode) {
                auto layer = CCLayerColor::create(ccc4(255, 60, 60, 255));
                layer->setContentSize({10.f, 10.f});
                layer->ignoreAnchorPointForPosition(false);
                layer->setAnchorPoint({0.5f, 0.5f});
                badgeNode = layer;
            } else {
                badge->setScale(0.45f);
            }
            badgeNode->setID("paimon-hub-update-badge"_spr);
            auto sz = btn->getContentSize();
            badgeNode->setPosition({sz.width - 4.f, sz.height - 4.f});
            btn->addChild(badgeNode, 100);

            // Visual pulse
            badgeNode->runAction(CCRepeatForever::create(CCSequence::create(
                CCScaleTo::create(0.4f, 0.55f),
                CCScaleTo::create(0.4f, 0.45f),
                nullptr
            )));
        } else if (!hasUpdate && existing) {
            existing->removeFromParent();
        }
    }

    void openVerificationQueue(float dt) {
        auto scene = VerificationCenterLayer::scene();
        if (scene) {
            TransitionManager::get().pushScene(scene);
        }
    }

    void deferredMenuServicesInit(float) {
        s_menuServicesScheduled = false;
        if (s_menuServicesInitialized) return;
        s_menuServicesInitialized = true;
        log::info("[PaimonThumbnails] Initializing MenuLayer-bound services");

        // VolumeScrollManager::init() only assigns primitives, no I/O. Safe on main.
        paimon::volscroll::VolumeScrollManager::get().init();

        // PERF: tickers are CCNodes that run regardless of config (they check
        // enabled internally each frame). Register them now so we don't wait on
        // the worker thread; if config ends up disabled, the ticker does no
        // useful work (negligible cost).
        initPetTicker();
        initCursorTicker();
        initVolumeScrollTicker();

        // PERF: Pet/Cursor::init() called loadConfig() doing synchronous
        // file::readString (~1-10ms on HDDs / aggressive AV), which blocked the
        // main thread during deferredMenuServicesInit (~0.35s after MenuLayer::init).
        // Now the I/O runs in the background; when done, setSettingValue and
        // applyConfigLive run on the main thread (Geode settings aren't thread-safe).
        paimon::ThreadTracker::get().spawn([]() {
            geode::utils::thread::setName("PaimonPetCursorLoad");
            if (paimon::isRuntimeShuttingDown()) return;

            // Disk I/O (file::readString + matjson::parse) on the worker thread
            PetManager::get().loadConfig();
            if (paimon::isRuntimeShuttingDown()) return;
            CursorManager::get().loadConfig();

            if (paimon::isRuntimeShuttingDown()) return;

            // Back to the main thread for Geode settings and cocos2d nodes
            geode::Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;

                // PetManager: srand with no critical ordering (a different seed
                // per session is what we want). Keep it on main to avoid racing
                // early std::rand use in the ticker.
                std::srand(static_cast<unsigned int>(std::time(nullptr)));

                // CursorManager: equivalent to init() except loadConfig already
                // ran on the worker. Push config -> Geode settings and apply.
                {
                    auto& cm = CursorManager::get();
                    Mod::get()->setSettingValue<bool>("custom-cursor-enable", cm.config().enabled);
                    cm.applyConfigLive();
                }

                // PetManager: applyConfigLive is a no-op if m_petNode doesn't
                // exist yet (normal at startup). When the ticker creates the node,
                // it picks up the loaded config.
                PetManager::get().applyConfigLive();

                log::info("[PaimonThumbnails] Pet/Cursor config loaded and applied");
            });
        });
    }

    void tickHeartbeat(float dt) {
        paimon::forum::ForumApi::get().sendHeartbeat([](paimon::forum::Result<bool> result) {
            // Quiet: debug log only
            if (!result.ok || !result.data) {
                log::debug("[Heartbeat] Forum server heartbeat failed or offline");
            }
        });
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonMenuLayer::tickHeartbeat));
        this->unschedule(schedule_selector(PaimonMenuLayer::deferredMenuServicesInit));
        if (!s_menuServicesInitialized) s_menuServicesScheduled = false;
        this->unschedule(schedule_selector(PaimonMenuLayer::openVerificationQueue));
        this->unscheduleUpdate();
        if (m_fields->m_bgSprite) {
            if (auto* shaderSpr = typeinfo_cast<Shaders::ShaderBgSprite*>(m_fields->m_bgSprite.data())) {
                shaderSpr->unschedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));
            }
        }
        m_fields->m_bgStaticLoad.reset();
        MenuLayer::onExit();
    }

    void onBackgroundConfig(CCObject*) {
        TransitionManager::get().pushScene(PaiConfigLayer::scene());
    }

    void onPaimonHub(CCObject*) {
        auto scene = PaimonHubLayer::scene();
        CCDirector::get()->replaceScene(scene);
    }

    void onPaimonClick(CCObject* sender) {
        auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!btn) return;

        // If guide mode is active, open Paimon's chat instead of the explosion
        // easter egg, so the hidden sprite becomes an interactive guide without
        // losing its original role when the user doesn't want the guide.
        if (paimon::guide::PaimonGuideService::get().isEnabled()) {
            if (auto* popup = paimon::guide::PaimonGuideChatPopup::create()) {
                popup->show();
            }
            return;
        }

        // World position for the explosion
        auto* parent = btn->getParent();
        CCPoint worldPos = parent
            ? parent->convertToWorldSpace(btn->getPosition())
            : btn->getPosition();

        // Random explosion sound
        static std::string const explosionSounds[] = {
            std::string("explode_11") + ".ogg",
            std::string("quitSound_01") + ".ogg",
            std::string("endStart_02") + ".ogg",
            std::string("gold_02") + ".ogg",
            std::string("crystalDestroy") + ".ogg",
        };
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> soundDist(0, 4);
        FMODAudioEngine::sharedEngine()->playEffect(explosionSounds[soundDist(rng)].c_str());

        // Random explosion particles
        static char const* explosionEffects[] = {
            "explodeEffect.plist",
            "firework_01.plist",
            "fireEffect_01.plist",
            "chestOpen.plist",
            "goldPickupEffect.plist",
        };
        std::uniform_int_distribution<int> fxDist(0, 4);
        auto* particles = CCParticleSystemQuad::create(explosionEffects[fxDist(rng)], false);
        if (particles) {
            particles->setPosition(worldPos);
            particles->setPositionType(kCCPositionTypeGrouped);
            particles->setAutoRemoveOnFinish(true);
            particles->setScale(1.5f);
            this->addChild(particles, 100);
        }

        // Disappear animation
        btn->runAction(CCSequence::create(
            CCSpawn::create(
                CCScaleTo::create(0.3f, 0.f),
                CCRotateBy::create(0.3f, 360.f),
                CCFadeOut::create(0.3f),
                nullptr
            ),
            CCCallFunc::create(btn, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }

    void updateBackground() {
        log::info("[MenuLayer] updateBackground");
        // Read unified config
        auto cfg = LayerBackgroundManager::get().getConfig("menu");

        // NOTE: legacy fallback removed — migration runs once at startup via
        // LayerBackgroundManager::migrateFromLegacy(). Re-running it on every
        // scene reload caused the user's "Default" choice to be overwritten
        // by stale `bg-type` legacy values that other UI paths had set in
        // earlier sessions. The unified `layerbg-menu-type` saved value is
        // the single source of truth from now on.

        // Default mode: restore the original background
        if (cfg.type == "default") {
            if (auto bg = this->getChildByID("main-menu-bg")) {
                bg->setVisible(true);
                bg->setZOrder(-10);
            }
            // Clear the previous video cache
            LayerBackgroundManager::get().cleanupOldVideoCache(this, "");
            // Remove previous custom backgrounds
            if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
                oldContainer->removeFromParent();
            }
            LayerBackgroundManager::get().clearAppliedBackground(this, false);
            // Force-evict any stale/unreferenced shared video players so they
            // don't keep RAM/decoder threads alive past the TTL grace window.
            LayerBackgroundManager::get().forceEvictAllStaleVideos();
            LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
            paimon::beat_shaders::BeatShaderManager::get().applyToLayer(this, "menu");
            m_fields->m_bgSprite = nullptr;
            m_fields->m_bgOverlay = nullptr;
            this->applyAdaptiveColor({255, 255, 255});
            return;
        }

        // Hide the vanilla background.
        if (auto bg = this->getChildByID("main-menu-bg")) {
            bg->setVisible(false);
        }

        // Clear the previous container
        if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
            oldContainer->removeFromParent();
        }
        bool nextOwnsVideoAudio =
            LayerBackgroundManager::get().resolveConfig("menu").type == "video" &&
            paimon::settings::video::audioEnabled();

        // Clear the video cache when the type changes
        {
            std::string nextResolvedType = LayerBackgroundManager::get().resolveConfig("menu").type;
            if (nextResolvedType != "video") {
                LayerBackgroundManager::get().cleanupOldVideoCache(this, "");
            }
        }

        LayerBackgroundManager::get().clearAppliedBackground(this, nextOwnsVideoAudio);
        m_fields->m_bgSprite = nullptr;
        m_fields->m_bgOverlay = nullptr;

        auto winSize = CCDirector::get()->getWinSize();

        auto container = CCNode::create();
        container->setContentSize(winSize);
        container->setPosition({0, 0});
        container->setAnchorPoint({0, 0});
        container->setID("paimon-bg-container"_spr);
        container->setZOrder(-10);
        this->addChild(container);

        // Resolve "Same as..." type
        std::string resolvedType = cfg.type;
        std::string resolvedPath = cfg.customPath;
        int resolvedId = cfg.levelId;
        std::string resolvedShader = cfg.shader;

        int maxHops = 5;
        while (maxHops-- > 0) {
            bool isLayerRef = false;
            for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
                if (resolvedType == k && k != "menu") { isLayerRef = true; break; }
            }
            if (isLayerRef) {
                auto refCfg = LayerBackgroundManager::get().getConfig(resolvedType);
                if (refCfg.type == "default") {
                    container->removeFromParent();
                    if (auto bg = this->getChildByID("main-menu-bg")) {
                        bg->setVisible(true);
                        bg->setZOrder(-10);
                    }
                    paimon::beat_shaders::BeatShaderManager::get().applyToLayer(this, "menu");
                    this->applyAdaptiveColor({255, 255, 255});
                    return;
                }
                resolvedType = refCfg.type;
                resolvedPath = refCfg.customPath;
                resolvedId = refCfg.levelId;
                resolvedShader = refCfg.shader;
                continue;
            }
            break;
        }

        CCSprite* sprite = nullptr;
        CCTexture2D* tex = nullptr;

        if (resolvedType == "custom" && !resolvedPath.empty()) {
            std::error_code fsEc;
            if (!std::filesystem::exists(resolvedPath, fsEc) || fsEc) {
                container->removeFromParent();
                return;
            }
            if (resolvedPath.ends_with(".gif") || resolvedPath.ends_with(".GIF")) {
                // Async GIF with Ref<> to avoid a leak
                Ref<MenuLayer> safeThis = this;
                Ref<CCNode> safeContainer = container;
                bool darkMode = cfg.darkMode;
                float darkIntensity = cfg.darkIntensity;
                std::string shaderName = cfg.shader;
                AnimatedGIFSprite::pinGIF(resolvedPath);
                AnimatedGIFSprite::createAsync(resolvedPath, [safeThis, safeContainer, winSize, darkMode, darkIntensity, shaderName, resolvedPath](AnimatedGIFSprite* anim) {
                    if (!anim || !safeContainer->getParent()) {
                        AnimatedGIFSprite::unpinGIF(resolvedPath);
                        if (!anim && safeContainer->getParent()) safeContainer->removeFromParent();
                        return;
                    }

                    float contentWidth = anim->getContentWidth();
                    float contentHeight = anim->getContentHeight();

                    if (contentWidth <= 0 || contentHeight <= 0) {
                        AnimatedGIFSprite::unpinGIF(resolvedPath);
                        safeContainer->removeFromParent();
                        return;
                    }

                    float scaleX = winSize.width / contentWidth;
                    float scaleY = winSize.height / contentHeight;
                    float scale = std::max(scaleX, scaleY);

                    anim->ignoreAnchorPointForPosition(false);
                    anim->setAnchorPoint({0.5f, 0.5f});
                    anim->setPosition(winSize / 2);
                    anim->setScale(scale);

                    // Apply shader to the GIF
                    if (!shaderName.empty() && shaderName != "none") {
                        auto* program = Shaders::getBgShaderProgram(shaderName);
                        if (program) {
                            anim->setShaderProgram(program);
                            anim->m_intensity = 0.5f;
                            anim->m_texSize = CCSize(winSize.width, winSize.height);
                        }
                    }

                    auto* self = static_cast<PaimonMenuLayer*>(safeThis.data());

                    if (darkMode) {
                        GLubyte alpha = static_cast<GLubyte>(darkIntensity * 200.0f);
                        auto overlay = CCLayerColor::create({0, 0, 0, alpha});
                        overlay->setContentSize(winSize);
                        overlay->setZOrder(1);
                        safeContainer->addChild(overlay);
                        self->m_fields->m_bgOverlay = overlay;
                    }

                    safeContainer->addChild(anim);
                    self->m_fields->m_bgSprite = anim;
                });
                return;
            } else {
                // Static image, loaded ASYNC so entering the menu doesn't stall
                // the frame. Decoding (PNG/JPG/WebP/etc) runs on a background
                // thread via LazySprite; when done, the callback (on the main
                // thread) finalizes the sprite. m_bgStaticLoad auto-cancels any
                // previous load if the user changes background or leaves the menu first.
                CCTextureCache::sharedTextureCache()->removeTextureForKey(resolvedPath.c_str());

                Ref<CCNode> safeContainer = container;
                Ref<MenuLayer> safeThis = this;
                LayerBgConfig cfgCopy = cfg;
                std::string pathCopy = resolvedPath;

                m_fields->m_bgStaticLoad.loadFromFile(
                    std::filesystem::path(resolvedPath),
                    [safeThis, safeContainer, cfgCopy, pathCopy](CCTexture2D* loadedTex, bool success) {
                        if (paimon::isRuntimeShuttingDown()) return;
                        if (!safeContainer->getParent()) return;
                        auto* self = static_cast<PaimonMenuLayer*>(safeThis.data());
                        if (!self) return;
                        if (!success || !loadedTex) {
                            safeContainer->removeFromParent();
                            return;
                        }
                        self->finalizeBackgroundWithTexture(safeContainer, loadedTex, cfgCopy, "custom", pathCopy);
                    },
                    /*ignoreCache=*/true);
                return;
            }
        } else if (resolvedType == "id" && resolvedId > 0) {
            // Async: leer el .rgb (varios MB) + convertir a RGBA en el main
            // thread congelaba el frame en cada entrada al menu.
            loadThumbBackgroundAsync(container, cfg, resolvedType, resolvedPath, resolvedId);
            return;
        } else if (resolvedType == "video" && !resolvedPath.empty()) {
            std::error_code fsEc;
            if (!std::filesystem::exists(resolvedPath, fsEc) || fsEc) {
                // Video file not found — revert to default gracefully
                log::warn("[MenuLayer] Video file not found: {} — showing default bg", resolvedPath);
                container->removeFromParent();
                if (auto bg = this->getChildByID("main-menu-bg")) {
                    bg->setVisible(true);
                    bg->setZOrder(-10);
                }
                LayerBackgroundManager::get().forceReleaseSharedVideoByPath(resolvedPath);
                LayerBackgroundManager::get().forceEvictAllStaleVideos();
                LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
                paimon::beat_shaders::BeatShaderManager::get().applyToLayer(this, "menu");
                PaimonNotify::create("Video file not found", NotificationIcon::Warning)->show();
                return;
            }
            // Delegate video to LayerBackgroundManager
            container->removeFromParent();
            LayerBackgroundManager::get().applyVideoBg(this, resolvedPath, cfg);
            return;
        } else if (resolvedType == "shader") {
            container->removeFromParent();
            auto shaderCfg = cfg;
            shaderCfg.type = "shader";
            shaderCfg.shader = resolvedShader;
            LayerBackgroundManager::get().applyProceduralShaderBg(this, shaderCfg);
            this->applyAdaptiveColor({255, 255, 255});
            return;
        }

        if (!sprite && !tex && (resolvedType == "random" || resolvedType == "thumbnails")) {
            auto ids = LocalThumbs::get().getAllLevelIDs();
            if (!ids.empty()) {
                static std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                int32_t levelID = ids[dist(rng)];
                loadThumbBackgroundAsync(container, cfg, resolvedType, resolvedPath, levelID);
                return;
            }
        }

        if (!tex) {
            container->removeFromParent();
            return;
        }

        // Remaining synchronous path (id/random): the texture is already in
        // RAM/GPU, so finalizing here decodes nothing heavy in the frame.
        this->finalizeBackgroundWithTexture(container, tex, cfg, resolvedType, resolvedPath);
    }

    // Thumbnail background ("id" / "random" / "thumbnails") loaded off-thread.
    // RAM-cache hits finalize immediately; otherwise LocalThumbs reads and
    // converts on a worker and only the GPU upload runs on the main thread.
    void loadThumbBackgroundAsync(
        CCNode* container, LayerBgConfig const& cfg,
        std::string const& resolvedType, std::string const& resolvedPath, int32_t levelID
    ) {
        if (auto* cached = LocalThumbs::get().getCachedTexture(levelID)) {
            this->finalizeBackgroundWithTexture(container, cached, cfg, resolvedType, resolvedPath);
            return;
        }

        Ref<CCNode> safeContainer = container;
        Ref<MenuLayer> safeThis = this;
        LayerBgConfig cfgCopy = cfg;
        std::string typeCopy = resolvedType;
        std::string pathCopy = resolvedPath;
        LocalThumbs::get().loadTextureAsync(levelID,
            [safeThis, safeContainer, cfgCopy, typeCopy, pathCopy](CCTexture2D* tex) {
                if (paimon::isRuntimeShuttingDown()) return;
                if (!safeContainer->getParent()) return;
                auto* self = static_cast<PaimonMenuLayer*>(safeThis.data());
                if (!self) return;
                if (!tex) {
                    safeContainer->removeFromParent();
                    return;
                }
                self->finalizeBackgroundWithTexture(safeContainer, tex, cfgCopy, typeCopy, pathCopy);
            });
    }

    // Finalize the background once the texture is ready (sync or after async
    // load). Creates the sprite (optional shader), scales it to screen, applies
    // dark mode, and kicks off adaptive-color extraction on a background thread.
    // Safe to call from the async callback: the caller already checked container->getParent().
    void finalizeBackgroundWithTexture(
        CCNode* container, CCTexture2D* tex, LayerBgConfig const& cfg,
        std::string const& resolvedType, std::string const& resolvedPath
    ) {
        if (!container || !tex) {
            if (container) container->removeFromParent();
            return;
        }

        auto winSize = CCDirector::get()->getWinSize();
        CCSprite* sprite = nullptr;

        // Use ShaderBgSprite if a shader is configured
        if (!cfg.shader.empty() && cfg.shader != "none") {
            auto shaderSpr = Shaders::ShaderBgSprite::createWithTexture(tex);
            if (shaderSpr) {
                auto* program = Shaders::getBgShaderProgram(cfg.shader);
                if (program) {
                    shaderSpr->setShaderProgram(program);
                    shaderSpr->m_shaderIntensity = geode::Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
                    shaderSpr->m_screenW = winSize.width;
                    shaderSpr->m_screenH = winSize.height;
                    shaderSpr->m_shaderTime = 0.f;
                    shaderSpr->schedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));
                }
                sprite = shaderSpr;
            }
        }
        if (!sprite) {
            sprite = CCSprite::createWithTexture(tex);
        }
        if (!sprite) {
            container->removeFromParent();
            return;
        }

        if (sprite->getContentWidth() <= 0 || sprite->getContentHeight() <= 0) {
            container->removeFromParent();
            return;
        }

        float scaleX = winSize.width / sprite->getContentWidth();
        float scaleY = winSize.height / sprite->getContentHeight();
        float scale = std::max(scaleX, scaleY);

        sprite->setScale(scale);
        sprite->setPosition(winSize / 2);
        sprite->ignoreAnchorPointForPosition(false);
        sprite->setAnchorPoint({0.5f, 0.5f});

        // Dark mode
        if (cfg.darkMode) {
            GLubyte alpha = static_cast<GLubyte>(cfg.darkIntensity * 200.0f);
            auto overlay = CCLayerColor::create({0, 0, 0, alpha});
            overlay->setContentSize(winSize);
            overlay->setZOrder(1);
            container->addChild(overlay);
            m_fields->m_bgOverlay = overlay;
        }

        container->addChild(sprite);
        m_fields->m_bgSprite = sprite;

        // Adaptive colors: extraction (K-means in LAB) is pure CPU and costly,
        // so run it on a background thread and apply the result on the main
        // thread. Running it synchronously in init stalled the frame when
        // entering the menu with a custom wallpaper.
        bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
        m_fields->m_adaptiveColors = adaptive;
        if (adaptive && resolvedType == "custom" && !resolvedPath.empty()) {
            Ref<MenuLayer> safeThis = this;
            std::string pathCopy = resolvedPath;
            paimon::ThreadTracker::get().spawn([safeThis, pathCopy]() {
                if (paimon::isRuntimeShuttingDown()) return;
                // Decode a CPU copy (no GL) to sample colors.
                auto imgDeleter = [](CCImage* p) { if (p) p->release(); };
                std::unique_ptr<CCImage, decltype(imgDeleter)> img(
                    new (std::nothrow) CCImage(), imgDeleter);
                ccColor3B primary{255, 255, 255};
                bool ok = false;
                if (img && img->initWithImageFile(pathCopy.c_str())) {
                    auto colors = DominantColors::extract(img->getData(), img->getWidth(), img->getHeight());
                    primary = { colors.first.r, colors.first.g, colors.first.b };
                    ok = true;
                }
                geode::Loader::get()->queueInMainThread([safeThis, primary, ok]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto* self = static_cast<PaimonMenuLayer*>(safeThis.data());
                    if (!self || !self->getParent()) return;
                    self->applyAdaptiveColor(ok ? primary : ccColor3B{255, 255, 255});
                });
            });
        } else {
            this->applyAdaptiveColor({255, 255, 255});
        }

        log::debug("Updated menu background with unified config, type: {}", cfg.type);
    }

    void updateProfileButton() {
        auto profileMenu = this->getChildByID("profile-menu");
        if (!profileMenu) {
            profileMenu = this->getChildByIDRecursive("profile-menu");
        }
        if (!profileMenu) return;

        auto profileButton = typeinfo_cast<CCMenuItemSpriteExtra*>(profileMenu->getChildByID("profile-button"));
        if (!profileButton) return;

        float const targetSize = 48.0f;

        // Read custom config
        auto picCfg = ProfilePicCustomizer::get().getConfig();
        std::string shapeName = picCfg.stencilSprite;
        if (shapeName.empty()) shapeName = "circle";

        if (picCfg.onlyIconMode) {
            auto container = paimon::profile_pic::composeProfilePicture(nullptr, targetSize, picCfg);
            if (container) {
                profileButton->setNormalImage(container);
            }
            return;
        }

        auto type = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
        if (type != "custom") return;

        auto path = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
        if (path.empty()) return;

        std::error_code fsEc;
        if (!std::filesystem::exists(path, fsEc) || fsEc) {
             return;
        }

        if (ImageLoadHelper::isAnimatedImage(std::filesystem::path(path))) {
              // Ref<> to avoid a leak in the GIF callback
              Ref<MenuLayer> safeThis = this;
              AnimatedGIFSprite::pinGIF(path);
             // Capture profileButton with Ref<> to avoid use-after-free if the
             // node is destroyed before the async callback runs
             Ref<CCMenuItemSpriteExtra> safeProfileBtn = profileButton;
             AnimatedGIFSprite::createAsync(path, [safeThis, safeProfileBtn, targetSize, shapeName, picCfg, path](AnimatedGIFSprite* anim) {
                if (!anim || !safeProfileBtn->getParent()) {
                    AnimatedGIFSprite::unpinGIF(path);
                    return;
                }

                auto container = buildProfileClipContainer(anim, shapeName, targetSize, picCfg);
                if (container) {
                    safeProfileBtn->setNormalImage(container);
                }
            });
        } else {
            auto loaded = ImageLoadHelper::loadStaticImage(std::filesystem::path(path), 16);
            if (!loaded.success || !loaded.texture) return;
            auto sprite = CCSprite::createWithTexture(loaded.texture);
            loaded.texture->release();
            if (!sprite) return;

            auto container = buildProfileClipContainer(sprite, shapeName, targetSize, picCfg);
            if (container) {
                profileButton->setNormalImage(container);
            }
        }
    }
};




// Global listener for the "Guide" toggle. When the user toggles the guide from
// the Hub sidebar, GuideEnabledChangedEvent fires; refresh the MenuLayer's
// hidden Paimon live without restarting the scene.
//   - ON  -> hide static sprite, mount AnimatedPaimon overlay
//   - OFF -> remove AnimatedPaimon overlay, restore static opacity
// Registered once as a leaked global listener so it survives the whole session.
$execute {
    using namespace paimon::guide;

    GuideEnabledChangedEvent(kGuideEventFilter).listen(
        [](bool enabled) {
            // Find the running scene and a MenuLayer with the Paimon inside it.
            auto* scene = CCDirector::get()->getRunningScene();
            if (!scene) return geode::ListenerResult::Propagate;

            auto* hiddenMenu = scene->getChildByIDRecursive("paimon-hidden-menu"_spr);
            if (!hiddenMenu) return geode::ListenerResult::Propagate;

            auto* hiddenBtn = hiddenMenu->getChildByIDRecursive("paimon-hidden-btn"_spr);
            if (!hiddenBtn) return geode::ListenerResult::Propagate;

            auto* btnSpriteExtra = typeinfo_cast<CCMenuItemSpriteExtra*>(hiddenBtn);
            if (!btnSpriteExtra) return geode::ListenerResult::Propagate;

            // Static sprite: getNormalImage() returns a generic CCNode*; cast to
            // CCSprite to access setOpacity.
            auto* staticSprNode = btnSpriteExtra->getNormalImage();
            auto* staticSpr = typeinfo_cast<CCSprite*>(staticSprNode);

            // AnimatedPaimon overlay (may not exist yet)
            auto* overlay = hiddenBtn->getChildByIDRecursive("paimon-hidden-animated"_spr);

            if (enabled) {
                // Switch to guide mode ON
                if (staticSpr) staticSpr->setOpacity(0);
                if (!overlay) {
                    // Match the static sprite's scale
                    float spriteScale = staticSpr ? staticSpr->getScale() : 0.5f;
                    auto* animated = AnimatedPaimon::create(spriteScale);
                    if (animated) {
                        animated->setLively(true);
                        animated->play(AnimatedPaimon::Animation::Idle);
                        animated->setAnchorPoint({0.5f, 0.5f});
                        animated->setPosition(btnSpriteExtra->getContentSize() * 0.5f);
                        animated->setID("paimon-hidden-animated"_spr);
                        btnSpriteExtra->addChild(animated, 1);

                        // Periodic bubble, same logic as the initial path (see
                        // MenuLayer init). If the user disables the guide, the
                        // overlay and its action scheduler are removed automatically.
                        WeakRef<AnimatedPaimon> weakAnim(animated);
                        auto bubbleTick = CallFuncExt::create([weakAnim] {
                            if (auto anim = weakAnim.lock()) {
                                static std::mt19937 brng(std::random_device{}());
                                std::uniform_int_distribution<int> chance(0, 99);
                                if (chance(brng) < 30) {
                                    auto txt = Localization::get().getString("pai.guide.bubble");
                                    anim->showBubble(txt, 3.f);
                                }
                            }
                        });
                        animated->runAction(CCRepeatForever::create(
                            CCSequence::create(
                                CCDelayTime::create(30.f),
                                bubbleTick,
                                nullptr
                            )
                        ));
                    }
                }
            } else {
                // Switch to guide mode OFF
                if (overlay) {
                    overlay->removeFromParent();
                }
                if (staticSpr) staticSpr->setOpacity(180);
            }

            return geode::ListenerResult::Propagate;
        }
    ).leak();
}
