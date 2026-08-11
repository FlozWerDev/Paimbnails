#include <Geode/modify/MenuLayer.hpp>
#include "../framework/HookConventions.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include <Geode/utils/cocos.hpp>
#include "../framework/state/SessionState.hpp"
#include <Geode/utils/file.hpp>
#include "../features/pet/services/PetManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/volume-scroll/services/VolumeScrollManager.hpp"
#include "../features/dynamic-volume/services/DynamicVolumeManager.hpp"
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
#include "../utils/LocalAssetStore.hpp"
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

extern void initPetTicker();

extern void initCursorTicker();

extern void initVolumeScrollTicker();

extern void initDynamicVolumeTicker();

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
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    struct Fields {
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCLayerColor> m_bgOverlay = nullptr;
        bool m_adaptiveColors = false;
// Cache the button weakly; other mods may remove it.
        WeakRef<CCNode> m_hubBtnCached;
        bool m_hubBtnSearched = false;
        int m_adaptiveFrameCounter = 0;
        ccColor3B m_lastAdaptiveColor = {255, 255, 255};
        int m_badgeFrameCounter = 0;
// Decode custom backgrounds off-thread; the loader cancels on scene exit.
        paimon::image::RetainedLazyTextureLoad m_bgStaticLoad;
    };

    void setVanillaBackgroundVisible(bool visible) {
        if (auto* bg = this->getChildByID("main-menu-bg")) {
            bg->setVisible(visible);
            bg->setZOrder(-10);
        }
    }

    void failBackgroundLoad(CCNode* container, std::string const& reason) {
        if (container && container->getParent()) {
            container->removeFromParent();
        }
        this->setVanillaBackgroundVisible(true);
        m_fields->m_bgSprite = nullptr;
        m_fields->m_bgOverlay = nullptr;
        LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
        paimon::beat_shaders::BeatShaderManager::get().applyToLayer(this, "menu");
        this->applyAdaptiveColor({255, 255, 255});
        log::warn("[MenuLayer] Background load failed: {}", reason);
    }

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

// Build the shared Paimon hub button.
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

// Menu entry clears stale Paimon audio ownership.
        {
            using namespace paimon;
            bool anyStuck =
                isProfileMusicInteropActive() ||
                isDynamicSongInteropActive()  ||
                isVideoAudioInteropActive();
            if (anyStuck) {
                log::warn("[InteropWatchdog] flags stuck on MenuLayer entry: profile={} dynamic={} video={} - clearing",
                          isProfileMusicInteropActive(),
                          isDynamicSongInteropActive(),
                          isVideoAudioInteropActive());
                setProfileMusicInteropActive(false);
                setDynamicSongInteropActive(false);
                setVideoAudioInteropActive(false);
            }
        }

        if (!s_menuServicesInitialized && !s_menuServicesScheduled) {
            s_menuServicesScheduled = true;
            this->scheduleOnce(schedule_selector(PaimonMenuLayer::deferredMenuServicesInit), 0.35f);
        }

        paimon::forum::ForumApi::get().sendHeartbeat([](paimon::forum::Result<bool> result) {
            log::debug("[Heartbeat] Initial heartbeat sent: {}", result.ok && result.data);
        });
        this->schedule(schedule_selector(PaimonMenuLayer::tickHeartbeat), 60.f);

        paimon::SessionState::get().currentListID = 0;

        this->scheduleUpdate();

        if (paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.reopenQueue)) {
            this->scheduleOnce(schedule_selector(PaimonMenuLayer::openVerificationQueue), 0.6f);
        }

// Add the hub button once.
        if (!this->getChildByID("paimon-hub-btn"_spr) && !this->getChildByID("paimon-fallback-bottom-menu"_spr)) {
            if (auto bottomMenu = this->getChildByID("bottom-menu")) {
                if (auto btn = createPaimonHubButton()) {
                    bottomMenu->addChild(btn);
                    bottomMenu->updateLayout();
                }
            } else {
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

        if (auto* title = this->getChildByID("main-title")) {
            auto paimonSpr = CCSprite::create("paim_Paimon.png"_spr);
            if (paimonSpr) {
                auto titleSize = title->getContentSize();
                auto titleScale = title->getScale();
                float titleW = titleSize.width * titleScale;
                float titleH = titleSize.height * titleScale;

                float paimonMaxH = titleH * 0.7f;
                float sprH = paimonSpr->getContentSize().height;
                if (sprH <= 0.f) sprH = 1.f;
                float paimonScale = paimonMaxH / sprH;
                paimonSpr->setScale(paimonScale);

                static std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> distX(-titleW * 0.35f, titleW * 0.35f);
                std::uniform_real_distribution<float> distY(-titleH * 0.15f, titleH * 0.15f);
                std::uniform_real_distribution<float> distRot(-45.f, 45.f);

                auto titlePos = title->getPosition();
                float px = titlePos.x + distX(rng);
                float py = titlePos.y + distY(rng);
                float rot = distRot(rng);

                auto paimonBtn = CCMenuItemSpriteExtra::create(                    paimonSpr, this, menu_selector(PaimonMenuLayer::onPaimonClick));
                paimonBtn->setRotation(rot);
                paimonBtn->setID("paimon-hidden-btn"_spr);

                auto paimonMenu = CCMenu::create();
                paimonMenu->setPosition(ccp(px, py));
                paimonMenu->setContentSize(paimonSpr->getScaledContentSize());
                paimonMenu->setID("paimon-hidden-menu"_spr);
                paimonMenu->addChild(paimonBtn);

                bool guideOn = paimon::guide::PaimonGuideService::get().isEnabled();
                if (guideOn) {
                    paimonSpr->setOpacity(0);

                    auto* animated = paimon::guide::AnimatedPaimon::create(paimonScale);
                    if (animated) {
                        animated->setLively(true);
                        animated->play(paimon::guide::AnimatedPaimon::Animation::Idle);
                        animated->setRotation(rot);
                        animated->setAnchorPoint({0.5f, 0.5f});
                        animated->setPosition(paimonBtn->getContentSize() * 0.5f);
                        animated->setID("paimon-hidden-animated"_spr);
                        paimonBtn->addChild(animated, 1);

                        WeakRef<paimon::guide::AnimatedPaimon> weakAnim(animated);
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
                } else {
                    paimonSpr->setOpacity(180);
                }

                this->addChild(paimonMenu, 1);
            }
        }

        return true;
    }

    $override
    void update(float dt) {
        MenuLayer::update(dt);
        
        if (m_fields->m_adaptiveColors && m_fields->m_bgSprite) {
             if (++m_fields->m_adaptiveFrameCounter >= 4) {
                 m_fields->m_adaptiveFrameCounter = 0;
                 if (auto gif = typeinfo_cast<AnimatedGIFSprite*>(static_cast<CCSprite*>(m_fields->m_bgSprite))) {
                     auto colors = gif->getCurrentFrameColors();
                     ccColor3B newColor = {colors.first.r, colors.first.g, colors.first.b};
                     if (newColor.r != m_fields->m_lastAdaptiveColor.r ||
                         newColor.g != m_fields->m_lastAdaptiveColor.g ||
                         newColor.b != m_fields->m_lastAdaptiveColor.b) {
                         m_fields->m_lastAdaptiveColor = newColor;
                         this->applyAdaptiveColor(newColor);
                     }
                 }
             }
        }

        if (ProfilePicCustomizer::get().isDirty()) {
            ProfilePicCustomizer::get().setDirty(false);
            this->updateProfileButton();
        }

        if (++m_fields->m_badgeFrameCounter >= 60) {
            m_fields->m_badgeFrameCounter = 0;
            this->applyUpdateBadge();
        }

    }

    void applyUpdateBadge() {
// Cache the button weakly; it may be recreated with the scene.
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
            m_fields->m_hubBtnSearched = false;
            return;
        }

        bool hasUpdate = paimon::updates::UpdateChecker::get().hasUpdate();
        auto existing = btn->getChildByID("paimon-hub-update-badge"_spr);

        if (hasUpdate && !existing) {
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

        paimon::volscroll::VolumeScrollManager::get().init();

        initPetTicker();
        initCursorTicker();
        initVolumeScrollTicker();

        paimon::dynvol::DynamicVolumeManager::get().init();
        initDynamicVolumeTicker();

// Load pet/cursor config off-thread; apply on the main thread.
        paimon::ThreadTracker::get().spawn([]() {
            geode::utils::thread::setName("PaimonPetCursorLoad");
            if (paimon::isRuntimeShuttingDown()) return;

            PetManager::get().loadConfig();
            if (paimon::isRuntimeShuttingDown()) return;
            CursorManager::get().loadConfig();

            if (paimon::isRuntimeShuttingDown()) return;

            geode::Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;

                std::srand(static_cast<unsigned int>(std::time(nullptr)));

                {
                    auto& cm = CursorManager::get();
                    Mod::get()->setSettingValue<bool>("custom-cursor-enable", cm.config().enabled);
                    cm.applyConfigLive();
                }

                PetManager::get().applyConfigLive();

                log::info("[PaimonThumbnails] Pet/Cursor config loaded and applied");
            });
        });
    }

    void tickHeartbeat(float dt) {
        paimon::forum::ForumApi::get().sendHeartbeat([](paimon::forum::Result<bool> result) {
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

// Guide mode turns the hidden sprite into the chat shortcut.
        if (paimon::guide::PaimonGuideService::get().isEnabled()) {
            if (auto* popup = paimon::guide::PaimonGuideChatPopup::create()) {
                popup->show();
            }
            return;
        }

        auto* parent = btn->getParent();
        CCPoint worldPos = parent
            ? parent->convertToWorldSpace(btn->getPosition())
            : btn->getPosition();

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
        auto cfg = LayerBackgroundManager::get().getConfig("menu");

// Legacy background migration runs once at startup; the unified saved value is
// the source of truth after that.

        if (cfg.type == "default"
            || !paimon::modules::isEnabled("paimbnails.backgrounds.global")) {
            if (auto bg = this->getChildByID("main-menu-bg")) {
                bg->setVisible(true);
                bg->setZOrder(-10);
            }
            LayerBackgroundManager::get().cleanupOldVideoCache(this, "");
            if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
                oldContainer->removeFromParent();
            }
            LayerBackgroundManager::get().clearAppliedBackground(this, false);
            LayerBackgroundManager::get().forceEvictAllStaleVideos();
            LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
            paimon::beat_shaders::BeatShaderManager::get().applyToLayer(this, "menu");
            m_fields->m_bgSprite = nullptr;
            m_fields->m_bgOverlay = nullptr;
            this->applyAdaptiveColor({255, 255, 255});
            return;
        }

        this->setVanillaBackgroundVisible(true);

        if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
            oldContainer->removeFromParent();
        }        bool nextOwnsVideoAudio =
            LayerBackgroundManager::get().resolveConfig("menu").type == "video" &&
            paimon::settings::video::audioEnabled();

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
                    this->failBackgroundLoad(container, "referenced layer uses the default background");
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
            auto backgroundPath = paimon::assets::normalizePath(resolvedPath);
            if (!std::filesystem::exists(backgroundPath, fsEc) || fsEc) {
                this->failBackgroundLoad(container, "custom file not found");
                return;
            }
            if (ImageLoadHelper::isGIF(backgroundPath)) {
                Ref<MenuLayer> safeThis = this;
                Ref<CCNode> safeContainer = container;
                bool darkMode = cfg.darkMode;
                float darkIntensity = cfg.darkIntensity;
                std::string shaderName = cfg.shader;
                AnimatedGIFSprite::pinGIF(resolvedPath);
                AnimatedGIFSprite::createAsync(resolvedPath, [safeThis, safeContainer, winSize, darkMode, darkIntensity, shaderName, resolvedPath](AnimatedGIFSprite* anim) {
                    auto* self = static_cast<PaimonMenuLayer*>(safeThis.data());
                    if (!safeContainer->getParent()) {
                        AnimatedGIFSprite::unpinGIF(resolvedPath);
                        return;
                    }
                    if (!anim) {
                        AnimatedGIFSprite::unpinGIF(resolvedPath);
                        self->failBackgroundLoad(safeContainer, "GIF decode failed");
                        return;
                    }

                    float contentWidth = anim->getContentWidth();
                    float contentHeight = anim->getContentHeight();

                    if (contentWidth <= 0 || contentHeight <= 0) {
                        AnimatedGIFSprite::unpinGIF(resolvedPath);
                        self->failBackgroundLoad(safeContainer, "GIF has invalid dimensions");
                        return;
                    }

                    float scaleX = winSize.width / contentWidth;
                    float scaleY = winSize.height / contentHeight;
                    float scale = std::max(scaleX, scaleY);

                    anim->ignoreAnchorPointForPosition(false);
                    anim->setAnchorPoint({0.5f, 0.5f});
                    anim->setPosition(winSize / 2);
                    anim->setScale(scale);

                    if (!shaderName.empty() && shaderName != "none") {
                        auto* program = Shaders::getBgShaderProgram(shaderName);
                        if (program) {
                            anim->setShaderProgram(program);
                            anim->m_intensity = 0.5f;
                            anim->m_texSize = CCSize(winSize.width, winSize.height);
                        }
                    }

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
                    self->setVanillaBackgroundVisible(false);
                });
                return;
            } else {
// Decode static backgrounds off-thread.
                CCTextureCache::sharedTextureCache()->removeTextureForKey(resolvedPath.c_str());

                Ref<CCNode> safeContainer = container;
                Ref<MenuLayer> safeThis = this;
                LayerBgConfig cfgCopy = cfg;
                std::string pathCopy = resolvedPath;

                m_fields->m_bgStaticLoad.loadFromFile(
                    backgroundPath,
                    [safeThis, safeContainer, cfgCopy, pathCopy](CCTexture2D* loadedTex, bool success) {
                        if (paimon::isRuntimeShuttingDown()) return;
                        if (!safeContainer->getParent()) return;
                        auto* self = static_cast<PaimonMenuLayer*>(safeThis.data());
                        if (!self) return;
                        if (!success || !loadedTex) {
                            self->failBackgroundLoad(safeContainer, "static image decode failed");
                            return;
                        }
                        self->finalizeBackgroundWithTexture(safeContainer, loadedTex, cfgCopy, "custom", pathCopy);
                    },
                    /*ignoreCache=*/true);
                return;
            }
        } else if (resolvedType == "id" && resolvedId > 0) {
            loadThumbBackgroundAsync(container, cfg, resolvedType, resolvedPath, resolvedId);
            return;
        } else if (resolvedType == "video" && !resolvedPath.empty()) {
            if (!paimon::assets::exists(resolvedPath)) {
// Revert to the default when the video is missing.
                log::warn("[MenuLayer] Video file not found: {} - showing default bg", resolvedPath);
                LayerBackgroundManager::get().forceReleaseSharedVideoByPath(resolvedPath);
                LayerBackgroundManager::get().forceEvictAllStaleVideos();
                this->failBackgroundLoad(container, "video file not found");
                PaimonNotify::create("Video file not found", NotificationIcon::Warning)->show();
                return;
            }
            container->removeFromParent();
            LayerBackgroundManager::get().applyVideoBg(this, resolvedPath, cfg);
            return;
        } else if (resolvedType == "shader") {
            container->removeFromParent();
            auto shaderCfg = cfg;
            shaderCfg.type = "shader";
            shaderCfg.shader = resolvedShader;
            if (!LayerBackgroundManager::get().applyProceduralShaderBg(this, shaderCfg)) {
                this->failBackgroundLoad(nullptr, "procedural shader creation failed");
            }
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
            this->failBackgroundLoad(container, "background source is unavailable");
            return;
        }

        this->finalizeBackgroundWithTexture(container, tex, cfg, resolvedType, resolvedPath);
    }

// Decode thumbnail backgrounds off-thread; upload on the main thread.
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
                    self->failBackgroundLoad(safeContainer, "thumbnail texture load failed");
                    return;
                }
                self->finalizeBackgroundWithTexture(safeContainer, tex, cfgCopy, typeCopy, pathCopy);
            });
    }

// Finalize the background and start optional color extraction.
    void finalizeBackgroundWithTexture(
        CCNode* container, CCTexture2D* tex, LayerBgConfig const& cfg,
        std::string const& resolvedType, std::string const& resolvedPath
    ) {
        if (!container || !tex) {
            this->failBackgroundLoad(container, "background texture is unavailable");
            return;
        }

        auto winSize = CCDirector::get()->getWinSize();
        CCSprite* sprite = nullptr;

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
            this->failBackgroundLoad(container, "background sprite creation failed");
            return;
        }

        if (sprite->getContentWidth() <= 0 || sprite->getContentHeight() <= 0) {
            this->failBackgroundLoad(container, "background has invalid dimensions");
            return;
        }

        float scaleX = winSize.width / sprite->getContentWidth();
        float scaleY = winSize.height / sprite->getContentHeight();
        float scale = std::max(scaleX, scaleY);

        sprite->setScale(scale);
        sprite->setPosition(winSize / 2);
        sprite->ignoreAnchorPointForPosition(false);
        sprite->setAnchorPoint({0.5f, 0.5f});

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
        this->setVanillaBackgroundVisible(false);

// Extract adaptive colors off-thread.
        bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
        m_fields->m_adaptiveColors = adaptive;
        if (adaptive && resolvedType == "custom" && !resolvedPath.empty()) {
// Use WeakRef across threads; lock it only on the main thread.
            WeakRef<MenuLayer> safeThis = this;
            std::string pathCopy = resolvedPath;
            paimon::ThreadTracker::get().spawn([safeThis, pathCopy]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto imgDeleter = [](CCImage* p) { if (p) p->release(); };
                std::unique_ptr<CCImage, decltype(imgDeleter)> img(
                    new (std::nothrow) CCImage(), imgDeleter);
                ccColor3B primary{255, 255, 255};
                bool ok = false;
                auto fileData = ImageLoadHelper::readBinaryFile(pathCopy, 32);
                if (img && !fileData.empty() &&
                    img->initWithImageData(fileData.data(), fileData.size())) {
                    auto colors = DominantColors::extract(img->getData(), img->getWidth(), img->getHeight());
                    primary = { colors.first.r, colors.first.g, colors.first.b };
                    ok = true;
                }
                geode::Loader::get()->queueInMainThread([safeThis, primary, ok]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto ref = safeThis.lock();
                    if (!ref) return;
                    auto* self = static_cast<PaimonMenuLayer*>(ref.data());
                    if (!self->getParent()) return;
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

        auto picCfg = ProfilePicCustomizer::get().getConfig();
        std::string shapeName = picCfg.stencilSprite;
        if (shapeName.empty()) shapeName = "circle";

        if (!picCfg.profileFont.empty()) {
            if (auto lbl = typeinfo_cast<CCLabelBMFont*>(this->getChildByID("player-username"))) {
                lbl->setFntFile(picCfg.profileFont.c_str());
            }
        }

        if (picCfg.onlyIconMode) {
            auto container = paimon::profile_pic::composeProfilePicture(nullptr, targetSize, picCfg);
            if (container) {
                profileButton->setNormalImage(container);
            }
            return;
        }

        auto photo = paimon::profile_pic::resolveProfilePhoto(picCfg);
        using PhotoKind = paimon::profile_pic::ResolvedProfilePhoto::Kind;

        if (photo.kind == PhotoKind::GifFile) {
            auto path = photo.path;
            AnimatedGIFSprite::pinGIF(path);
            Ref<CCMenuItemSpriteExtra> safeProfileBtn = profileButton;
            AnimatedGIFSprite::createAsync(path, [safeProfileBtn, targetSize, shapeName, picCfg, path](AnimatedGIFSprite* anim) {
                if (!anim || !safeProfileBtn->getParent()) {
                    AnimatedGIFSprite::unpinGIF(path);
                    return;
                }

                auto container = buildProfileClipContainer(anim, shapeName, targetSize, picCfg);
                if (container) {
                    safeProfileBtn->setNormalImage(container);
                }
            });
            return;
        }

        auto imageNode = paimon::profile_pic::createResolvedPhotoNode(photo);
        if (!imageNode) return;

        auto container = buildProfileClipContainer(imageNode, shapeName, targetSize, picCfg);
        if (container) {
            profileButton->setNormalImage(container);
        }
    }
};




// Keep the hidden Paimon in sync with the guide toggle.
$execute {
    using namespace paimon::guide;

    GuideEnabledChangedEvent(kGuideEventFilter).listen(
        [](bool enabled) {
            auto* scene = CCDirector::get()->getRunningScene();
            if (!scene) return geode::ListenerResult::Propagate;

            auto* hiddenMenu = scene->getChildByIDRecursive("paimon-hidden-menu"_spr);
            if (!hiddenMenu) return geode::ListenerResult::Propagate;

            auto* hiddenBtn = hiddenMenu->getChildByIDRecursive("paimon-hidden-btn"_spr);
            if (!hiddenBtn) return geode::ListenerResult::Propagate;

            auto* btnSpriteExtra = typeinfo_cast<CCMenuItemSpriteExtra*>(hiddenBtn);
            if (!btnSpriteExtra) return geode::ListenerResult::Propagate;

            auto* staticSprNode = btnSpriteExtra->getNormalImage();
            auto* staticSpr = typeinfo_cast<CCSprite*>(staticSprNode);

            auto* overlay = hiddenBtn->getChildByIDRecursive("paimon-hidden-animated"_spr);

            if (enabled) {
                if (staticSpr) staticSpr->setOpacity(0);
                if (!overlay) {
                    float spriteScale = staticSpr ? staticSpr->getScale() : 0.5f;
                    auto* animated = AnimatedPaimon::create(spriteScale);
                    if (animated) {
                        animated->setLively(true);
                        animated->play(AnimatedPaimon::Animation::Idle);
                        animated->setAnchorPoint({0.5f, 0.5f});
                        animated->setPosition(btnSpriteExtra->getContentSize() * 0.5f);
                        animated->setID("paimon-hidden-animated"_spr);
                        btnSpriteExtra->addChild(animated, 1);

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
                if (overlay) {
                    overlay->removeFromParent();
                }
                if (staticSpr) staticSpr->setOpacity(180);
            }

            return geode::ListenerResult::Propagate;
        }
    ).leak();
}
