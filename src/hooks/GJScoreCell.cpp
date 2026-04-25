#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/moderation/ui/ModeratorsLayer.hpp"
#include "../features/thumbnails/services/ThumbsRegistry.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"

using namespace Shaders;

// helpers pa los efectos premium
namespace {
   
}

namespace {
    struct ButtonMoveCache {
        bool initialized = false;
        float buttonOffset = 30.f;

        void reset() {
            initialized = false;
        }
    };

    ButtonMoveCache g_buttonCache;
}

class $modify(PaimonGJScoreCell, GJScoreCell) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("GJScoreCell::loadFromScore", geode::Priority::Late);
    }

    void onExit() {
        if (auto f = m_fields.self()) {
            f->m_isBeingDestroyed = true;
            hideLoadingSpinner();
        }
        GJScoreCell::onExit();
    }

    struct Fields {
        Ref<CCClippingNode> m_profileClip = nullptr;
        Ref<CCLayerColor> m_profileSeparator = nullptr;
        Ref<CCNode> m_profileBg = nullptr;
        Ref<CCLayerColor> m_darkOverlay = nullptr;
        bool m_buttonsMoved = false; // pa no andar moviendo botones mil veces
        Ref<geode::LoadingSpinner> m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false; // pa no tocar celdas que ya se mueren
    };
    
    void showLoadingSpinner() {
        auto f = m_fields.self();
        
        // quita el spinner viejo si ya habia
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
        
        // crear spinner usando geode::LoadingSpinner (10px diametro â‰ˆ loadingCircle.png * 0.25)
        auto spinner = geode::LoadingSpinner::create(10.f);
        
        // lo pongo a la derecha donde iria la mini
        auto cs = this->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) {
            cs.width = this->m_width;
            cs.height = this->m_height;
        }
        spinner->setPosition({35.f, cs.height / 2.f + 20.f});
        spinner->setZOrder(999);
        
        spinner->setID("paimon-loading-spinner"_spr);
        
        this->addChild(spinner);
        f->m_loadingSpinner = spinner;
    }
    
    void hideLoadingSpinner() {
        auto f = m_fields.self();
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
    }

    // mover CCLayerColor del juego detras de nuestro fondo
    void pushGameColorLayersBehind(CCNode* node) {
        if (!node) return;
        // no toques los nodos del mod (id empieza con "paimon-")
        std::string id = node->getID();
        if (!id.empty() && id.find("paimon-") == 0) return;

        bool isBackground = false;
        if (geode::cast::typeinfo_cast<CCLayerColor*>(node) != nullptr) isBackground = true;
        else if (geode::cast::typeinfo_cast<CCScale9Sprite*>(node) != nullptr) isBackground = true;

        if (isBackground) {
            // empuja solo si aun no esta al fondo
            if (node->getZOrder() > -20) {
                if (auto parent = node->getParent()) parent->reorderChild(node, -20);
                else node->setZOrder(-20);
            }
        }
        // recursivo pa limpiar todo el arbol
        auto children = CCArrayExt<CCNode*>(node->getChildren());
        for (auto* ch : children) pushGameColorLayersBehind(ch);
    }

    void addOrUpdateProfileThumb(CCTexture2D* texture) {
        // dejo textura null si hay gifKey, se apana con eso
        
        // check critico: la celda tiene que tener parent
            if (!this->getParent()) {
                log::warn("[GJScoreCell] Cell has no parent, skipping addOrUpdateProfileThumb");
                return;
            }
            
            log::info("[GJScoreCell] addOrUpdateProfileThumb called");

            auto f = m_fields.self();
            if (!f) {
                log::error("[GJScoreCell] Fields are null in addOrUpdateProfileThumb");
                return;
            }
            
            // mira si la celda ya se esta destruyendo
            if (f->m_isBeingDestroyed) {
                log::debug("[GJScoreCell] Cell marked as destroyed, skipping thumbnail update");
                return;
            }
            
            log::debug("[GJScoreCell] Starting profile thumbnail update");
            
            // limpieza agresiva pa que no se apilen cosas raras
            // recoger nodos del mod para eliminar en segundo paso (evita
            // mutar el array de children durante iteracion)
            if (auto children = this->getChildren()) {
                std::vector<CCNode*> toRemove;
                for (auto* node : CCArrayExt<CCNode*>(children)) {
                    if (!node) continue;
                    std::string id = node->getID();
                    if (id == "paimon-profile-bg"_spr ||
                        id == "paimon-profile-clip"_spr ||
                        id == "paimon-profile-thumb"_spr ||
                        id == "paimon-score-bg-clipper"_spr ||
                        id == "paimon-profile-separator"_spr) {
                        toRemove.push_back(node);
                    }
                }
                for (auto* node : toRemove) {
                    node->removeFromParent();
                }
            }
            
            f->m_profileClip = nullptr;
            f->m_profileSeparator = nullptr;
            f->m_profileBg = nullptr;
            f->m_darkOverlay = nullptr;

            // geometria base segun tamano de la celda
            auto cs = this->getContentSize();
            if (cs.width <= 0 || cs.height <= 0) {
                log::error("[GJScoreCell] Invalid cell content size: {}x{}", cs.width, cs.height);
                return;
            }
            if (cs.width <= 1.f || cs.height <= 1.f) {
                cs.width = this->m_width;
                cs.height = this->m_height;
            }

            // --- logica del fondo (gradiente vs mini blur) ---
            std::string bgType = "gradient";
            float blurIntensity = 3.0f;
            float darkness = 0.2f;
            ccColor3B colorA = {255,255,255};
            ccColor3B colorB = {255,255,255};
            bool useGradient = false;
            std::string gifKey = "";

            bool isCurrentUser = false;
            if (this->m_score) isCurrentUser = this->m_score->isCurrentUser();
            
            int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
            auto config = ProfileThumbs::get().getProfileConfig(accountID);

            // gifKey siempre desde config (inyectado desde cache)
            gifKey = config.gifKey;

            if (isCurrentUser) {
                // Usuario actual: usa config local
                bgType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
                blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
                darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
            } else {
                // Otros usuarios: intenta config desde cache
                if (config.hasConfig) {
                    bgType = config.backgroundType;
                    blurIntensity = config.blurIntensity;
                    darkness = config.darkness;
                    useGradient = config.useGradient;
                    colorA = config.colorA;
                    colorB = config.colorB;
                } else {
                    // Sin config: usa defaults
                    bgType = "thumbnail"; // por defecto miniatura blur
                    // mantener otros defaults
                }
            }
            
            // Validacion basica
            if (!texture && gifKey.empty()) {
                log::error("[GJScoreCell] No texture and no GIF key available for account {}", accountID);
                return;
            }

            // Fuerza modo miniatura si hay textura/gif
            if (bgType == "gradient" && (texture || !gifKey.empty())) {
                bgType = "thumbnail";
            }

            if (bgType == "none") {
                // Deja fondo del juego
            }
            else if (bgType == "thumbnail") {
                // Crea fondo con blur
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);

                CCNode* bgNode = nullptr;

                // Intenta GIF o Video primero
                if (!gifKey.empty()) {
                    // Verifica video en cache
                    if (VideoThumbnailSprite::isCached(gifKey)) {
                        auto bgVideo = VideoThumbnailSprite::createFromCache(gifKey);
                        if (bgVideo) {
                            float scaleX = targetSize.width / std::max(1.f, bgVideo->getContentSize().width);
                            float scaleY = targetSize.height / std::max(1.f, bgVideo->getContentSize().height);
                            bgVideo->setScale(std::max(scaleX, scaleY));
                            bgVideo->setAnchorPoint({0.5f, 0.5f});
                            bgVideo->setPosition(targetSize * 0.5f);

                            auto shader = Shaders::getBlurCellShader();
                            if (shader) {
                                bgVideo->setShaderProgram(shader);
                            }

                            bgVideo->play();
                            bgVideo->setID("paimon-bg-sprite"_spr);
                            bgNode = bgVideo;
                        }
                    }

                    // Fallback a GIF
                    if (!bgNode) {
                    // Usa AnimatedGIFSprite como fondo con blur
                    auto bgGif = AnimatedGIFSprite::createFromCache(gifKey);
                    if (bgGif) {
                        // Escala para cubrir area
                        float scaleX = targetSize.width / bgGif->getContentSize().width;
                        float scaleY = targetSize.height / bgGif->getContentSize().height;
                        float scale = std::max(scaleX, scaleY);

                        bgGif->setScale(scale);
                        bgGif->setAnchorPoint({0.5f, 0.5f});
                        bgGif->setPosition(targetSize * 0.5f);

                        // Configura blur via shader
                        float norm = (blurIntensity - 1.0f) / 9.0f;
                        bgGif->m_intensity = std::min(1.7f, norm * 2.5f);
                        if (bgGif->getTexture()) {
                            bgGif->m_texSize = bgGif->getTexture()->getContentSizeInPixels();
                        }

                        auto shader = Shaders::getBlurCellShader();
                        if (shader) {
                            bgGif->setShaderProgram(shader);
                        }

                        bgGif->play();
                        bgGif->setID("paimon-bg-sprite"_spr);
                        bgNode = bgGif;
                    }
                    } // GIF fallback
                }
                
                // Fallback a textura
                if (!bgNode && texture) {
                    // Imagen estatica: blur multi-paso
                    CCSize blurTargetSize = cs;
                    blurTargetSize.width = std::max(blurTargetSize.width, 512.f);
                    blurTargetSize.height = std::max(blurTargetSize.height, 256.f);

                    float stronger = std::min(10.0f, blurIntensity + 3.0f); // Mas blur
                    auto blurredBg = BlurSystem::getInstance()->createBlurredSprite(texture, blurTargetSize, stronger);
                    if (blurredBg) {
                        blurredBg->setPosition(blurTargetSize * 0.5f);
                        bgNode = blurredBg;
                    } else {
                        // Fallback final: textura con shader
                        auto tempSprite = CCSprite::createWithTexture(texture);
                        float scaleX = blurTargetSize.width / texture->getContentSize().width;
                        float scaleY = blurTargetSize.height / texture->getContentSize().height;
                        float scale = std::max(scaleX, scaleY);

                        tempSprite->setScale(scale);
                        tempSprite->setPosition(blurTargetSize * 0.5f);

                        auto shader = Shaders::getBlurCellShader();
                        if (shader) {
                            tempSprite->setShaderProgram(shader);
                        }
                        bgNode = tempSprite;
                    }
                }

                if (bgNode) {
                    // Crea clipper para fondo
                    auto stencil = paimon::SpriteHelper::createRectStencil(cs.width, cs.height);
                    
                    auto clipper = CCClippingNode::create(stencil);
                    clipper->setContentSize(cs);
                    clipper->setPosition({0,0});
                    clipper->setZOrder(-2); // Al fondo
                    clipper->setID("paimon-score-bg-clipper"_spr);

                    // Escala bgNode a tamano de celda
                    CCSize bgSize = bgNode->getContentSize();
                    if (bgSize.width > 0 && bgSize.height > 0) {
                        float scaleToFitX = cs.width / bgSize.width;
                        float scaleToFitY = cs.height / bgSize.height;
                        float finalScale = std::max(scaleToFitX, scaleToFitY);
                        bgNode->setScale(finalScale);
                    }
                    bgNode->setAnchorPoint({0.5f, 0.5f});
                    bgNode->setPosition(cs / 2);
                    
                    clipper->addChild(bgNode);
                    this->addChild(clipper);
                    f->m_profileBg = clipper;

                    // Capa oscura encima
                    if (darkness > 0.0f) {
                        auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(darkness * 255)});
                        overlay->setContentSize(cs);
                        overlay->setPosition({0, 0});
                        overlay->setZOrder(-1); 
                        this->addChild(overlay);
                        f->m_darkOverlay = overlay;
                    }

                    // Empuja fondo original atras
                    pushGameColorLayersBehind(this);
                }
            }

            // Logica del sprite principal
            CCNode* mainNode = nullptr;
            float contentW = 0, contentH = 0;

            // Prueba video primero
            if (!gifKey.empty() && VideoThumbnailSprite::isCached(gifKey)) {
                auto videoSprite = VideoThumbnailSprite::createFromCache(gifKey);
                if (videoSprite) {
                    mainNode = videoSprite;
                    contentW = videoSprite->getContentSize().width;
                    contentH = videoSprite->getContentSize().height;
                    videoSprite->play();
                    videoSprite->setID("paimon-profile-thumb-video"_spr);
                }
            }

            // luego pruebo con GIF
            if (!mainNode && !gifKey.empty()) {
                log::debug("[GJScoreCell] Trying to create GIF sprite from cache key: {}", gifKey);

                // Verifica GIF en cache
                if (AnimatedGIFSprite::isCached(gifKey)) {
                    log::debug("[GJScoreCell] GIF is cached, creating sprite...");
                    auto gifSprite = AnimatedGIFSprite::createFromCache(gifKey);
                    if (gifSprite) {
                        mainNode = gifSprite;
                        contentW = gifSprite->getContentSize().width;
                        contentH = gifSprite->getContentSize().height;

                        // Asegura que la animacion corra
                        gifSprite->play();

                        gifSprite->setID("paimon-profile-thumb-gif"_spr);
                        log::debug("[GJScoreCell] Created GIF sprite from key: {}, size: {}x{}, frames: {}",
                            gifKey, contentW, contentH, gifSprite->getFrameCount());

                    } else {
                        log::warn("[GJScoreCell] createFromCache returned null for key: {}", gifKey);
                    }
                } else {
                    log::warn("[GJScoreCell] GIF not in cache for key: {}", gifKey);
                }
            }
            
            // Sin GIF: usa textura
            if (!mainNode && texture) {
                auto sprite = CCSprite::createWithTexture(texture);
                if (sprite) {
                    mainNode = sprite;
                    contentW = sprite->getContentWidth();
                    contentH = sprite->getContentHeight();
                    sprite->setID("paimon-profile-thumb"_spr);
                }
            }

            if (!mainNode) {
                log::error("[GJScoreCell] Failed to create main sprite");
                return;
            }

        // Verifica que la celda siga existiendo
        if (!this->getParent()) {
            log::warn("[GJScoreCell] Cell was destroyed before thumbnail could be added");
            return;
        }

        
        log::debug("[GJScoreCell] Cell size: {}x{}", cs.width, cs.height);

            // Escala en ancho con factor
            float factor = 0.80f;
            
            if (isCurrentUser) {
                factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f);
            } else {
                // Otros usuarios: config en cache
                int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
                auto config = ProfileThumbs::get().getProfileConfig(accountID);
                if (config.hasConfig) {
                    factor = config.widthFactor;
                } else {
                    // Sin config: usa default
                    factor = 0.60f; 
                }
            }
            
            factor = std::max(0.30f, std::min(0.95f, factor));
            float desiredWidth = cs.width * factor;

            float scaleY = cs.height / contentH;
            float scaleX = desiredWidth / contentW;

            mainNode->setScaleY(scaleY);
            mainNode->setScaleX(scaleX);

        // Recorte en angulo del lado derecho
    constexpr float angle = 18.f;
        CCSize scaledSize{ desiredWidth, contentH * scaleY };
        auto mask = paimon::SpriteHelper::createRectStencil(scaledSize.width, scaledSize.height);
        mask->setAnchorPoint({1,0});
        mask->ignoreAnchorPointForPosition(true);
        mask->setSkewX(angle);

        auto clip = CCClippingNode::create();
        clip->setStencil(mask);
        clip->setContentSize(scaledSize);
        clip->setAnchorPoint({1,0});
        // Pegado al borde derecho con offset
        clip->setPosition({ cs.width, 0.3f });
        clip->setID("paimon-profile-clip"_spr);
    // Detras de textos/iconos
    clip->setZOrder(-1);

        mainNode->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(mainNode);
        
        this->addChild(clip);
        f->m_profileClip = clip;
        
        // Aplica efectos premium
        bool isPremiumUser = false;

        // Bordes alrededor de la mini
        constexpr float borderThickness = 2.f;
        ccColor4B borderColor = isPremiumUser ? ccc4(255, 215, 0, 200) : ccc4(0, 0, 0, 120);

        auto makeBorder = [&](CCSize bSize, CCPoint pos, std::string_view id, float skew) {
            auto b = CCLayerColor::create(borderColor);
            b->setContentSize(bSize);
            b->setAnchorPoint({1, 0});
            b->setSkewX(skew);
            b->setPosition(pos);
            b->setZOrder(-1);
            b->setID(std::string(id).c_str());
            if (isPremiumUser) {
                b->runAction(CCRepeatForever::create(CCSequence::create(
                    CCFadeTo::create(0.8f, 255), CCFadeTo::create(0.8f, 180), nullptr
                )));
            }
            this->addChild(b);
            return b;
        };

        makeBorder({scaledSize.width, borderThickness}, {cs.width, 0.3f + scaledSize.height}, "paimon-profile-border-top"_spr, angle);
        makeBorder({scaledSize.width, borderThickness}, {cs.width, 0.3f - borderThickness}, "paimon-profile-border-bottom"_spr, angle);
        makeBorder({borderThickness, scaledSize.height + borderThickness * 2}, {cs.width, 0.3f - borderThickness}, "paimon-profile-border-right"_spr, 0.f);

    // separador detras de la imagen (estilo fijo)
    auto sep = CCLayerColor::create(ccc4(0, 0, 0, 50));
    sep->setScaleX(0.45f);
        sep->ignoreAnchorPointForPosition(false);
        sep->setSkewX(angle * 2);
        sep->setContentSize(scaledSize);
        sep->setAnchorPoint({1,0});
        sep->setPosition({ cs.width - sep->getContentSize().width / 2 - 16.f, 0.3f });
    sep->setZOrder(-2);
        sep->setID("paimon-profile-separator"_spr);
        this->addChild(sep);
        f->m_profileSeparator = sep;

        // el fondo ya lo meti mas arriba
        log::debug("[GJScoreCell] Profile thumbnail added successfully");
    }

    $override void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        
        // empujar capas de color del juego atras pa que se vea el gradiente
        pushGameColorLayersBehind(this);

        if (!score) return;
        log::info("[GJScoreCell] loadFromScore: accountID={} user={}", score->m_accountID, std::string(score->m_userName));

            int accountID = score->m_accountID;
            if (accountID <= 0) return;

            {
                // primero cache y solo descargo si toca
                std::string username = score->m_userName;
                if (username.empty()) {
                    log::warn("[GJScoreCell] Username empty for account {}", accountID);
                    return;
                }
                
                // comprobar cache primero
                auto cachedProfile = ProfileThumbs::get().getCachedProfile(accountID);
                bool wantsGifProfile = cachedProfile && !cachedProfile->gifKey.empty();
                bool hasReadyCachedGif = wantsGifProfile && AnimatedGIFSprite::isCached(cachedProfile->gifKey);
                bool hasReadyCachedProfile = cachedProfile && (hasReadyCachedGif || (!wantsGifProfile && cachedProfile->texture));
                if (hasReadyCachedProfile) {
                    log::debug("[GJScoreCell] Found cached profile for account {}", accountID);
                    // cargo desde cache de forma asincrona
                    WeakRef<PaimonGJScoreCell> safeThis = this;
                    Loader::get()->queueInMainThread([safeThis, accountID]() {
                        auto selfRef = safeThis.lock();
                        auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                        if (!self || !self->getParent()) return;

                        auto cached = ProfileThumbs::get().getCachedProfile(accountID);
                        if (cached) {
                            self->addOrUpdateProfileThumb(cached->texture);
                        } else {
                            log::warn("[GJScoreCell] Cache entry disappeared for account {}", accountID);
                        }
                    });
                    return;
                }
                
                if (wantsGifProfile && !hasReadyCachedGif) {
                    log::info("[GJScoreCell] GIF cache cold for account {}, re-downloading profile image", accountID);
                } else {
                    log::debug("[GJScoreCell] No cache for account {}, downloading...", accountID);
                }
                
                // no esta en cache: toca descargar del server
                log::debug("[GJScoreCell] Profile not in cache for user: {} - Downloading...", username);
                
                bool enableSpinners = true;
                
                if (enableSpinners) {
                    showLoadingSpinner();
                }
                
                WeakRef<PaimonGJScoreCell> safeRef = this;

                // uso queueLoad en vez de bajar directo
                ProfileThumbs::get().queueLoad(accountID, username, [safeRef, accountID, enableSpinners](bool success, CCTexture2D* texture) {
                    auto selfRef = safeRef.lock();
                    auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                    if (!self) return;

                    if (!success) {
                        if (enableSpinners) self->hideLoadingSpinner();
                        log::warn("[GJScoreCell] Failed to download profile for account {}", accountID);
                        return;
                    }

                    // Para perfiles GIF, texture puede ser null pero el GIF ya esta en cache
                    // via cacheProfileGIF. Permitir que continue para que addOrUpdateProfileThumb
                    // lo detecte via gifKey.
                    if (!texture) {
                        // Verificar si hay un GIF cacheado para este perfil
                        auto cachedEntry = ProfileThumbs::get().getCachedProfile(accountID);
                        if (!cachedEntry || cachedEntry->gifKey.empty()) {
                            if (enableSpinners) self->hideLoadingSpinner();
                            log::warn("[GJScoreCell] No texture and no GIF for account {}", accountID);
                            return;
                        }
                        // GIF-only profile: continuar sin texture
                    }

                    // Ref<> para que no se autorelease en la siguiente llamada async
                    Ref<CCTexture2D> safeTex = texture;

                    // descargar config
                    ThumbnailAPI::get().downloadProfileConfig(accountID, [safeRef, accountID, safeTex, enableSpinners](bool success2, ProfileConfig const& config) {
                        auto selfRef = safeRef.lock();
                        auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                        if (!self) return;
                        if (enableSpinners) self->hideLoadingSpinner();

                        // guardo en cache
                        if (safeTex) {
                            ProfileThumbs::get().cacheProfile(accountID, safeTex, {255,255,255}, {255,255,255}, 0.5f);
                        }
                        if (success2) {
                            ProfileThumbs::get().cacheProfileConfig(accountID, config);
                        }

                        // aplico la textura al final
                        self->addOrUpdateProfileThumb(safeTex);
                        // Ref<> gestiona el refcount automaticamente
                    });
                });
            }

        // muevo el boton de perfil del jugador (tirando del cache ese)
        auto f = m_fields.self();
        if (!f->m_buttonsMoved) {
            f->m_buttonsMoved = true; // marcado como ya procesado
            
            // init del cache solo una vez
                if (!g_buttonCache.initialized) {
                        g_buttonCache.buttonOffset = 0.0f;
                    g_buttonCache.initialized = true;
                    log::debug("[GJScoreCell] Button cache initialized with offset: {}", g_buttonCache.buttonOffset);
                }
                
                // si el offset es 0, ni me molesto
                if (g_buttonCache.buttonOffset <= 0.01f) {
                    return;
                }
                
                // busco y muevo botones solo entre hijos directos
                auto children = this->getChildren();
                if (!children) return;
                
                bool foundButton = false;
                
                // limito la busqueda a los primeros 10 nodos (los menus suelen estar al inicio)
                int searchCount = 0;

                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if (foundButton || searchCount >= 10) break;
                    searchCount++;

                    auto menu = typeinfo_cast<CCMenu*>(child);
                    if (!menu) continue;

                    auto menuChildren = menu->getChildren();
                    if (!menuChildren) continue;
                    
                    // solo miro los primeros 5 items del menu
                    int menuSearchCount = 0;

                    for (auto* menuChild : CCArrayExt<CCNode*>(menuChildren)) {
                        if (foundButton || menuSearchCount >= 5) break;
                        menuSearchCount++;

                        auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(menuChild);
                        if (!btn) continue;
                        
                        auto btnID = btn->getID();
                        
                        // ignoro botones del mod (solo miro el prefijo)
                        std::string btnIDStr = btnID;
                        if (btnIDStr.empty() || btnIDStr.compare(0, 7, "paimon-") != 0) {
                            auto currentPos = btn->getPosition();
                            
                            // lo muevo solo si esta en una posicion razonable
                            if (currentPos.x > 50.f && currentPos.x < 400.f) {
                                btn->setPosition({currentPos.x - g_buttonCache.buttonOffset, currentPos.y});
                                foundButton = true;
                                log::debug("[GJScoreCell] Moved button: {}x{} -> {}x{}", 
                                         currentPos.x, currentPos.y, 
                                         currentPos.x - g_buttonCache.buttonOffset, currentPos.y);
                                break;
                            }
                    }
                }
            }
        }

        // ModeratorsLayer: ocultar rank-label en celdas de la lista de moderadores (unificado aquí para evitar doble $modify sobre GJScoreCell)
        if (ModeratorsLayer::s_instance && ModeratorsLayer::s_instance->isScoreInList(score)) {
            WeakRef<PaimonGJScoreCell> self = this;
            Loader::get()->queueInMainThread([self]() {
                auto cellRef = self.lock();
                auto* cell = static_cast<PaimonGJScoreCell*>(cellRef.data());
                if (!cell) return;
                if (auto rankLabel = cell->getChildByID("rank-label")) {
                    rankLabel->setVisible(false);
                }
            });
        }
    }

    // hook de draw quitado pa evitar crashes feos
};
