#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include "../framework/state/SessionState.hpp"
#include <Geode/utils/file.hpp>
#include "../features/pet/services/PetManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../layers/PaiConfigLayer.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/DominantColors.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include "../utils/ShapeStencil.hpp"
#include "../core/Settings.hpp"
#include "../features/profiles/services/ProfilePicCustomizer.hpp"
#include "../features/profiles/services/ProfilePicRenderer.hpp"
#include "../utils/Shaders.hpp"
#include "../video/VideoPlayer.hpp"
#include <random>
#include <filesystem>
#include <string>

using namespace geode::prelude;

// declarada en main.cpp — inicializacion diferida del mod
extern void PaimonOnModLoaded();

// declarada en PetHook.cpp — registra el ticker del pet con el scheduler
extern void initPetTicker();

// declarada en CursorHook.cpp — registra el ticker del cursor con el scheduler
extern void initCursorTicker();

// ── Helper: construye el nodo clip+container+borde para la foto de perfil ──
// Delega en paimon::profile_pic::composeProfilePicture para aplicar scaleX/Y,
// rotacion, decoraciones y zoom/rotacion de imagen segun la config del usuario.
// `shapeName` se ignora (la forma se saca de picCfg.stencilSprite) y se mantiene
// por compatibilidad con los call sites.
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
        // MenuLayer usa node IDs y fondos de geode.node-ids
        (void)self.setHookPriorityAfterPost("MenuLayer::init", "geode.node-ids");
    }

    struct Fields {
        // Ref<> evita dangling pointers
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCLayerColor> m_bgOverlay = nullptr;
        bool m_adaptiveColors = false;
    };

    // Aplica colores adaptativos
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

        // Titea hijos de menus conocidos
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

    $override
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }
        log::info("[MenuLayer] init");

        // Inicializa mod una sola vez
        static bool s_paimonLoaded = false;
        if (!s_paimonLoaded) {
            s_paimonLoaded = true;
            log::info("[PaimonThumbnails] Invoking delayed Mod Loaded initialization from MenuLayer");
            PaimonOnModLoaded();
            PetManager::get().init();
            initPetTicker();
            CursorManager::get().init();
            initCursorTicker();
        }

        // Limpia contexto de lista al volver al menu
        paimon::SessionState::get().currentListID = 0;

        // Schedule update para colores adaptativos
        this->scheduleUpdate();

        // Reabre popup de verificacion si es necesario
        if (paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.reopenQueue)) {
            this->scheduleOnce(schedule_selector(PaimonMenuLayer::openVerificationQueue), 0.6f);
        }

        // Agrega boton de config en menu inferior
        if (auto bottomMenu = this->getChildByID("bottom-menu")) {
            auto btnSpr = CircleButtonSprite::createWithSpriteFrameName("GJ_paintBtn_001.png", 1.0f, CircleBaseColor::Green, CircleBaseSize::Medium);
            if (!btnSpr) return false;
            auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(PaimonMenuLayer::onBackgroundConfig));
            btn->setID("background-config-btn"_spr);
            bottomMenu->addChild(btn);
            bottomMenu->updateLayout();
        } else {
            // Fallback: menu propio con RowLayout
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto menu = CCMenu::create();
            menu->setContentSize({winSize.width, 40.f});
            menu->setAnchorPoint({0.f, 0.f});
            menu->ignoreAnchorPointForPosition(false);
            menu->setPosition({0.f, 8.f});
            menu->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setGap(8.f));
            menu->setID("paimon-fallback-bottom-menu"_spr);

            auto btnSpr = CircleButtonSprite::createWithSpriteFrameName("GJ_paintBtn_001.png", 1.0f, CircleBaseColor::Green, CircleBaseSize::Medium);
            if (!btnSpr) return false;
            auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(PaimonMenuLayer::onBackgroundConfig));
            btn->setID("background-config-btn"_spr);
            menu->addChild(btn);
            menu->updateLayout();

            this->addChild(menu);
        }

        this->updateBackground();
        this->updateProfileButton();

        // ── Paimon escondida detras del titulo (clickeable) ──
        if (auto* title = this->getChildByID("main-title")) {
            auto paimonSpr = CCSprite::create("paim_Paimon.png"_spr);
            if (paimonSpr) {
                auto titleSize = title->getContentSize();
                auto titleScale = title->getScale();
                float titleW = titleSize.width * titleScale;
                float titleH = titleSize.height * titleScale;

                // Escala Paimon menor que titulo
                float paimonMaxH = titleH * 0.7f;
                float paimonScale = paimonMaxH / paimonSpr->getContentSize().height;
                paimonSpr->setScale(paimonScale);

                // Posicion aleatoria dentro del titulo
                static std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> distX(-titleW * 0.35f, titleW * 0.35f);
                std::uniform_real_distribution<float> distY(-titleH * 0.15f, titleH * 0.15f);
                std::uniform_real_distribution<float> distRot(-45.f, 45.f);

                auto titlePos = title->getPosition();
                float px = titlePos.x + distX(rng);
                float py = titlePos.y + distY(rng);
                float rot = distRot(rng);

                // Crea boton clickeable
                auto paimonBtn = CCMenuItemSpriteExtra::create(
                    paimonSpr, this, menu_selector(PaimonMenuLayer::onPaimonClick));
                paimonBtn->setRotation(rot);
                paimonBtn->setID("paimon-hidden-btn"_spr);

                // Menu contenedor para boton (easter egg)
                auto paimonMenu = CCMenu::create();
                paimonMenu->setPosition(ccp(px, py));
                paimonMenu->setContentSize(paimonSpr->getScaledContentSize());
                paimonMenu->setID("paimon-hidden-menu"_spr);
                paimonMenu->addChild(paimonBtn);

                paimonSpr->setOpacity(180);

                // zOrder 1: detras del titulo
                this->addChild(paimonMenu, 1);
            }
        }

        return true;
    }

    $override
    void update(float dt) {
        MenuLayer::update(dt);
        
        if (m_fields->m_adaptiveColors && m_fields->m_bgSprite) {
             // Ref<>::operator->() devuelve el puntero raw para typeinfo_cast
             if (auto gif = typeinfo_cast<AnimatedGIFSprite*>(static_cast<CCSprite*>(m_fields->m_bgSprite))) {
                 auto colors = gif->getCurrentFrameColors();
                 this->applyAdaptiveColor({colors.first.r, colors.first.g, colors.first.b});
             }
        }

        // Si el editor de foto guardo cambios, actualizar el boton de perfil
        if (ProfilePicCustomizer::get().isDirty()) {
            ProfilePicCustomizer::get().setDirty(false);
            this->updateProfileButton();
        }
    }

    void openVerificationQueue(float dt) {
        auto scene = VerificationCenterLayer::scene();
        if (scene) {
            TransitionManager::get().pushScene(scene);
        }
    }

    void onBackgroundConfig(CCObject*) {
        TransitionManager::get().pushScene(PaiConfigLayer::scene());
    }

    void onPaimonClick(CCObject* sender) {
        auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!btn) return;

        // Posicion mundial para explosion
        auto* parent = btn->getParent();
        CCPoint worldPos = parent
            ? parent->convertToWorldSpace(btn->getPosition())
            : btn->getPosition();

        // Sonido de explosion aleatorio
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

        // Particulas de explosion aleatorias
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

        // Animacion de desaparicion
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
        // Lee config unificada
        auto cfg = LayerBackgroundManager::get().getConfig("menu");

        // Fallback a legacy keys
        if (cfg.type == "default") {
            std::string legacyType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
            if (legacyType == "thumbnails") legacyType = "random";
            if (legacyType != "default" && !legacyType.empty()) {
                // Migra datos legacy al formato unificado
                cfg.type = legacyType;
                cfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
                cfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
                cfg.darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
                cfg.darkIntensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
                // Guarda en formato unificado
                LayerBackgroundManager::get().saveConfig("menu", cfg);
            }
        }

        // Modo default: restaurar fondo original
        if (cfg.type == "default") {
            if (auto bg = this->getChildByID("main-menu-bg")) {
                bg->setVisible(true);
                bg->setZOrder(-10);
            }
            // Limpia cache de video anterior
            LayerBackgroundManager::get().cleanupOldVideoCache(this, "");
            // Elimina fondos custom anteriores
            if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
                oldContainer->removeFromParent();
            }
            LayerBackgroundManager::get().clearAppliedBackground(this, false);
            m_fields->m_bgSprite = nullptr;
            m_fields->m_bgOverlay = nullptr;
            this->applyAdaptiveColor({255, 255, 255});
            return;
        }

        // Oculta fondo original
        if (auto bg = this->getChildByID("main-menu-bg")) {
            bg->setVisible(false);
        }

        // Limpia container previo
        if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
            oldContainer->removeFromParent();
        }
        bool nextOwnsVideoAudio =
            LayerBackgroundManager::get().resolveConfig("menu").type == "video" &&
            paimon::settings::video::audioEnabled();

        // Limpia cache de video al cambiar tipo
        {
            std::string nextResolvedType = LayerBackgroundManager::get().resolveConfig("menu").type;
            if (nextResolvedType != "video") {
                LayerBackgroundManager::get().cleanupOldVideoCache(this, "");
            }
        }

        LayerBackgroundManager::get().clearAppliedBackground(this, nextOwnsVideoAudio);
        m_fields->m_bgSprite = nullptr;
        m_fields->m_bgOverlay = nullptr;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto container = CCNode::create();
        container->setContentSize(winSize);
        container->setPosition({0, 0});
        container->setAnchorPoint({0, 0});
        container->setID("paimon-bg-container"_spr);
        container->setZOrder(-10);
        this->addChild(container);

        // Resuelve tipo "Same as..."
        std::string resolvedType = cfg.type;
        std::string resolvedPath = cfg.customPath;
        int resolvedId = cfg.levelId;

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
                    if (auto bg = this->getChildByID("main-menu-bg")) bg->setVisible(true);
                    this->applyAdaptiveColor({255, 255, 255});
                    return;
                }
                resolvedType = refCfg.type;
                resolvedPath = refCfg.customPath;
                resolvedId = refCfg.levelId;
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
                // GIF async con Ref<> para evitar leak
                Ref<MenuLayer> safeThis = this;
                Ref<CCNode> safeContainer = container;
                bool darkMode = cfg.darkMode;
                float darkIntensity = cfg.darkIntensity;
                std::string shaderName = cfg.shader;
                AnimatedGIFSprite::pinGIF(resolvedPath);
                AnimatedGIFSprite::createAsync(resolvedPath, [safeThis, safeContainer, winSize, darkMode, darkIntensity, shaderName](AnimatedGIFSprite* anim) {
                    if (!anim || !safeContainer->getParent()) {
                        if (!anim && safeContainer->getParent()) safeContainer->removeFromParent();
                        return;
                    }

                    float contentWidth = anim->getContentWidth();
                    float contentHeight = anim->getContentHeight();

                    if (contentWidth <= 0 || contentHeight <= 0) {
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

                    // Aplica shader al GIF
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
                // Imagen estatica
                CCTextureCache::sharedTextureCache()->removeTextureForKey(resolvedPath.c_str());
                tex = CCTextureCache::sharedTextureCache()->addImage(resolvedPath.c_str(), false);
                if (!tex) {
                    auto stbResult = ImageLoadHelper::loadWithSTB(std::filesystem::path(resolvedPath));
                    if (stbResult.success && stbResult.texture) {
                        stbResult.texture->autorelease();
                        tex = stbResult.texture;
                    }
                }
            }
        } else if (resolvedType == "id" && resolvedId > 0) {
            tex = LocalThumbs::get().loadTexture(resolvedId);
        } else if (resolvedType == "video" && !resolvedPath.empty()) {
            std::error_code fsEc;
            if (!std::filesystem::exists(resolvedPath, fsEc) || fsEc) {
                container->removeFromParent();
                return;
            }
            // Delega video a LayerBackgroundManager
            container->removeFromParent();
            if (auto bg = this->getChildByID("main-menu-bg")) bg->setVisible(false);
            LayerBackgroundManager::get().applyVideoBg(this, resolvedPath, cfg);
            return;
        }

        if (!sprite && !tex && (resolvedType == "random" || resolvedType == "thumbnails")) {
            auto ids = LocalThumbs::get().getAllLevelIDs();
            if (!ids.empty()) {
                static std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                int32_t levelID = ids[dist(rng)];
                tex = LocalThumbs::get().loadTexture(levelID);
            }
        }

        if (!sprite && tex) {
            // Usar ShaderBgSprite si hay shader configurado
            if (!cfg.shader.empty() && cfg.shader != "none") {
                auto shaderSpr = Shaders::ShaderBgSprite::createWithTexture(tex);
                if (shaderSpr) {
                    auto* program = Shaders::getBgShaderProgram(cfg.shader);
                    if (program) {
                        shaderSpr->setShaderProgram(program);
                        shaderSpr->m_shaderIntensity = 0.5f;
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
        }

        if (!sprite) {
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

        // Colores adaptativos
        bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
        m_fields->m_adaptiveColors = adaptive;
        if (adaptive && tex) {
            // RAII guard para release() seguro
            auto imgDeleter = [](CCImage* p) { if (p) p->release(); };
            std::unique_ptr<CCImage, decltype(imgDeleter)> img(
                new (std::nothrow) CCImage(), imgDeleter);
            if (img) {
                bool loaded = false;
                if (resolvedType == "custom" && !resolvedPath.empty()) {
                    loaded = img->initWithImageFile(resolvedPath.c_str());
                }
                if (loaded) {
                    auto colors = DominantColors::extract(img->getData(), img->getWidth(), img->getHeight());
                    ccColor3B primary = { colors.first.r, colors.first.g, colors.first.b };
                    this->applyAdaptiveColor(primary);
                } else {
                    this->applyAdaptiveColor({255, 255, 255});
                }
                // release() automatico por RAII deleter
            } else {
                this->applyAdaptiveColor({255, 255, 255});
            }
        } else {
            this->applyAdaptiveColor({255, 255, 255});
        }

        // Modo oscuro
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

        log::debug("Updated menu background with unified config, type: {}", cfg.type);
    }

    void updateProfileButton() {
        auto type = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");

        if (type != "custom") return;

        auto path = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
        if (path.empty()) return;

        std::error_code fsEc;
        if (!std::filesystem::exists(path, fsEc) || fsEc) {
             return;
        }

        auto profileMenu = this->getChildByID("profile-menu");
        if (!profileMenu) {
            profileMenu = this->getChildByIDRecursive("profile-menu");
        }
        
        if (!profileMenu) return;

        auto profileButton = typeinfo_cast<CCMenuItemSpriteExtra*>(profileMenu->getChildByID("profile-button"));
        if (!profileButton) {
             return;
        }
        
        float const targetSize = 48.0f;

        // Lee config personalizada
        auto picCfg = ProfilePicCustomizer::get().getConfig();
        std::string shapeName = picCfg.stencilSprite;
        if (shapeName.empty()) shapeName = "circle";

        if (path.ends_with(".gif") || path.ends_with(".GIF")) {
             // Ref<> para evitar leak en callback GIF
             Ref<MenuLayer> safeThis = this;
             AnimatedGIFSprite::pinGIF(path);
             // Capturar profileButton con Ref<> para evitar use-after-free
             // si el nodo se destruye antes de que el callback async ejecute
             Ref<CCMenuItemSpriteExtra> safeProfileBtn = profileButton;
             AnimatedGIFSprite::createAsync(path, [safeThis, safeProfileBtn, targetSize, shapeName, picCfg](AnimatedGIFSprite* anim) {
                if (!anim || !safeProfileBtn->getParent()) return;

                auto container = buildProfileClipContainer(anim, shapeName, targetSize, picCfg);
                if (container) {
                    safeProfileBtn->setNormalImage(container);
                }
            });
        } else {
            auto sprite = CCSprite::create(path.c_str());
            if (!sprite) return;

            auto container = buildProfileClipContainer(sprite, shapeName, targetSize, picCfg);
            if (container) {
                profileButton->setNormalImage(container);
            }
        }
    }
};
