#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/GameManager.hpp>
#include "../utils/Localization.hpp"
#include "../utils/Debug.hpp"
#include "../layers/UserThumbnailsLayer.hpp"
#include <chrono>
#include <cmath>
#include <optional>
#include <vector>
#include <fstream>
#include <mutex>
#include <atomic>
#include <list>
#include "../utils/FileDialog.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../features/moderation/ui/AddModeratorPopup.hpp"
#include "../features/moderation/ui/BanUserPopup.hpp"
#include "../utils/Assets.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"
#include "../features/moderation/services/ModerationService.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../features/profiles/ui/RateProfilePopup.hpp"
#include "../features/profiles/ui/ProfileReviewsPopup.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include "../features/profiles/services/ProfileImageCache.hpp"
#include "../core/Settings.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "../utils/BetaUploadWarning.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include "../features/moderation/services/ModeratorCache.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include "../utils/FormatDetect.hpp"
#include "../utils/GIFDecoder.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/profiles/ui/ProfileSettingsPopup.hpp"
#include "../features/profiles/ui/ProfileBgPickerPopup.hpp"
#include "../features/profiles/ui/ProfileBgGradientPopup.hpp"
#include "../features/profiles/services/ProfileGradientEffects.hpp"
#include "../features/forum/services/ForumApi.hpp"
#include "../features/profiles/ui/CommentBgSettingsPopup.hpp"
#include "../features/profiles/ui/CustomBadgePickerPopup.hpp"
#include "../features/profiles/services/CustomBadgeService.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"
#include "../features/profiles/ui/ProfileViewsPopup.hpp"

using namespace geode::prelude;

namespace {
bool shouldHideVanillaCommentBgNode(cocos2d::CCNode* node) {
    if (!node) return false;
    std::string nodeID = node->getID();
    if (!nodeID.empty()) {
        if (nodeID.find("paimon-") != std::string::npos) return false;
        if (nodeID == "background" || nodeID == "comment-background" ||
            nodeID == "left-border" || nodeID == "right-border" ||
            nodeID == "top-border" || nodeID == "bottom-border") {
            return true;
        }
    }
    return typeinfo_cast<CCLayerColor*>(node) || typeinfo_cast<CCScale9Sprite*>(node);
}
}

// CCScale9Sprite::create crashea si el sprite no existe (no retorna nullptr).
// Usar paimon::SpriteHelper::safeCreateScale9() del header compartido.

// cache de texturas de profileimg para carga instantanea entre popups.
// Usa Ref<> para manejo automatico de refcount, con guardia de shutdown
// para evitar release() cuando el CCPoolManager ya este destruido.
// (Implementacion del cache de profileimg movida a
//  features/profiles/services/ProfileImageCache.cpp)


// Limpiar el cache de profileimg durante el cierre del juego.
// Los destructores estaticos se ejecutan en orden indefinido y
// CCPoolManager puede ya estar muerto Ã¢â‚¬â€ usamos take() para sacar
// los Ref<> sin llamar release().






    // skip MP4 video files (handled by ensureAnimatedProfileImg)
    // ftyp box can start at different offsets â€” search first 12 bytes



class $modify(PaimonProfilePage, ProfilePage) {
    static void onModify(auto& self) {
        // Depende de node IDs estables
        (void)self.setHookPriorityAfterPost("ProfilePage::loadPageFromUserInfo", "geode.node-ids");
    }

    struct Fields {
        Ref<CCMenuItemSpriteExtra> m_gearBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_addModBtn = nullptr;       // Boton add-moderador (solo admins)
        Ref<CCMenuItemSpriteExtra> m_banBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_musicPauseBtn = nullptr;  // Boton pausar musica
        Ref<CCClippingNode> m_profileImgClip = nullptr;   // Clip imagen de perfil
        Ref<CCNode> m_profileImgBorder = nullptr;          // Borde imagen de perfil
        bool m_isApprovedMod = false;
        bool m_isAdmin = false;
        bool m_musicPlaying = false;  // Estado musica
        bool m_menuMusicPaused = false; // Musica del menu pausada
        // Estado fade de musica
        int m_fadeStep = 0;
        int m_fadeTotalSteps = 0;
        float m_fadeFromVol = 0.0f;
        float m_fadeToVol = 0.0f;
        bool m_hasProfileBackdrop = false;
        bool m_leaveForClose = false;
        bool m_pausedForTemporaryExit = false;
        bool m_audioCleanedUp = false;
        // WeakRef<>: el statsMenu de GD puede ser reconstruido por otros mods,
        // dejando este label dangling. WeakRef nos permite chequear con lock().
        WeakRef<CCLabelBMFont> m_thumbCountLabel;
        int64_t m_statusLastSeen = 0;
        bool m_statusOnline = false;

        // PERF: cache del username-menu para evitar getChildByIDRecursive en
        // hot paths. La pagina del perfil hace ~9 lookups del mismo nodo
        // durante init + clicks de botones; cada uno recorre todo el arbol
        // en DFS (~0.1-0.5ms en arboles grandes). WeakRef se invalida solo
        // cuando el nodo desaparece (ej. layout reset por otro mod).
        WeakRef<CCMenu> m_usernameMenuCached = nullptr;
    };

    // PERF: helper que cachea el username-menu via WeakRef. El primer call hace
    // getChildByIDRecursive y guarda; calls subsiguientes son lookups en una
    // refcell (~5ns vs ~100us para DFS recursivo del arbol).
    //
    // Si el nodo fue removido (otro mod cambia layout, scene reload), WeakRef::lock
    // devuelve null y volvemos a buscar. typeinfo_cast filtra por si el ID lo
    // toma un nodo de otro tipo.
    CCMenu* getUsernameMenu() {
        if (auto cached = m_fields->m_usernameMenuCached.lock()) {
            // Validar que sigue en el arbol del ProfilePage; si otro mod lo
            // movio fuera, invalidamos y re-buscamos.
            if (cached->getParent() && cached->hasAncestor(this)) {
                return cached.data();
            }
        }
        auto* found = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("username-menu"));
        if (found) {
            m_fields->m_usernameMenuCached = found;
        }
        return found;
    }

    bool canShowModerationControls() {
        // Controles si es mod o admin
        return m_fields->m_isApprovedMod || m_fields->m_isAdmin;
    }

    // Obtener left-menu de forma segura
    CCMenu* getLeftMenu() {
        if (!this->m_mainLayer) return nullptr;
        auto node = this->m_mainLayer->getChildByID("left-menu");
        return node ? typeinfo_cast<CCMenu*>(node) : nullptr;
    }

    // Obtener socials-menu de forma segura
    CCMenu* getSocialsMenu() {
        if (!this->m_mainLayer) return nullptr;
        auto node = this->m_mainLayer->getChildByID("socials-menu");
        return node ? typeinfo_cast<CCMenu*>(node) : nullptr;
    }

    // Escala sprite a tamano cuadrado
    static void scaleToFit(CCNode* spr, float targetSize) {
        if (!spr) return;
        float curSize = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (curSize > 0) spr->setScale(targetSize / curSize);
    }

    // Crea boton gear si no existe
    void ensureGearButton(CCMenu* menu) {
        if (!menu || m_fields->m_gearBtn) return;
        if (menu->getChildByID("thumbs-gear-button"_spr)) return;

        auto gearSpr = Assets::loadButtonSprite(
            "profile-gear",
            "frame:GJ_optionsBtn02_001.png",
            [](){
                auto s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
                if (!s) s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
                if (!s) s = CCSprite::create();
                return s;
            }
        );
        scaleToFit(gearSpr, 26.f);
        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(PaimonProfilePage::onOpenThumbsCenter));
        gearBtn->setID("thumbs-gear-button"_spr);
        menu->addChild(gearBtn);
        m_fields->m_gearBtn = gearBtn;
    }

    // Crea boton add-moderator si no existe
    void ensureAddModeratorButton(CCMenu* menu) {
        if (!menu || m_fields->m_addModBtn) return;
        if (menu->getChildByID("add-moderator-button"_spr)) return;

        auto addModSpr = Assets::loadButtonSprite(
            "add-moderator",
            "frame:GJ_plus2Btn_001.png",
            [](){
                auto s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plus2Btn_001.png");
                if (!s) s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
                if (!s) s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
                return s;
            }
        );
        scaleToFit(addModSpr, 26.f);
        auto addModBtn = CCMenuItemSpriteExtra::create(addModSpr, this, menu_selector(PaimonProfilePage::onOpenAddModerator));
        addModBtn->setID("add-moderator-button"_spr);
        menu->addChild(addModBtn);
        m_fields->m_addModBtn = addModBtn;
    }

    // Verificador periodico de integridad de botones
    void verifyButtonIntegrity(float dt) {
        if (!this->getParent()) return;
        if (!this->m_mainLayer) return;
        auto* leftMenu = getLeftMenu();
        if (!leftMenu) return;

        bool needsLayout = false;

        // 1. Boton de ban: visibilidad segun rango
        if (!m_fields->m_banBtn || !m_fields->m_banBtn->getParent()) {
            // Recrea boton de ban si se perdio
            auto banSpr = ButtonSprite::create("X", 40, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
            banSpr->setScale(0.5f);
            auto banBtn = CCMenuItemSpriteExtra::create(banSpr, this, menu_selector(PaimonProfilePage::onBanUser));
            banBtn->setID("ban-user-button"_spr);
            banBtn->setVisible(false);
            leftMenu->addChild(banBtn);
            m_fields->m_banBtn = banBtn;
            needsLayout = true;
            log::debug("[ProfilePage] Boton de ban recreado por verificador de integridad");
        }

        // Actualiza visibilidad del ban
        {
            bool shouldShow = !this->m_ownProfile && (m_fields->m_isApprovedMod || m_fields->m_isAdmin);
            if (m_fields->m_banBtn->isVisible() != shouldShow) {
                m_fields->m_banBtn->setVisible(shouldShow);
                m_fields->m_banBtn->setEnabled(shouldShow);
                needsLayout = true;
            }
        }

        // 2. Boton de reviews
        if (!leftMenu->getChildByID("profile-reviews-btn"_spr)) {
            auto reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_chatBtn_001.png");
            if (!reviewIcon) reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
            if (reviewIcon) {
                scaleToFit(reviewIcon, 26.f);
                auto reviewBtn = CCMenuItemSpriteExtra::create(reviewIcon, this, menu_selector(PaimonProfilePage::onProfileReviews));
                reviewBtn->setID("profile-reviews-btn"_spr);
                leftMenu->addChild(reviewBtn);
                needsLayout = true;
                log::debug("[ProfilePage] Boton de reviews recreado por verificador de integridad");
            }
        }

        // 3. Boton gear
        if (this->m_ownProfile && (m_fields->m_isApprovedMod || m_fields->m_isAdmin)) {
            if (!m_fields->m_gearBtn || !m_fields->m_gearBtn->getParent()) {
                m_fields->m_gearBtn = nullptr;
                ensureGearButton(leftMenu);
                needsLayout = true;
                log::debug("[ProfilePage] Boton gear recreado por verificador de integridad");
            }
        }

        // 4. Boton add moderator
        if (this->m_ownProfile && m_fields->m_isAdmin) {
            if (!m_fields->m_addModBtn || !m_fields->m_addModBtn->getParent()) {
                m_fields->m_addModBtn = nullptr;
                ensureAddModeratorButton(leftMenu);
                needsLayout = true;
                log::debug("[ProfilePage] Boton add-mod recreado por verificador de integridad");
            }
        }

        // 5. Mantener status dot al final del username-menu (a la derecha del nombre)
        if (auto* usernameMenu = getUsernameMenu()) {
            if (auto* dot = usernameMenu->getChildByID("paimon-user-status-dot"_spr)) {
                if (dot->getParent() != usernameMenu) {
                    dot->retain();
                    dot->removeFromParent();
                    usernameMenu->addChild(dot);
                    dot->release();
                    if (auto menuNode = typeinfo_cast<CCMenu*>(usernameMenu)) {
                        menuNode->updateLayout();
                    }
                }
            }
        }

        if (needsLayout) {
            leftMenu->updateLayout();
        }
    }

    // Badge de moderador/admin en el perfil

    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    void addModeratorBadge(bool isMod, bool isAdmin) {
        // Busca menu del username
        auto menu = getUsernameMenu();
        if (!menu) return;

        // Evita duplicados
        if (menu->getChildByID("paimon-moderator-badge"_spr)) return;
        if (menu->getChildByID("paimon-admin-badge"_spr)) return;

        CCSprite* badgeSprite = nullptr;
        std::string badgeID;

        if (isAdmin) {
            badgeSprite = CCSprite::create("paim_Admin.png"_spr);
            badgeID = "paimon-admin-badge"_spr;
        } else if (isMod) {
            badgeSprite = CCSprite::create("paim_Moderador.png"_spr);
            badgeID = "paimon-moderator-badge"_spr;
        }

        if (!badgeSprite) return;

        log::info("Adding badge (Clickable) - Admin: {}, Mod: {}", isAdmin, isMod);

        float targetHeight = 20.0f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(PaimonProfilePage::onPaimonBadge)
        );
        btn->setID(badgeID);

        if (auto menuNode = typeinfo_cast<CCMenu*>(menu)) {
            menuNode->addChild(btn);
            menuNode->updateLayout();
        }
    }

    void addUserStatusIndicator(bool online, int64_t lastSeen) {
        m_fields->m_statusOnline = online;
        m_fields->m_statusLastSeen = lastSeen;

        auto menu = getUsernameMenu();
        if (!menu) return;

        // Evita duplicados
        if (menu->getChildByID("paimon-user-status-dot"_spr)) return;

        // Circulo 20% mas pequeno, en la misma fila del nombre/badges.
        constexpr float dotRadius = 4.0f;
        auto* dotNode = PaimonDrawNode::create();
        if (!dotNode) return;

        ccColor4F fillColor;
        if (online) {
            fillColor = ccc4f(0.0f, 1.0f, 0.0f, 1.0f); // Verde intenso
        } else {
            fillColor = ccc4f(0.5f, 0.5f, 0.5f, 1.0f); // Gris
        }
        dotNode->drawSolidCircle({dotRadius, dotRadius}, dotRadius, fillColor);
        dotNode->setContentSize({dotRadius * 2, dotRadius * 2});
        dotNode->setAnchorPoint({0.5f, 0.5f});
        dotNode->ignoreAnchorPointForPosition(false);

        if (auto menuNode = typeinfo_cast<CCMenu*>(menu)) {
            auto dotBtn = CCMenuItemSpriteExtra::create(dotNode, this, menu_selector(PaimonProfilePage::onUserStatusDotClicked));
            dotBtn->setID("paimon-user-status-dot"_spr);
            menuNode->addChild(dotBtn);
            menuNode->updateLayout();
        }
    }

    void onUserStatusDotClicked(CCObject* sender) {
        if (this->m_ownProfile) {
            // Own profile: show profile views popup
            if (auto popup = ProfileViewsPopup::create(this->m_accountID)) {
                popup->show();
            }
            return;
        }

        std::string message;
        if (m_fields->m_statusOnline) {
            message = "This user is currently online.";
        } else if (m_fields->m_statusLastSeen > 0) {
            message = "Last seen: " + paimon::forum::formatAbsoluteTime(m_fields->m_statusLastSeen);
        } else {
            message = "This user is currently offline.\n(No last seen data available)";
        }
        FLAlertLayer::create("User Status", message.c_str(), "OK")->show();
    }

    void fetchAndShowUserStatus(int accountID) {
        Ref<ProfilePage> self = this;
        // Instant presence: send heartbeat first so the server marks us online,
        // then fetch the viewed user's status.
        paimon::forum::ForumApi::get().sendHeartbeat([self, accountID](paimon::forum::Result<bool> hbResult) {
            paimon::forum::ForumApi::get().getUserStatus(accountID,
                [self, accountID](paimon::forum::Result<paimon::forum::UserStatus> result) {
                    Loader::get()->queueInMainThread([self, accountID, result]() {
                        if (!self || !self->getParent()) return;
                        // static_cast is safe here: self was captured from this (a PaimonProfilePage).
                        // typeinfo_cast on a $modify class is forbidden by Geode.
                        auto* page = static_cast<PaimonProfilePage*>(self.data());
                        if (!page || page->m_accountID != accountID) return;
                        if (!result.ok) return;

                        page->addUserStatusIndicator(result.data.online, result.data.lastSeen);
                    });
                });
        });
    }

    void addCustomBadgeToProfile(std::string const& emoteName) {
        if (emoteName.empty()) return;

        auto menu = getUsernameMenu();
        if (!menu) return;

        // Evita duplicados
        if (menu->getChildByID("paimon-custom-badge"_spr)) return;

        auto emoteOpt = paimon::emotes::EmoteService::get().getEmoteByName(emoteName);
        if (!emoteOpt) return;

        auto emoteInfo = *emoteOpt;
        float targetHeight = 20.0f;

        Ref<PaimonProfilePage> self = this;

        paimon::emotes::EmoteCache::get().loadEmote(emoteInfo,
            [self, targetHeight, emoteName](cocos2d::CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
                if (!tex && !(isGif && !gifData.empty())) return;

                if (isGif && !gifData.empty()) {
                    auto dataCopy = gifData;
                    Loader::get()->queueInMainThread([self, targetHeight, emoteName, dataCopy = std::move(dataCopy)]() mutable {
                        if (!self->getParent()) return;
                        AnimatedGIFSprite::createAsync(dataCopy, emoteName, [self, targetHeight](AnimatedGIFSprite* gifSpr) {
                            if (!gifSpr || !self->getParent()) return;
                            auto menu = self->getUsernameMenu();
                            if (!menu) return;
                            if (menu->getChildByID("paimon-custom-badge"_spr)) return;
                            float maxDim = std::max(gifSpr->getContentWidth(), gifSpr->getContentHeight());
                            if (maxDim > 0) gifSpr->setScale(targetHeight / maxDim);
                            auto btn = CCMenuItemSpriteExtra::create(gifSpr, self, nullptr);
                            btn->setID("paimon-custom-badge"_spr);
                            menu->addChild(btn);
                            menu->updateLayout();
                        });
                    });
                } else {
                    Loader::get()->queueInMainThread([self, tex, targetHeight]() {
                        if (!self->getParent()) return;
                        auto menu = self->getUsernameMenu();
                        if (!menu) return;
                        if (menu->getChildByID("paimon-custom-badge"_spr)) return;
                        auto* spr = CCSprite::createWithTexture(tex);
                        if (!spr) return;
                        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
                        if (maxDim > 0) spr->setScale(targetHeight / maxDim);
                        auto btn = CCMenuItemSpriteExtra::create(spr, self, nullptr);
                        btn->setID("paimon-custom-badge"_spr);
                        menu->addChild(btn);
                        menu->updateLayout();
                    });
                }
            });
    }

    void onThumbnailCountClicked(CCObject*) {
        std::string username = getViewedUsername();
        int accountID = this->m_accountID;
        
        if (username.empty() || accountID <= 0) {
            PaimonNotify::create("Unable to load thumbnails", NotificationIcon::Warning)->show();
            return;
        }
        
        log::info("[ProfilePage] Opening thumbnails layer for user: {} (accountID: {})", username, accountID);
        
        auto scene = UserThumbnailsLayer::scene(username, accountID);
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.5f, scene));
    }

    void addThumbnailCountBadge(int uploadCount) {
        if (!this->m_mainLayer) return;

        // Find the stats-menu (Geode node-ids assigns this on ProfilePage)
        auto* statsMenu = this->m_mainLayer->getChildByIDRecursive("stats-menu");
        if (!statsMenu) {
            log::debug("[ProfilePage] stats-menu not found, skipping thumbnail count badge");
            return;
        }

        // Avoid duplicates
        if (statsMenu->getChildByID("paimon-thumb-count-btn"_spr)) return;

        // Only show if user has at least 1 approved thumbnail
        if (uploadCount <= 0) return;

        auto* statsMenuCC = typeinfo_cast<CCMenu*>(statsMenu);
        if (!statsMenuCC) return;

        // Icon sprite â€" use the mod's thumbnail icon or a GD frame
        auto* iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
        if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
        if (!iconSprite) iconSprite = CCSprite::create();
        
        // Create the count label (like SendDB's sends label)
        auto* countLabel = CCLabelBMFont::create(
            fmt::format("{}", uploadCount).c_str(),
            "bigFont.fnt"
        );
        countLabel->setScale(0.6f);
        
        // Create a simple sprite that combines icon and text
        auto* combinedSprite = CCNode::create();
        combinedSprite->setAnchorPoint({0.5f, 0.5f});
        combinedSprite->setContentSize({50.f, 30.f});
        
        if (iconSprite) {
            scaleToFit(iconSprite, 20.f);
            iconSprite->setAnchorPoint({0.5f, 0.5f});
            iconSprite->setPosition({15.f, 15.f});
            combinedSprite->addChild(iconSprite);
        }
        
        countLabel->setAnchorPoint({0.f, 0.5f});
        countLabel->setPosition({iconSprite ? 28.f : 10.f, 15.f});
        combinedSprite->addChild(countLabel);

        // Create clickable button with the combined sprite
        auto* thumbBtn = CCMenuItemSpriteExtra::create(
            combinedSprite,
            this,
            menu_selector(PaimonProfilePage::onThumbnailCountClicked)
        );
        thumbBtn->setID("paimon-thumb-count-btn"_spr);
        
        // Use same layout options as other stats items
        thumbBtn->setLayoutOptions(AxisLayoutOptions::create()
            ->setScaleLimits(0.0f, 1.0f)
        );

        statsMenuCC->addChild(thumbBtn);
        statsMenuCC->updateLayout();

        m_fields->m_thumbCountLabel = countLabel;
        log::debug("[ProfilePage] Added clickable thumbnail count badge: {} uploads", uploadCount);
    }

    std::string getViewedUsername() {
        // acceso directo al campo m_userName del GJUserScore
        if (this->m_score && !this->m_score->m_userName.empty()) {
            return this->m_score->m_userName;
        }
        // fallback: leer de labels si m_score no esta disponible aun
        if (this->m_mainLayer) {
            if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(this->m_mainLayer->getChildByIDRecursive("username-label"))) {
                if (lbl->getString()) return std::string(lbl->getString());
            }
            if (auto* lbl2 = typeinfo_cast<CCLabelBMFont*>(this->m_mainLayer->getChildByIDRecursive("username"))) {
                if (lbl2->getString()) return std::string(lbl2->getString());
            }
        }
        return "";
    }

    void refreshBanButtonVisibility() {
        if (!m_fields->m_banBtn) return;

        // nunca mostrar en tu propio perfil
        if (this->m_ownProfile) {
            m_fields->m_banBtn->setVisible(false);
            m_fields->m_banBtn->setEnabled(false);
            return;
        }

        bool show = canShowModerationControls();
        m_fields->m_banBtn->setVisible(show);
        m_fields->m_banBtn->setEnabled(show);

        // si se ve, tambien lo desactivo si el perfil es mod/admin
        // uso /api/moderators pa mantenerlo consistente
        auto targetName = getViewedUsername();
        if (show && !targetName.empty()) {
            auto targetLower = geode::utils::string::toLower(targetName);
            Ref<ProfilePage> self = this;
            int currentAccount = this->m_accountID;
            HttpClient::get().get("/api/moderators", [self, targetLower, currentAccount](bool ok, std::string const& resp) {
                if (!ok) return;
                // compruebo que sigo vivo y es el mismo perfil
                if (!self || !self->getParent()) return;
                if (self->m_accountID != currentAccount) return;

                auto parsed = matjson::parse(resp);
                if (!parsed.isOk()) return;
                auto root = parsed.unwrap();
                auto mods = root["moderators"]; // [{ username, currentBanner }]
                if (!mods.isArray()) return;
                auto modsArr = mods.asArray();
                if (!modsArr.isOk()) return;
                for (auto const& v : modsArr.unwrap()) {
                    if (!v.isObject()) continue;
                    auto u = v["username"];
                    if (!u.isString()) continue;
                    auto nameLower = geode::utils::string::toLower(u.asString().unwrapOr(""));
                    if (nameLower == targetLower) {
                        // ya estamos en el main thread
                        if (auto banBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(self->getChildByIDRecursive("ban-user-button"))) {
                            banBtn->setEnabled(false);
                            banBtn->setOpacity(120);
                        }
                        return;
                    }
                }
            });
        }
    }

    void onBanUser(CCObject*) {
        if (!canShowModerationControls()) {
            PaimonNotify::create(Localization::get().getString("ban.profile.mod_only"), NotificationIcon::Warning)->show();
            return;
        }
        if (this->m_ownProfile) {
            PaimonNotify::create(Localization::get().getString("ban.profile.self_ban"), NotificationIcon::Warning)->show();
            return;
        }

        // nombre del user en el perfil
        std::string target = getViewedUsername();
        if (target.empty()) {
            PaimonNotify::create(Localization::get().getString("ban.profile.read_error"), NotificationIcon::Error)->show();
            return;
        }
        
        BanUserPopup::create(target)->show();
    }

    static std::shared_ptr<std::vector<uint8_t>> readProfileImgCacheBytes(int accountID) {
        auto path = getProfileImgCachePath(accountID);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return nullptr;

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return nullptr;
        auto size = file.tellg();
        if (size <= 0) return nullptr;
        file.seekg(0, std::ios::beg);

        auto bytes = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(bytes->data()), size)) return nullptr;
        return bytes;
    }

    void displayProfileImgGif(std::string const& gifKey) {
        auto gif = AnimatedGIFSprite::createFromCache(gifKey);
        if (!gif) return;

        auto f = m_fields.self();
        if (f->m_profileImgClip) {
            f->m_profileImgClip->removeFromParent();
            f->m_profileImgClip = nullptr;
        }

        auto layer = this->m_mainLayer;
        if (!layer) return;
        auto layerSize = layer->getContentSize();
        auto popupGeo = paimon::compat::InfoLayerLocator::findPopupGeometry(layer);
        CCSize popupSize = popupGeo.found ? popupGeo.size : CCSize(440.f, 290.f);
        CCPoint popupCenter = popupGeo.found ? popupGeo.center : ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);
        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        auto stencil = paimon::SpriteHelper::createRectStencil(imgArea.width, imgArea.height);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);

        float scaleX = imgArea.width / std::max(1.0f, gif->getContentWidth());
        float scaleY = imgArea.height / std::max(1.0f, gif->getContentHeight());
        gif->setScale(std::max(scaleX, scaleY));
        gif->setAnchorPoint(ccp(0.5f, 0.5f));
        gif->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));
        gif->play();
        clip->addChild(gif);
        if (gif->getTexture()) {
            cacheProfileImgTexture(this->m_accountID, gif->getTexture());
        }

        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 70));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-profileimg-dark-overlay"_spr);
        clip->addChild(dark);

        layer->addChild(clip, paimon::settings::profiles::profileImgZLayer());
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 1.5f);
        styleProfileInternalBgs(layer);
    }

    void displayProfileImgVideo(std::string const& videoKey) {
        auto f = m_fields.self();

        // Clean up previous video if exists
        if (f->m_profileImgClip) {
            // Stop any playing video before removing
            CCClippingNode* clip = f->m_profileImgClip;
            if (clip) {
                for (auto* child : CCArrayExt<CCNode*>(clip->getChildren())) {
                    if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(child)) {
                        videoSprite->stop();
                    }
                }
            }
            f->m_profileImgClip->removeFromParent();
            f->m_profileImgClip = nullptr;
        }

        auto video = VideoThumbnailSprite::createFromCache(videoKey);
        if (!video) return;

        auto layer = this->m_mainLayer;
        if (!layer) return;
        auto layerSize = layer->getContentSize();
        auto popupGeo = paimon::compat::InfoLayerLocator::findPopupGeometry(layer);
        CCSize popupSize = popupGeo.found ? popupGeo.size : CCSize(440.f, 290.f);
        CCPoint popupCenter = popupGeo.found ? popupGeo.center : ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);
        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        auto stencil = paimon::SpriteHelper::createRectStencil(imgArea.width, imgArea.height);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);

        // Helper: applies scale-to-fill against the actual video dimensions.
        // Reuses the cocos2d "cover" formula (max of axis ratios) so the video
        // fills imgArea without leaving stripes, possibly cropping a little.
        auto applyCoverScale = [imgArea](VideoThumbnailSprite* spr) {
            if (!spr) return;
            auto sz = spr->getVideoSize();
            float w = std::max(1.0f, sz.width);
            float h = std::max(1.0f, sz.height);
            float sx = imgArea.width  / w;
            float sy = imgArea.height / h;
            spr->setScale(std::max(sx, sy));
        };

        // Position/anchor first; the scale is applied below either right
        // away (if the sprite already has a real frame) or once the first
        // visible frame arrives via setOnFirstVisibleFrame.
        video->setAnchorPoint(ccp(0.5f, 0.5f));
        video->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));

        // Bug-1 fix: if the sprite was just constructed and the decoder has
        // not produced a frame yet, getContentWidth() is 1×1 (placeholder).
        // Computing setScale against that gives an enormous scale that maps
        // a 1×1 placeholder all over imgArea (the "small video top-left, rest
        // distorted" symptom). Instead, only apply the scale immediately when
        // the sprite already has its real video size; otherwise defer until
        // the first frame arrives.
        if (video->hasVisibleFrame()) {
            applyCoverScale(video);
        } else {
            // Provisional: scale to fill assuming the encoded video size
            // (returned by getVideoSize() reading the player metadata even
            // before the first decoded frame). If even metadata is missing,
            // setScale(1) avoids the 1×1 stretch artefact and waits for the
            // callback below.
            auto sz = video->getVideoSize();
            if (sz.width > 1.f && sz.height > 1.f) {
                applyCoverScale(video);
            } else {
                video->setScale(1.0f);
                video->setVisible(false); // hide until first frame to avoid the 1×1 flash
            }
        }
        clip->addChild(video);

        // The callback fires on the first decoded frame: by then the sprite
        // has real contentSize, getVideoSize() is accurate and we can apply
        // the final cover scale. Capture both the sprite (Ref<>) and accountID
        // so we keep the sprite alive until the callback runs and we don't
        // race against profile switches.
        Ref<VideoThumbnailSprite> videoRef = video;
        int currentAccountID = this->m_accountID;
        video->setOnFirstVisibleFrame(
            [accountID = currentAccountID, applyCoverScale, videoRef]
            (VideoThumbnailSprite* readyVideo) {
                if (!readyVideo) return;
                applyCoverScale(readyVideo);
                readyVideo->setVisible(true);
                if (auto* tex = readyVideo->getTexture()) {
                    cacheProfileImgTexture(accountID, tex);
                }
            }
        );
        video->play();

        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 70));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-profileimg-dark-overlay"_spr);
        clip->addChild(dark);

        layer->addChild(clip, paimon::settings::profiles::profileImgZLayer());
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 1.5f);
        styleProfileInternalBgs(layer);

        // Si la config del perfil pide usar el audio del video como musica,
        // re-disparar checkAndPlayProfileMusic ahora que el .mp4 cacheado
        // ya esta disponible.  La primera invocacion (que ocurre en paralelo
        // a la descarga) puede haber salteado el playback porque el video
        // todavia no estaba en disco.
        {
            int videoAccountID = this->m_accountID;
            auto bgConfig = ProfileThumbs::get().getProfileConfig(videoAccountID);
            if (bgConfig.hasConfig && bgConfig.useVideoAudio &&
                !ProfileMusicManager::get().isPlaying() &&
                !ProfileMusicManager::get().isPaused()) {
                checkAndPlayProfileMusic(videoAccountID, std::nullopt, false, false);
            }
        }
    }

    // Bug-2 helper: returns true when the local cache already has a
    // GIF or video for this profile. We use it to override the cached
    // server-side config when it disagrees with what the user just
    // configured locally (otherwise an old "icon-gradient" config would
    // hide a freshly uploaded video on a re-open).
    bool hasLocalAnimatedProfileMedia(int accountID) {
        auto videoKey       = fmt::format("profileimg_video_{}", accountID);
        auto legacyVideoKey = fmt::format("profile_video_{}", accountID);
        auto gifKey         = getProfileImgGifCacheKey(accountID);
        if (VideoThumbnailSprite::isCached(videoKey))       return true;
        if (VideoThumbnailSprite::isCached(legacyVideoKey)) return true;
        if (!gifKey.empty() && AnimatedGIFSprite::isCached(gifKey)) return true;
        return false;
    }

    bool ensureAnimatedProfileImg(int accountID) {
        auto gifKey = getProfileImgGifCacheKey(accountID);
        auto videoKey = fmt::format("profileimg_video_{}", accountID);

        // check for cached video first (use correct video key)
        if (VideoThumbnailSprite::isCached(videoKey)) {
            displayProfileImgVideo(videoKey);
            return true;
        }

        // also check legacy video key
        auto legacyVideoKey = fmt::format("profile_video_{}", accountID);
        if (VideoThumbnailSprite::isCached(legacyVideoKey)) {
            displayProfileImgVideo(legacyVideoKey);
            return true;
        }

        if (!gifKey.empty() && AnimatedGIFSprite::isCached(gifKey)) {
            displayProfileImgGif(gifKey);
            return true;
        }

        auto bytes = readProfileImgCacheBytes(accountID);
        if (!bytes || bytes->empty()) return false;

        // check if disk cache is MP4 (ftyp box can be at different offsets)
        bool isMp4 = false;
        if (bytes->size() > 12) {
            for (size_t i = 0; i + 3 < bytes->size() && i < 12; ++i) {
                if ((*bytes)[i]=='f' && (*bytes)[i+1]=='t' && (*bytes)[i+2]=='y' && (*bytes)[i+3]=='p') {
                    isMp4 = true;
                    break;
                }
            }
        }
        if (isMp4) {
            std::string videoKey = fmt::format("profileimg_video_{}", accountID);
            auto* videoSprite = VideoThumbnailSprite::createFromData(*bytes, videoKey);
            if (videoSprite) {
                ProfileImageService::get().rememberProfileImgGifKey(accountID, videoKey);
                displayProfileImgVideo(videoKey);
                return true;
            }
        }

        bool isAnimatedImg = paimon::format::isGif(bytes->data(), bytes->size());
        if (!isAnimatedImg) return false;

        ProfileImageService::get().rememberProfileImgGifKey(accountID, gifKey);
        Ref<ProfilePage> safeRef = this;
        AnimatedGIFSprite::createAsync(*bytes, gifKey, [safeRef, accountID, gifKey](AnimatedGIFSprite* sprite) {
            if (!sprite) return;
            Loader::get()->queueInMainThread([safeRef, accountID, gifKey]() {
                if (!safeRef || !safeRef->getParent()) return;
                auto* page = static_cast<PaimonProfilePage*>(safeRef.data());
                if (!page || page->m_accountID != accountID) return;
                page->displayProfileImgGif(gifKey);
            });
        });

        return true;
    }

    void addOrUpdateProfileImgOnPage(int accountID, bool isSelf = false) {
        auto f = m_fields.self();
        f->m_hasProfileBackdrop = false;
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));

        // limpiar anteriores - stop video first to prevent stale playback
        if (f->m_profileImgClip) {
            CCClippingNode* clip = f->m_profileImgClip;
            if (clip) {
                for (auto* child : CCArrayExt<CCNode*>(clip->getChildren())) {
                    if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(child)) {
                        videoSprite->stop();
                    }
                }
            }
            f->m_profileImgClip->removeFromParent();
            f->m_profileImgClip = nullptr;
        }
        if (f->m_profileImgBorder) { f->m_profileImgBorder->removeFromParent(); f->m_profileImgBorder = nullptr; }

        // Si la config cacheada dice que el usuario eligio el degradado de
        // iconos, lo pintamos directamente y nos saltamos la descarga del
        // banner de imagen / GIF / video — no tiene sentido bajarlo si no
        // se va a mostrar.
        //
        // Bug-2 fix: hay un caso en que la config cacheada dice
        // "icon-gradient" pero el usuario YA tiene un video/GIF local del
        // perfil (subido en otro dispositivo o antes de que la config se
        // re-sincronizara desde el servidor). Si tomamos el atajo de
        // gradient sin chequear el cache local, el video que el usuario
        // acaba de configurar se ve UNA vez (cuando se subió) y luego nunca
        // mas, porque la config server-side cacheada lo bloquea. Por eso
        // primero comprobamos si hay video/GIF local cacheado y, si lo hay,
        // lo mostramos: el video local es la fuente de verdad mas reciente.
        {
            auto cachedCfg = ProfileThumbs::get().getProfileConfig(accountID);
            bool hasLocalAnimatedMedia = hasLocalAnimatedProfileMedia(accountID);

            if (cachedCfg.hasConfig && cachedCfg.backgroundType == "icon-gradient" &&
                !hasLocalAnimatedMedia) {
                // Colores LIVE: leemos del jugador actual (GameManager para
                // perfil propio, GJUserScore para otros) en cada render.
                // Si todavia no hay datos del jugador (m_score aun no
                // llego), no pintamos nada y dejamos que getUserInfoFinished
                // dispare el repintado mas tarde con los colores correctos.
                cocos2d::ccColor3B liveA, liveB;
                if (getLiveProfileColors(liveA, liveB)) {
                    this->displayProfileBgGradient(
                        liveA, liveB,
                        cachedCfg.gradientEffect, cachedCfg.gradientSpeed
                    );
                }
                // Sin datos live no renderizamos: el config ya no guarda
                // snapshot, los colores SOLO vienen de local.
                return;
            }
            if (cachedCfg.hasConfig && cachedCfg.backgroundType == "none" &&
                !hasLocalAnimatedMedia) {
                // El usuario reseteo su fondo: no pintamos nada extra.
                return;
            }
            // Caso especial: config dice icon-gradient/none pero hay media
            // local. Caemos al flujo normal abajo, que llamara a
            // ensureAnimatedProfileImg() y mostrara el video/GIF local.
        }

        bool queuedAnimated = ensureAnimatedProfileImg(accountID);

        if (!queuedAnimated) {
            // 1) si hay cache en memoria, mostrar de inmediato
            CCTexture2D* cachedTex = getProfileImgCachedTexture(accountID);
            if (cachedTex) {
                this->displayProfileImg(accountID, cachedTex);
            } else {
                // 2) si hay cache en disco, cargar y mostrar
                if (auto* diskTex = loadProfileImgFromDisk(accountID)) {
                    // Ref<> hace retain en la asignacion y release del anterior automaticamente
                    cacheProfileImgTexture(accountID, diskTex);
                    this->displayProfileImg(accountID, diskTex);
                }
            }
        }

        // Asegura tener config (tipo de fondo) para el caso en que el
        // usuario haya elegido icon-gradient pero todavia no este cacheado.
        // Si despues de bajar la config vemos icon-gradient, repintamos el
        // banner como gradiente y limpiamos cualquier imagen previa.
        WeakRef<PaimonProfilePage> cfgSelf = this;
        ThumbnailAPI::get().downloadProfileConfig(accountID,
            [cfgSelf, accountID](bool ok, ProfileConfig const& cfg) {
                if (!ok || !cfg.hasConfig) return;
                ProfileThumbs::get().cacheProfileConfig(accountID, cfg);
                if (cfg.backgroundType == "icon-gradient") {
                    Loader::get()->queueInMainThread([cfgSelf, accountID, cfg]() {
                        auto page = cfgSelf.lock();
                        if (!page) return;
                        if (page->m_accountID != accountID) return;
                        // Bug-2 fix: do not paint over a locally-configured
                        // video/GIF. The server config can lag behind the
                        // local media (different device, recent upload, etc.).
                        auto* selfPaimon = static_cast<PaimonProfilePage*>(page.data());
                        if (selfPaimon && selfPaimon->hasLocalAnimatedProfileMedia(accountID)) {
                            return;
                        }
                        // Colores LIVE: ver comentario en addOrUpdateProfileImgOnPage.
                        // Si todavia no hay live, no pintamos: getUserInfoFinished
                        // se encargara cuando llegue el GJUserScore.
                        cocos2d::ccColor3B liveA, liveB;
                        if (page->getLiveProfileColors(liveA, liveB)) {
                            page->displayProfileBgGradient(
                                liveA, liveB,
                                cfg.gradientEffect, cfg.gradientSpeed
                            );
                        }
                    });
                } else if (cfg.backgroundType == "none") {
                    Loader::get()->queueInMainThread([cfgSelf, accountID]() {
                        auto page = cfgSelf.lock();
                        if (!page) return;
                        if (page->m_accountID != accountID) return;
                        // Same fix as above: keep local media if present.
                        auto* selfPaimon = static_cast<PaimonProfilePage*>(page.data());
                        if (selfPaimon && selfPaimon->hasLocalAnimatedProfileMedia(accountID)) {
                            return;
                        }
                        page->clearProfileBgVisual();
                    });
                }
            });

        // descargar del servidor en segundo plano (actualizar cache)
        // Ref<> mantiene vivo el ProfilePage hasta que termine el callback
        Ref<ProfilePage> self = this;
        ThumbnailAPI::get().downloadProfileImg(accountID, [self, accountID](bool success, CCTexture2D* texture) {
            if (!self || !self->getParent()) return;

            if (success) {
                // Ref<> hace retain en la asignacion y release del anterior automaticamente
                auto* page = static_cast<PaimonProfilePage*>(self.data());
                if (!page) return;
                if (self->m_accountID != accountID) return;

                // Si entre tanto se cacheo una config con backgroundType
                // distinto de imagen (icon-gradient / none), respetamos
                // la config y no pintamos la imagen.
                auto cfg = ProfileThumbs::get().getProfileConfig(accountID);
                if (cfg.hasConfig &&
                    (cfg.backgroundType == "icon-gradient" || cfg.backgroundType == "none")) {
                    return;
                }

                if (texture) {
                    cacheProfileImgTexture(accountID, texture);
                }
                if (!page->ensureAnimatedProfileImg(accountID) && texture) {
                    page->displayProfileImg(accountID, texture);
                }
            }
        }, isSelf);
    }

    static bool isBrownColor(ccColor3B const& c) {
        return (c.r >= 0x70 && c.g >= 0x20 && c.g <= 0xA0 && c.b <= 0x70 && c.r > c.g && c.g >= c.b);
    }

    static bool isDarkBgColor(ccColor3B const& c) {
        return (c.r <= 0x60 && c.g <= 0x50 && c.b <= 0x40 && (c.r + c.g + c.b) > 0);
    }

    // Tinta un CCScale9Sprite completo (centro + bordes)
    static void tintScale9(CCScale9Sprite* s9, ccColor3B const& color, GLubyte opacity) {
        if (!s9) return;

        // Activa cascade para hijos
        s9->setCascadeColorEnabled(true);
        s9->setCascadeOpacityEnabled(true);
        s9->setColor(color);
        s9->setOpacity(opacity);

        // Tintar hijos directos del batch node interno
        auto s9Children = s9->getChildren();
        if (!s9Children) return;
        for (auto* batchNode : CCArrayExt<CCSpriteBatchNode*>(s9Children)) {
            if (!batchNode) continue;
            auto batchChildren = batchNode->getChildren();
            if (!batchChildren) continue;
            for (auto* spr : CCArrayExt<CCSprite*>(batchChildren)) {
                if (spr) {
                    spr->setColor(color);
                    spr->setOpacity(opacity);
                }
            }
        }
    }

    // Oculta fondos decorativos y agrega panel oscuro
    void styleProfileInternalBgs(CCNode* root) {
        if (!root) return;

        auto walk = [&](auto const& self, CCNode* parent) -> void {
            if (!parent) return;
            auto* children = parent->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                // Oculta icon-background. El otro mod lo reubica lejos de los
                // iconos, pero los ICONOS no se mueven, asi que son la posicion
                // "original" correcta. Centramos el panel sobre la fila real de
                // iconos (SimplePlayers fuera de la lista de comentarios),
                // calculada en espacio de mundo -> espacio de 'parent'. Si no
                // se encuentran iconos, caemos al valor vanilla de siempre.
                if (child->getID() == "icon-background") {
                    child->setVisible(false);

                    CCPoint lo{0.f, 0.f}, hi{0.f, 0.f};
                    bool any = false;
                    auto scan = [&](auto const& self, CCNode* n) -> void {
                        if (!n || typeinfo_cast<GJCommentListLayer*>(n)) return;
                        if (typeinfo_cast<SimplePlayer*>(n) && n->getParent()) {
                            auto w = n->getParent()->convertToWorldSpace(n->getPosition());
                            auto p = parent->convertToNodeSpace(w);
                            if (!any) { lo = hi = p; any = true; }
                            else {
                                lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
                                hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
                            }
                        }
                        if (auto* ch = n->getChildren())
                            for (auto* k : CCArrayExt<CCNode*>(ch)) self(self, k);
                    };
                    scan(scan, root);

                    auto* panel = parent->getChildByID("paimon-icon-dark-panel"_spr);
                    if (!panel) {
                        panel = paimon::SpriteHelper::createDarkPanel(340.f, 45.f, 100, 8.f);
                        if (panel) {
                            panel->setAnchorPoint(ccp(0.5f, 0.5f));
                            panel->setZOrder(child->getZOrder());
                            panel->setID("paimon-icon-dark-panel"_spr);
                            parent->addChild(panel);
                        }
                    }
                    if (panel) {
                        if (any) {
                            panel->setPosition(ccp((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f));
                            panel->setContentSize(CCSize((hi.x - lo.x) + 60.f, 45.f));
                        } else {
                            panel->setPosition(ccp(283.f, 200.f));
                            panel->setContentSize(CCSize(340.f, 45.f));
                        }
                    }
                }

                // Oculta borde decorativo
                if (child->getID() == "alphalaneous.happy_textures/special-border") {
                    child->setVisible(false);
                }

                // GJCommentListLayer: opacidad 0, oculta bordes
                if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(child)) {
                    commentList->setOpacity(0);

                    auto* listChildren = commentList->getChildren();
                    if (listChildren) {
                        for (auto* lc : CCArrayExt<CCNode*>(listChildren)) {
                            if (!lc) continue;
                            auto id = lc->getID();
                            // Bordes con node ID
                            if (id == "left-border" || id == "right-border" ||
                                id == "top-border" || id == "bottom-border") {
                                lc->setVisible(false);
                            }
                        }
                    }

                    // Oculta fondos internos de CommentCells
                    hideCommentCellBgs(commentList);
                }

                self(self, child);
            }
        };

        walk(walk, root);
    }

    // Oculta fondos internos de CommentCells
    void hideCommentCellBgs(CCNode* listNode) {
        if (!listNode) return;

        auto findCells = [&](auto const& self, CCNode* node) -> void {
            if (!node) return;
            auto* children = node->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                // Oculta fondos de CommentCell
                if (typeinfo_cast<CommentCell*>(child)) {
                    // Solo oculta vanilla si hay panel paimon
                    if (!child->getChildByIDRecursive("paimon-comment-bg-panel"_spr)) {
                        self(self, child);
                        continue;
                    }

                    // Optimizacion FPS: si la celda ya fue procesada (todos los
                    // bgs vanilla escondidos) y no ha sido reciclada
                    // (loadFromComment limpia este flag), saltamos la
                    // recursion completa por hijos. Esto reduce de O(N*M) a
                    // O(N) las pasadas repetidas del tickStyleBgs.
                    if (child->getUserObject("paimon-comment-bgs-hidden"_spr)) {
                        continue;
                    }

                    // Busca fondos vanilla recursivamente
                    auto hideBgsRecursive = [](auto const& recurse, CCNode* node) -> void {
                        if (!node) return;
                        auto* kids = node->getChildren();
                        if (!kids) return;
                        for (auto* k : CCArrayExt<CCNode*>(kids)) {
                            if (!k) continue;

                            if (!shouldHideVanillaCommentBgNode(k)) {
                                if (!typeinfo_cast<CCMenu*>(k)) {
                                    recurse(recurse, k);
                                }
                                continue;
                            }

                            k->setVisible(false);
                        }
                    };

                    hideBgsRecursive(hideBgsRecursive, child);
                    // Marcar como procesada hasta el proximo loadFromComment.
                    child->setUserObject("paimon-comment-bgs-hidden"_spr, cocos2d::CCBool::create(true));
                }

                self(self, child);
            }
        };

        findCells(findCells, listNode);
    }

    // Hook: GD termina de cargar info del usuario
    $override
    void getUserInfoFinished(GJUserScore* score) {
        ProfilePage::getUserInfoFinished(score);
        if (m_fields->m_hasProfileBackdrop) {
            if (auto* layer = this->m_mainLayer) {
                styleProfileInternalBgs(layer);
            }
        }

        // Ahora que tenemos el GJUserScore, si el perfil eligio
        // icon-gradient repintamos con los colores LIVE del jugador.
        // En la primera pasada (durante init) m_score todavia podia ser null
        // y se uso el snapshot del config como fallback; este repintado
        // refresca al gradient con los colores actuales reales.
        //
        // Bug-2 fix: igual que en addOrUpdateProfileImgOnPage, si el usuario
        // tiene media local (video/GIF) cacheada para este perfil, no
        // pisamos el banner con el gradient — la media local manda.
        if (this->m_accountID > 0) {
            auto cachedCfg = ProfileThumbs::get().getProfileConfig(this->m_accountID);
            if (cachedCfg.hasConfig && cachedCfg.backgroundType == "icon-gradient" &&
                !this->hasLocalAnimatedProfileMedia(this->m_accountID)) {
                cocos2d::ccColor3B liveA, liveB;
                if (getLiveProfileColors(liveA, liveB)) {
                    this->displayProfileBgGradient(
                        liveA, liveB,
                        cachedCfg.gradientEffect, cachedCfg.gradientSpeed
                    );
                }
            }
        }
    }

    void onProfileReviews(CCObject*) {
        if (auto popup = ProfileReviewsPopup::create(this->m_accountID)) {
            popup->show();
        }
    }

    void onFavCreator(CCObject* sender) {
        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!item) return;
        int creatorID = item->getTag();
        if (creatorID <= 0) return;

        auto& tracker = paimon::foryou::ForYouTracker::get();
        if (tracker.isCreatorFavorited(creatorID)) {
            tracker.onUnfavoriteCreator(creatorID);
            if (auto spr = typeinfo_cast<CCSprite*>(item->getNormalImage())) spr->setOpacity(120);
            PaimonNotify::create(
                Localization::get().getString("foryou.fav_creator_removed").c_str(),
                NotificationIcon::Info
            )->show();
        } else {
            tracker.onFavoriteCreator(creatorID);
            if (auto spr = typeinfo_cast<CCSprite*>(item->getNormalImage())) spr->setOpacity(255);
            PaimonNotify::create(
                Localization::get().getString("foryou.fav_creator_added").c_str(),
                NotificationIcon::Success
            )->show();
        }
        tracker.save();
    }

    void onRateProfile(CCObject*) {
        // no calificar tu propio perfil
        if (this->m_ownProfile) {
            PaimonNotify::create(Localization::get().getString("profile.cant_rate_own").c_str(), NotificationIcon::Warning)->show();
            return;
        }

        std::string targetName = getViewedUsername();
        if (targetName.empty()) targetName = "Unknown";

        if (auto popup = RateProfilePopup::create(this->m_accountID, targetName)) {
            popup->show();
        }
    }

    // Limpia botones paimon para evitar duplicados
    void cleanPaimonButtons(CCMenu* menu) {
        if (!menu) return;
        static std::string const paimonBtnIDs[] = {
            "profile-reviews-btn"_spr,
            "ban-user-button"_spr,
            "thumbs-gear-button"_spr,
            "add-moderator-button"_spr,
            "fav-creator-btn"_spr,
        };
        for (auto const& id : paimonBtnIDs) {
            while (auto* btn = menu->getChildByID(id)) {
                btn->removeFromParent();
            }
        }
        m_fields->m_gearBtn = nullptr;
        m_fields->m_banBtn = nullptr;
        m_fields->m_addModBtn = nullptr;
    }

    void cleanPaimonSocialsButtons(CCMenu* menu) {
        if (!menu) return;
        static std::string const paimonSocialIDs[] = {
            "profile-settings-button"_spr,
            "profile-music-button"_spr,
            "add-profileimg-button"_spr,
            "profile-music-pause-button"_spr,
        };
        for (auto const& id : paimonSocialIDs) {
            while (auto* btn = menu->getChildByID(id)) {
                btn->removeFromParent();
            }
        }
        m_fields->m_musicPauseBtn = nullptr;
    }

    // Obtiene posicion/tamano del popup de perfil
    CCPoint getPopupCenter() {
        if (!this->m_mainLayer) {
            auto* dir = CCDirector::get();
            if (!dir) return {240.f, 160.f};
            return dir->getWinSize() / 2;
        }
        auto geo = paimon::compat::InfoLayerLocator::findPopupGeometry(this->m_mainLayer);
        if (geo.found) return geo.center;
        return this->m_mainLayer->getContentSize() / 2;
    }

    CCSize getPopupSize() {
        if (!this->m_mainLayer) return {440.f, 290.f};
        auto geo = paimon::compat::InfoLayerLocator::findPopupGeometry(this->m_mainLayer);
        if (geo.found) return geo.size;
        return {440.f, 290.f};
    }

    // Hook: GD construye paneles de iconos del perfil
    $override
    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);

        // Prefetch emote catalog for profile comment emote rendering
        if (!paimon::emotes::EmoteService::get().isLoaded() &&
            !paimon::emotes::EmoteService::get().isFetching()) {
            paimon::emotes::EmoteService::get().loadCatalogFromDisk();
            if (!paimon::emotes::EmoteService::get().isLoaded()) {
                paimon::emotes::EmoteService::get().fetchAllEmotes();
            }
        }

        if (m_fields->m_hasProfileBackdrop) {
            if (auto* layer = this->m_mainLayer) {
                styleProfileInternalBgs(layer);
            }
        }

        if (!this->m_mainLayer) return;

        // Referencia al popup
        auto popCenter = getPopupCenter();
        auto popSize = getPopupSize();

        // Obtener o crear left-menu
        auto leftMenuNode = this->m_mainLayer->getChildByID("left-menu");
        CCMenu* menu = leftMenuNode ? typeinfo_cast<CCMenu*>(leftMenuNode) : nullptr;

        if (!menu) {
            menu = CCMenu::create();
            menu->setID("left-menu");
            menu->setZOrder(10);
            this->m_mainLayer->addChild(menu);

            // Solo posicionar si creamos nosotros el menu (fallback)
            float menuX = popCenter.x - popSize.width / 2 + 18.f;
            float menuY = popCenter.y;
            menu->setPosition({menuX, menuY});
            menu->setContentSize({40.f, popSize.height * 0.75f});
            menu->setAnchorPoint({0.5f, 0.5f});
            menu->ignoreAnchorPointForPosition(false);

            menu->setLayout(
                ColumnLayout::create()
                    ->setGap(8.f)
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setAxisReverse(false)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
            );
        }

        // Limpia botones paimon anteriores
        cleanPaimonButtons(menu);

        // Limpia badge de thumbnail count en reload
        if (auto* statsMenu = this->m_mainLayer->getChildByIDRecursive("stats-menu")) {
            while (auto* icon = statsMenu->getChildByID("paimon-thumb-count-icon"_spr))
                icon->removeFromParent();
            while (auto* text = statsMenu->getChildByID("paimon-thumb-count-text"_spr))
                text->removeFromParent();
        }
        m_fields->m_thumbCountLabel = nullptr;

        // Boton de reviews
        {
            auto reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_chatBtn_001.png");
            if (!reviewIcon) reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
            if (reviewIcon) {
                scaleToFit(reviewIcon, 26.f);
                auto reviewBtn = CCMenuItemSpriteExtra::create(reviewIcon, this, menu_selector(PaimonProfilePage::onProfileReviews));
                reviewBtn->setID("profile-reviews-btn"_spr);
                menu->addChild(reviewBtn);
            }
        }

        if (!this->m_ownProfile) {
            if (auto bottomMenu = this->m_mainLayer->getChildByIDRecursive("bottom-menu")) {
                if (!bottomMenu->getChildByID("rate-profile-btn"_spr)) {
                    auto bg = paimon::SpriteHelper::safeCreateScale9("GJ_button_04.png");
                    if (!bg) bg = paimon::SpriteHelper::safeCreateScale9("GJ_button_01.png");
                    if (bg) {
                    bg->setContentSize({30.f, 30.f});

                    auto starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
                    if (!starIcon) starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
                    if (starIcon) {
                        scaleToFit(starIcon, 18.f);
                        starIcon->setPosition({15.f, 15.f});
                        bg->addChild(starIcon);
                    }

                    auto starBtn = CCMenuItemSpriteExtra::create(bg, this, menu_selector(PaimonProfilePage::onRateProfile));
                    starBtn->setID("rate-profile-btn"_spr);

                    auto* btmMenu = typeinfo_cast<CCMenu*>(bottomMenu);
                    if (btmMenu) {
                        btmMenu->addChild(starBtn);
                        btmMenu->updateLayout();
                    }
                    }
                }
            }
        }

        // Boton de ban (mods/admins)
        {
            auto banSpr = ButtonSprite::create("X", 40, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
            banSpr->setScale(0.5f);
            auto banBtn = CCMenuItemSpriteExtra::create(banSpr, this, menu_selector(PaimonProfilePage::onBanUser));
            banBtn->setID("ban-user-button"_spr);
            banBtn->setVisible(false);
            menu->addChild(banBtn);
            m_fields->m_banBtn = banBtn;
        }
        refreshBanButtonVisibility();

        // Botones de moderacion (perfil propio)
        if (this->m_ownProfile) {
            // Si ya esta verificado como mod o admin Ã¢â€ â€™ mostrar gear (centro de verificacion)
            if (m_fields->m_isApprovedMod || m_fields->m_isAdmin) {
                ensureGearButton(menu);
            }
            // Si es admin Ã¢â€ â€™ mostrar boton de anadir moderador
            if (m_fields->m_isAdmin) {
                ensureAddModeratorButton(menu);
            }
        }

        // Recalcula layout del left-menu
        menu->updateLayout();

        // Botones en socials-menu
        auto* socialsMenu = getSocialsMenu();
        bool createdSocialsMenu = false;
        if (!socialsMenu) {
            auto newSocialsMenu = CCMenu::create();
            newSocialsMenu->setID("socials-menu");
            newSocialsMenu->setZOrder(10);
            this->m_mainLayer->addChild(newSocialsMenu);
            socialsMenu = newSocialsMenu;
            createdSocialsMenu = true;

            // Solo posicionar si creamos nosotros el menu (fallback)
            float socialsX = popCenter.x + popSize.width / 2 - 18.f;
            float socialsY = popCenter.y;
            socialsMenu->setPosition({socialsX, socialsY});
            socialsMenu->setContentSize({40.f, popSize.height * 0.7f});
            socialsMenu->setAnchorPoint({0.5f, 0.5f});
            socialsMenu->ignoreAnchorPointForPosition(false);

            socialsMenu->setLayout(
                ColumnLayout::create()
                    ->setGap(8.f)
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setAxisReverse(false)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
            );
        }

        cleanPaimonSocialsButtons(socialsMenu);

        // Anade botones despues de los nativos

        if (this->m_ownProfile) {
            auto settingsSpr = paimon::SpriteHelper::safeCreateWithFrameName("accountBtn_settings_001.png");
            if (!settingsSpr) settingsSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
            if (!settingsSpr) settingsSpr = CCSprite::create();
            scaleToFit(settingsSpr, 22.f);
            auto settingsBtn = CCMenuItemSpriteExtra::create(settingsSpr, this, menu_selector(PaimonProfilePage::onOpenProfileSettings));
            settingsBtn->setID("profile-settings-button"_spr);
            socialsMenu->addChild(settingsBtn);
        }

        {
            auto pauseSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_fxOnBtn_001.png");
            if (!pauseSpr) pauseSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_pauseBtn_001.png");
            if (!pauseSpr) pauseSpr = CCSprite::create();
            scaleToFit(pauseSpr, 20.f);
            auto pauseBtn = CCMenuItemSpriteExtra::create(pauseSpr, this, menu_selector(PaimonProfilePage::onToggleProfileMusic));
            pauseBtn->setID("profile-music-pause-button"_spr);
            pauseBtn->setVisible(false);
            socialsMenu->addChild(pauseBtn);
            m_fields->m_musicPauseBtn = pauseBtn;
        }

        socialsMenu->updateLayout();

        // â”€â”€ Limpiar badge custom del username-menu en reloads â”€â”€
        if (auto* usernameMenu = getUsernameMenu()) {
            while (auto* old = usernameMenu->getChildByID("paimon-custom-badge"_spr))
                old->removeFromParent();
            while (auto* old = usernameMenu->getChildByID("paimon-user-status-dot"_spr))
                old->removeFromParent();
        }

        // User status indicator (online/offline dot)
        if (this->m_accountID > 0) {
            fetchAndShowUserStatus(this->m_accountID);
        }

        // Badge de moderador/admin
        if (score) {
            std::string badgeUsername = score->m_userName;

            bool isMod = false;
            bool isAdmin = false;
            if (moderatorCacheGet(badgeUsername, isMod, isAdmin)) {
                if (isMod || isAdmin) {
                    this->addModeratorBadge(isMod, isAdmin);
                }
            }

            // â”€â”€ Profile Bundle: mod status + badge + stats in 1 request â”€â”€
            // Replaces individual checkUserStatus + fetchBadge + getProfileStats calls
            int viewedAccountID = this->m_accountID;
            Ref<PaimonProfilePage> bundleSelf = this;

            HttpClient::get().downloadProfileBundle(viewedAccountID, badgeUsername,
                [bundleSelf, viewedAccountID, badgeUsername](bool success, std::string const& response) {
                    auto queueMusicFallback = [bundleSelf, viewedAccountID]() {
                        Loader::get()->queueInMainThread([bundleSelf, viewedAccountID]() {
                            if (!bundleSelf || !bundleSelf->getParent()) return;
                            if (bundleSelf->m_accountID != viewedAccountID) return;
                            bundleSelf->checkAndPlayProfileMusic(viewedAccountID, std::nullopt, false, true);
                        });
                    };

                    if (!success || response.empty()) {
                        queueMusicFallback();
                        return;
                    }
                    auto parsed = matjson::parse(response);
                    if (!parsed.isOk()) {
                        queueMusicFallback();
                        return;
                    }
                    auto json = parsed.unwrap();

                    // Mod status
                    bool isMod = json["isModerator"].asBool().unwrapOr(false);
                    std::string role = json["role"].asString().unwrapOr("");
                    bool isAdmin = (role == "admin");

                    // Actualiza caches
                    moderatorCacheInsert(badgeUsername, isMod, isAdmin);
                    ModerationService::get().updateUserStatusCache(badgeUsername, isMod, isAdmin);

                    // Custom badge
                    std::string emoteName;
                    if (json.contains("badge") && json["badge"].isObject() && json["badge"].contains("emote")) {
                        emoteName = json["badge"]["emote"].asString().unwrapOr("");
                    }
                    CustomBadgeService::get().updateCacheFromBundle(viewedAccountID, emoteName);

                    // Stats
                    int uploadCount = 0;
                    if (json.contains("stats") && json["stats"].isObject()) {
                        uploadCount = json["stats"]["uploadCount"].asInt().unwrapOr(0);
                    }

                    // ── Profile config del bundle ─────────────────────
                    // El bundle ya devuelve el blob de config completo bajo
                    // "config".  Lo cacheamos en ProfileThumbs para que
                    // applyVideoAudioOverride / displayProfileBg encuentren
                    // useVideoAudio sin un round-trip extra al servidor.
                    if (json.contains("config") && json["config"].isObject()) {
                        auto& cfgJson = json["config"];
                        ProfileConfig pcfg;
                        pcfg.hasConfig = true;
                        if (cfgJson.contains("backgroundType"))
                            pcfg.backgroundType = cfgJson["backgroundType"].asString().unwrapOr("gradient");
                        if (cfgJson.contains("blurIntensity"))
                            pcfg.blurIntensity = static_cast<float>(cfgJson["blurIntensity"].asDouble().unwrapOr(3.0));
                        if (cfgJson.contains("darkness"))
                            pcfg.darkness = static_cast<float>(cfgJson["darkness"].asDouble().unwrapOr(0.2));
                        if (cfgJson.contains("useGradient"))
                            pcfg.useGradient = cfgJson["useGradient"].asBool().unwrapOr(false);
                        if (cfgJson.contains("widthFactor"))
                            pcfg.widthFactor = static_cast<float>(cfgJson["widthFactor"].asDouble().unwrapOr(0.60));
                        if (cfgJson.contains("gradientEffect"))
                            pcfg.gradientEffect = cfgJson["gradientEffect"].asString().unwrapOr("none");
                        if (cfgJson.contains("gradientSpeed"))
                            pcfg.gradientSpeed = static_cast<float>(cfgJson["gradientSpeed"].asDouble().unwrapOr(1.0));
                        if (cfgJson.contains("useVideoAudio"))
                            pcfg.useVideoAudio = cfgJson["useVideoAudio"].asBool().unwrapOr(false);
                        // Cachear en main thread para evitar carrera con el
                        // render que podria estar leyendo simultaneamente.
                        Loader::get()->queueInMainThread([viewedAccountID, pcfg]() {
                            ProfileThumbs::get().cacheProfileConfig(viewedAccountID, pcfg);
                        });
                    }

                    // Music config del bundle
                    std::optional<ProfileMusicManager::ProfileMusicConfig> bundleMusicConfig;
                    bool hasBundleMusicConfig = json.contains("music");
                    if (hasBundleMusicConfig && json["music"].isObject()) {
                        auto& musicJson = json["music"];
                        ProfileMusicManager::ProfileMusicConfig musicCfg;
                        musicCfg.songID = musicJson["songID"].asInt().unwrapOr(0);
                        musicCfg.startMs = musicJson["startMs"].asInt().unwrapOr(0);
                        musicCfg.endMs = musicJson["endMs"].asInt().unwrapOr(20000);
                        musicCfg.volume = static_cast<float>(musicJson["volume"].asDouble().unwrapOr(0.7));
                        musicCfg.enabled = musicJson["enabled"].asBool().unwrapOr(true);
                        musicCfg.songName = musicJson["songName"].asString().unwrapOr("");
                        musicCfg.artistName = musicJson["artistName"].asString().unwrapOr("");
                        musicCfg.updatedAt = musicJson["updatedAt"].asString().unwrapOr("");
                        musicCfg.isCustom = musicJson["isCustom"].asBool().unwrapOr(false);
                        ProfileMusicManager::get().injectBundleConfig(viewedAccountID, musicCfg);
                        bundleMusicConfig = musicCfg;
                    }

                    Loader::get()->queueInMainThread([bundleSelf, viewedAccountID, isMod, isAdmin, emoteName, uploadCount, bundleMusicConfig, hasBundleMusicConfig]() {
                        if (!bundleSelf || !bundleSelf->getParent()) return;
                        if (bundleSelf->m_accountID != viewedAccountID) return;

                        // Mod badge
                        if (isMod || isAdmin) {
                            bundleSelf->addModeratorBadge(isMod, isAdmin);
                        }

                        // Custom emote badge
                        if (!emoteName.empty()) {
                            bundleSelf->addCustomBadgeToProfile(emoteName);
                        }

                        // Upload count badge
                        bundleSelf->addThumbnailCountBadge(uploadCount);

                        bundleSelf->checkAndPlayProfileMusic(viewedAccountID, bundleMusicConfig, hasBundleMusicConfig, !hasBundleMusicConfig);
                    });
                });
        }
    }

    void displayProfileImg(int accountID, CCTexture2D* tex) {
        if (!tex) return;

        auto texSize = tex->getContentSize();
        if (texSize.width <= 0.f || texSize.height <= 0.f) return;

        auto f = m_fields.self();
        if (f->m_profileImgClip) { f->m_profileImgClip->removeFromParent(); f->m_profileImgClip = nullptr; }

        auto layer = this->m_mainLayer;
        if (!layer) return;
        auto layerSize = layer->getContentSize();

        // Busca popup bg por node-id
        CCSize popupSize = CCSize(440.f, 290.f);
        CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        auto popupGeo = paimon::compat::InfoLayerLocator::findPopupGeometry(layer);
        if (popupGeo.found) {
            popupSize = popupGeo.size;
            popupCenter = popupGeo.center;
        }

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        // Stencil con esquinas redondeadas
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(imgArea.width, imgArea.height);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);

        // Imagen como fondo del popup
        auto imgSprite = CCSprite::createWithTexture(tex);
        if (!imgSprite) return;

        float scaleX = imgArea.width / imgSprite->getContentWidth();
        float scaleY = imgArea.height / imgSprite->getContentHeight();
        imgSprite->setScale(std::max(scaleX, scaleY));
        imgSprite->setAnchorPoint(ccp(0.5f, 0.5f));
        imgSprite->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));
        clip->addChild(imgSprite);

        // Overlay oscuro suave
        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 70));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-profileimg-dark-overlay"_spr);
        clip->addChild(dark);

        layer->addChild(clip, paimon::settings::profiles::profileImgZLayer());
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;

        // Aplica estilos a nodos existentes
        styleProfileInternalBgs(layer);
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 1.5f);
    }

    // Reaplica opacidad 0 a icon-background periodicamente
    void tickStyleBgs(float) {
        if (!this->getParent()) return;
        if (!m_fields->m_hasProfileBackdrop) return;
        if (auto* layer = this->m_mainLayer) {
            styleProfileInternalBgs(layer);
        }
    }

    $override
    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;

            // Inicia como no moderador
            m_fields->m_isApprovedMod = false;
            m_fields->m_isAdmin = false;
            PaimonDebug::log("[ProfilePage] Inicializando perfil - status moderador: false");

            // Estado mod guardado
            bool wasVerified = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            bool wasAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
            if (wasVerified) {
                m_fields->m_isApprovedMod = true;
                m_fields->m_isAdmin = wasAdmin;
            }

            // Verifica con el servidor si es perfil propio
            if (ownProfile) {
                auto gm = GameManager::get();
                if (gm && !gm->m_playerName.empty()) {
                    std::string username = gm->m_playerName;
                    Ref<ProfilePage> self = this;
                    ThumbnailAPI::get().checkModerator(username, [self](bool isApproved, bool isAdmin) {
                        Loader::get()->queueInMainThread([self, isApproved, isAdmin]() {
                            if (!self->getParent()) return;
                            auto* page = static_cast<PaimonProfilePage*>(self.data());
                            if (!page) return;
                            bool effectiveMod = isApproved || isAdmin;
                            page->m_fields->m_isApprovedMod = effectiveMod;
                            page->m_fields->m_isAdmin = isAdmin;

            // Guarda estado persistente
                            Mod::get()->setSavedValue("is-verified-moderator", effectiveMod);
                            Mod::get()->setSavedValue("is-verified-admin", isAdmin);

                            if (effectiveMod) {
                // Guarda archivo de verificacion
                                auto modDataPath = Mod::get()->getSaveDir() / "moderator_verification.dat";
                                std::ofstream modFile(modDataPath, std::ios::binary);
                                if (modFile) {
                                    auto now = std::chrono::system_clock::now();
                                    auto timestamp = std::chrono::system_clock::to_time_t(now);
                                    modFile.write(reinterpret_cast<char const*>(&timestamp), sizeof(timestamp));
                                    modFile.close();
                                }
                            }

                            page->refreshBanButtonVisibility();

            // Anade/quita botones segun rango
                            if (auto* leftMenu = page->getLeftMenu()) {
                                if (effectiveMod) {
                                    page->ensureGearButton(leftMenu);
                                }
                                if (isAdmin) {
                                    page->ensureAddModeratorButton(leftMenu);
                                }
                                leftMenu->updateLayout();
                            }
                        });
                    });
                }
            }

            // Marca perfil abierto para restaurar BG al cerrar
            m_fields->m_menuMusicPaused = true;

            // Verifica y reproduce musica del perfil
            checkAndPlayProfileMusic(accountID, std::nullopt, false, false);

            // Carga imagen de perfil
            addOrUpdateProfileImgOnPage(accountID, ownProfile);

            // Schedule verificacion de integridad cada 0.5s
            this->schedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity), 0.5f);

            // Registrar vista de perfil (solo perfiles ajenos)
            if (!ownProfile && accountID > 0) {
                paimon::forum::ForumApi::get().recordProfileView(accountID, [](paimon::forum::Result<bool>) {
                    // silent
                });
            }

        return true;
    }

    void onOpenAddModerator(CCObject*) {
        if (auto* popup = AddModeratorPopup::create(nullptr)) popup->show();
    }

    void onOpenThumbsCenter(CCObject*) {
        // Verifica que sea mod o admin
        if (!m_fields->m_isApprovedMod && !m_fields->m_isAdmin) {
            log::warn("[ProfilePage] Usuario NO es moderador ni admin, bloqueando acceso al centro de verificacion");
            FLAlertLayer::create(
                Localization::get().getString("profile.access_denied").c_str(),
                Localization::get().getString("profile.moderators_only").c_str(),
                Localization::get().getString("general.ok").c_str()
            )->show();
            return;
        }
        
        // Abre centro de verificacion
        log::info("[ProfilePage] Abriendo centro de verificacion para moderador");
        auto scene = VerificationCenterLayer::scene();
        if (scene) {
            TransitionManager::get().pushScene(scene);
        }
    }

    void onAddProfileImg(CCObject*) {
        // Solo permitir editar el fondo del propio perfil.
        if (!this->m_ownProfile) {
            PaimonNotify::create(
                Localization::get().getString("profile.cant_edit_other").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }

        // En vez de abrir directamente el file picker, mostramos un chooser
        // para que el usuario pueda elegir entre subir media, usar el degradado
        // basado en sus iconos, o resetear el fondo.
        auto* picker = ProfileBgPickerPopup::create(this->m_accountID);
        if (!picker) {
            // Fallback: si por algun motivo no se pudo crear el popup, vamos
            // directo al flujo clasico de subir media.
            runProfileBgMediaPicker();
            return;
        }

        WeakRef<PaimonProfilePage> self = this;
        picker->setOnPickMedia([self]() {
            if (auto page = self.lock()) page->runProfileBgMediaPicker();
        });
        picker->setOnPickGradient([self]() {
            if (auto page = self.lock()) page->openProfileBgGradientChooser();
        });
        picker->setOnPickVideoAudio([self]() {
            if (auto page = self.lock()) page->applyProfileBgVideoAudio();
        });
        picker->setOnPickReset([self]() {
            if (auto page = self.lock()) page->confirmProfileBgReset();
        });
        picker->show();
    }

    // Abre el sub-popup de eleccion de efecto + velocidad para el degradado
    // basado en iconos del jugador.  El efecto/velocidad inicial se toma de
    // la config cacheada si existe, asi el usuario sigue editando lo que ya
    // tenia.
    void openProfileBgGradientChooser() {
        int accountID = this->m_accountID;
        WeakRef<PaimonProfilePage> self = this;

        auto cached = ProfileThumbs::get().getProfileConfig(accountID);
        std::string initialEffect = paimon::profilebg::normalizeEffect(cached.gradientEffect);
        float initialSpeed = paimon::profilebg::normalizeSpeed(cached.gradientSpeed);

        auto* popup = ProfileBgGradientPopup::create(
            accountID, initialEffect, initialSpeed,
            [self](std::string const& effect, float speed) {
                if (auto page = self.lock()) {
                    page->applyProfileBgIconGradient(effect, speed);
                }
            }
        );
        if (popup) popup->show();
    }

    void runProfileBgMediaPicker() {
        WeakRef<PaimonProfilePage> self = this;
        pt::pickMedia([self](geode::Result<std::optional<std::filesystem::path>> result) {
            auto page = self.lock();
            if (!page) return;
            auto pathOpt = std::move(result).unwrapOr(std::nullopt);
            if (!pathOpt || pathOpt->empty()) {
                PaimonNotify::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
                return;
            }
            page->processProfileImg(std::move(*pathOpt));
        });
    }

    // Sube ProfileConfig con backgroundType = "icon-gradient" + efecto.
    // Importante: NO se suben los colores actuales del icono.  Los colores
    // se leen siempre LIVE en cada render (GameManager para perfil propio,
    // GJUserScore para otros usuarios) para que cambios de cubo se reflejen
    // sin necesidad de re-subir nada.
    void applyProfileBgIconGradient(std::string const& effect = "none", float speed = 1.0f) {
        if (!this->m_ownProfile) {
            PaimonNotify::create(
                Localization::get().getString("profile.cant_edit_other").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }

        std::string normalizedEffect = paimon::profilebg::normalizeEffect(effect);
        float normalizedSpeed = paimon::profilebg::normalizeSpeed(speed);

        int accountID = this->m_accountID;

        // Mostramos el spinner antes de bajar/subir la config para que el
        // usuario sepa que esta pasando algo.
        auto loading = PaimonLoadingOverlay::create(
            Localization::get().getString("profilebg.gradient.uploading").c_str(),
            30.f
        );
        Ref<PaimonLoadingOverlay> loadingRef = loading;
        if (loading) loading->show(this, 100);

        // Recuperamos config previa (si la hay) para no perder otros campos
        // como blur/darkness/widthFactor/comment-bg, etc.
        // Notar que NO tocamos cfg.colorA / cfg.colorB: dejamos lo que
        // hubiera (o sus defaults).  Los colores reales se calculan en
        // cada render desde local.
        WeakRef<PaimonProfilePage> self = this;
        ThumbnailAPI::get().downloadProfileConfig(accountID,
            [self, accountID, normalizedEffect, normalizedSpeed, loadingRef]
            (bool, ProfileConfig const& existing) {
                ProfileConfig cfg = existing;
                cfg.hasConfig       = true;
                cfg.backgroundType  = "icon-gradient";
                cfg.useGradient     = true;
                cfg.gradientEffect  = normalizedEffect;
                cfg.gradientSpeed   = normalizedSpeed;
                // colorA / colorB intencionalmente sin tocar: el render
                // siempre usa colores live, no los de la config.

                ThumbnailAPI::get().uploadProfileConfig(accountID, cfg,
                    [self, accountID, cfg, loadingRef](bool success, std::string const& msg) {
                        if (loadingRef) loadingRef->dismiss();
                        if (!success) {
                            PaimonNotify::create(
                                fmt::format("{}: {}",
                                    Localization::get().getString("profilebg.gradient.failed"),
                                    msg
                                ).c_str(),
                                NotificationIcon::Error
                            )->show();
                            return;
                        }

                        // Recachea config actualizada y borra cache de imagen
                        // para que el render use el degradado.
                        ProfileThumbs::get().cacheProfileConfig(accountID, cfg);

                        PaimonNotify::create(
                            Localization::get().getString("profilebg.gradient.applied").c_str(),
                            NotificationIcon::Success
                        )->show();

                        if (auto page = self.lock()) {
                            // Colores LIVE: el render siempre lee del
                            // jugador en vivo, no de la config (que ya no
                            // guarda snapshot de colores).  Para perfil
                            // propio GameManager esta disponible siempre,
                            // asi que aqui practicamente nunca falla.
                            cocos2d::ccColor3B liveA, liveB;
                            if (page->getLiveProfileColors(liveA, liveB)) {
                                page->displayProfileBgGradient(
                                    liveA, liveB,
                                    cfg.gradientEffect, cfg.gradientSpeed
                                );
                            }
                        }
                    });
            });
    }

    // Activa el modo "Audio Video" para el perfil propio: hace que el audio
    // del video del fondo se use como musica de perfil para los visitantes,
    // y borra cualquier musica configurada del servidor.  Solo tiene sentido
    // si el fondo actual es un video — si no lo es, le avisamos al usuario y
    // no hacemos nada para evitar dejar la config en un estado raro.
    void applyProfileBgVideoAudio() {
        if (!this->m_ownProfile) {
            PaimonNotify::create(
                Localization::get().getString("profile.cant_edit_other").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }

        int accountID = this->m_accountID;
        WeakRef<PaimonProfilePage> self = this;

        // Validar que tengamos un video de fondo cacheado: si el perfil no
        // tiene video, "Audio Video" no aporta nada.
        bool hasVideo = false;
        {
            auto videoKey       = fmt::format("profileimg_video_{}", accountID);
            auto legacyVideoKey = fmt::format("profile_video_{}", accountID);
            if (VideoThumbnailSprite::isCached(videoKey) ||
                VideoThumbnailSprite::isCached(legacyVideoKey)) {
                hasVideo = true;
            }
        }
        if (!hasVideo) {
            PaimonNotify::create(
                Localization::get().getString("profilebg.picker.video_audio_no_video").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }

        auto loading = PaimonLoadingOverlay::create(
            Localization::get().getString("profilebg.video_audio.applying").c_str(),
            30.f
        );
        Ref<PaimonLoadingOverlay> loadingRef = loading;
        if (loading) loading->show(this, 100);

        // Recuperar config previa para preservar el resto de campos (blur,
        // darkness, widthFactor, comment-bg, etc.).  Solo cambiamos el flag
        // useVideoAudio.  El backgroundType se deja como esta — si el
        // usuario tiene un video subido, ya esta resuelto en el render.
        ThumbnailAPI::get().downloadProfileConfig(accountID,
            [self, accountID, loadingRef]
            (bool, ProfileConfig const& existing) {
                ProfileConfig cfg   = existing;
                cfg.hasConfig       = true;
                cfg.useVideoAudio   = true;

                ThumbnailAPI::get().uploadProfileConfig(accountID, cfg,
                    [self, accountID, cfg, loadingRef](bool success, std::string const& msg) {
                        // Borrar musica configurada en paralelo: aunque la
                        // subida del config falle queremos quitar la musica
                        // vieja para que no compita con el audio del video.
                        // No esperamos al callback para mostrar el resultado:
                        // basta con saber que se intento.
                        auto* accountManager = GJAccountManager::get();
                        std::string username = accountManager ? accountManager->m_username : std::string();
                        ProfileMusicManager::get().deleteProfileMusic(accountID, username,
                            [accountID](bool delOk, std::string const& delMsg) {
                                if (delOk) {
                                    log::info("[ProfileBg] Cleared configured music after Audio Video for account {}", accountID);
                                } else {
                                    // No-op si no habia musica configurada.
                                    log::info("[ProfileBg] deleteProfileMusic returned: {}", delMsg);
                                }
                            });
                        // Invalida cache local de musica para que la proxima
                        // visita lea la nueva config.
                        ProfileMusicManager::get().invalidateCache(accountID);

                        if (loadingRef) loadingRef->dismiss();
                        if (!success) {
                            PaimonNotify::create(
                                fmt::format("{}: {}",
                                    Localization::get().getString("profilebg.video_audio.failed"),
                                    msg
                                ).c_str(),
                                NotificationIcon::Error
                            )->show();
                            return;
                        }

                        ProfileThumbs::get().cacheProfileConfig(accountID, cfg);

                        PaimonNotify::create(
                            Localization::get().getString("profilebg.video_audio.applied").c_str(),
                            NotificationIcon::Success
                        )->show();
                    });
            });
    }

    // Restablece el fondo de perfil: limpia config en servidor + cache local.
    void confirmProfileBgReset() {
        if (!this->m_ownProfile) {
            PaimonNotify::create(
                Localization::get().getString("profile.cant_edit_other").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }

        int accountID = this->m_accountID;
        WeakRef<PaimonProfilePage> self = this;

        // Sube una config "neutra" con backgroundType = "none" para que los
        // visitantes no vean ni miniatura ni degradado.  El asset binario
        // (imagen/GIF/video) sigue en el servidor pero el config gana en
        // el render.  Si quisieramos borrarlo del bucket habria que llamar
        // a un endpoint de delete dedicado.
        ProfileConfig cfg;
        cfg.hasConfig      = true;
        cfg.backgroundType = "none";
        cfg.useGradient    = false;

        auto loading = PaimonLoadingOverlay::create(
            Localization::get().getString("profilebg.reset.applying").c_str(),
            20.f
        );
        Ref<PaimonLoadingOverlay> loadingRef = loading;
        if (loading) loading->show(this, 100);

        ThumbnailAPI::get().uploadProfileConfig(accountID, cfg,
            [self, accountID, cfg, loadingRef](bool success, std::string const& msg) {
                if (loadingRef) loadingRef->dismiss();
                if (!success) {
                    PaimonNotify::create(
                        fmt::format("{}: {}",
                            Localization::get().getString("profilebg.reset.failed"),
                            msg
                        ).c_str(),
                        NotificationIcon::Error
                    )->show();
                    return;
                }

                ProfileThumbs::get().cacheProfileConfig(accountID, cfg);
                PaimonNotify::create(
                    Localization::get().getString("profilebg.reset.applied").c_str(),
                    NotificationIcon::Success
                )->show();

                if (auto page = self.lock()) {
                    page->clearProfileBgVisual();
                }
            });
    }

    // Devuelve los colores actuales del icono del jugador del perfil que
    // se esta viendo.  Para tu propio perfil usa GameManager (siempre live).
    // Para otros perfiles usa los color1/color2 del GJUserScore que GD ya
    // cargo al abrir el perfil.  Esto permite que el degradado refleje
    // cambios de color sin necesidad de re-subir la config.
    bool getLiveProfileColors(cocos2d::ccColor3B& outA, cocos2d::ccColor3B& outB) {
        auto* gm = GameManager::sharedState();
        if (!gm) return false;

        if (this->m_ownProfile) {
            outA = gm->colorForIdx(gm->getPlayerColor());
            outB = gm->colorForIdx(gm->getPlayerColor2());
            return true;
        }
        if (this->m_score) {
            outA = gm->colorForIdx(this->m_score->m_color1);
            outB = gm->colorForIdx(this->m_score->m_color2);
            return true;
        }
        return false;
    }

    // Sustituye el banner de fondo del perfil por un CCLayerGradient pintado
    // con los colores del icono del jugador.  Mantiene la misma geometria
    // (clipping al area del popup) que la version con imagen para que el
    // resto del estilo del perfil siga funcionando.  Si effect != "none"
    // anima el gradient con el efecto correspondiente.
    void displayProfileBgGradient(cocos2d::ccColor3B colorA,
                                  cocos2d::ccColor3B colorB,
                                  std::string const& effect = "none",
                                  float speed = 1.0f) {
        auto f = m_fields.self();

        // Limpia banner anterior (imagen / gif / video)
        if (f->m_profileImgClip) {
            CCClippingNode* clip = f->m_profileImgClip;
            if (clip) {
                for (auto* child : CCArrayExt<CCNode*>(clip->getChildren())) {
                    if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(child)) {
                        videoSprite->stop();
                    }
                }
            }
            f->m_profileImgClip->removeFromParent();
            f->m_profileImgClip = nullptr;
        }

        auto layer = this->m_mainLayer;
        if (!layer) return;
        auto layerSize = layer->getContentSize();

        CCSize popupSize = CCSize(440.f, 290.f);
        CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        auto popupGeo = paimon::compat::InfoLayerLocator::findPopupGeometry(layer);
        if (popupGeo.found) {
            popupSize = popupGeo.size;
            popupCenter = popupGeo.center;
        }

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(imgArea.width, imgArea.height);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);

        // Degradado animado con los colores del icono y el efecto pedido.
        auto* grad = paimon::profilebg::AnimatedGradientLayer::create(colorA, colorB);
        if (grad) {
            grad->setContentSize(imgArea);
            grad->setAnchorPoint({0.5f, 0.5f});
            grad->ignoreAnchorPointForPosition(false);
            grad->setPosition({imgArea.width * 0.5f, imgArea.height * 0.5f});
            clip->addChild(grad);
            grad->setEffect(
                paimon::profilebg::normalizeEffect(effect),
                paimon::profilebg::normalizeSpeed(speed)
            );
        }

        // Mismo overlay oscuro suave que el flujo de imagen para no romper
        // el contraste del username/labels.
        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 70));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-profileimg-dark-overlay"_spr);
        clip->addChild(dark);

        layer->addChild(clip, paimon::settings::profiles::profileImgZLayer());
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;

        styleProfileInternalBgs(layer);
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 1.5f);
    }

    void clearProfileBgVisual() {
        auto f = m_fields.self();
        if (f->m_profileImgClip) {
            CCClippingNode* clip = f->m_profileImgClip;
            if (clip) {
                for (auto* child : CCArrayExt<CCNode*>(clip->getChildren())) {
                    if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(child)) {
                        videoSprite->stop();
                    }
                }
            }
            f->m_profileImgClip->removeFromParent();
            f->m_profileImgClip = nullptr;
        }
        f->m_hasProfileBackdrop = false;
    }

    void processProfileImg(std::filesystem::path path) {
        // Check if it's a video file first
        std::string ext = geode::utils::string::pathToString(path.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        bool isVideo = (ext == ".mp4" || ext == ".mov" || ext == ".m4v");

        if (isVideo) {
            // Video: read and upload
            auto videoData = ImageLoadHelper::readBinaryFile(path, 50); // 50MB limit for videos
            if (videoData.empty()) {
                PaimonNotify::create(Localization::get().getString("profile.video_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            int accountID = this->m_accountID;
            auto* accountManager = GJAccountManager::get();
            if (!accountManager) {
                PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
                return;
            }
            std::string username = accountManager->m_username;

            auto videoSpinner = PaimonLoadingOverlay::create("Uploading video...", 30.f);
            videoSpinner->show(this, 100);
            Ref<PaimonLoadingOverlay> loading = videoSpinner;

            paimon::showBetaUploadWarningIfNeeded([this, accountID, videoData = std::move(videoData), username, loading]() mutable {
                ThumbnailAPI::get().uploadProfileVideo(accountID, videoData, username, [this, accountID, videoData, loading](bool success, std::string const& msg) {
                    if (loading) loading->dismiss();

                    if (success) {
                        PaimonNotify::create("Profile video uploaded!", NotificationIcon::Success)->show();
                        saveProfileImgToDisk(accountID, videoData);
                        ProfileImageService::get().rememberProfileImgGifKey(accountID, fmt::format("profileimg_video_{}", accountID));
                        this->ensureAnimatedProfileImg(accountID);
                    } else {
                        PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                    }
                });
            });
            return;
        }

        if (ImageLoadHelper::isGIF(path)) {
            // GIF: subir directamente
            auto imgData = ImageLoadHelper::readBinaryFile(path, 25);
            if (imgData.empty()) {
                PaimonNotify::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            int accountID = this->m_accountID;
            auto* accountManager = GJAccountManager::get();
            if (!accountManager) {
                PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
                return;
            }
            std::string username = accountManager->m_username;

            auto gifSpinner = PaimonLoadingOverlay::create("Uploading...", 30.f);
            gifSpinner->show(this, 100);
            Ref<PaimonLoadingOverlay> loading = gifSpinner;

            Ref<ProfilePage> imgGifSafeRef = this;

            paimon::showBetaUploadWarningIfNeeded([imgGifSafeRef, accountID, imgData = std::move(imgData), username, loading]() mutable {
                ThumbnailAPI::get().uploadProfileImgGIF(accountID, imgData, username, [imgGifSafeRef, accountID, imgData, loading](bool success, std::string const& msg) {
                    if (loading) loading->dismiss();

                    if (success) {
                        PaimonNotify::create("Profile image uploaded!", NotificationIcon::Success)->show();

                        saveProfileImgToDisk(accountID, imgData);
                        ProfileImageService::get().rememberProfileImgGifKey(accountID, getProfileImgGifCacheKey(accountID));
                        auto* page = static_cast<PaimonProfilePage*>(imgGifSafeRef.data());
                        if (page && page->ensureAnimatedProfileImg(accountID)) {
                            return;
                        }

                        if (paimon::format::isGif(imgData.data(), imgData.size())) {
                            auto gifResult = GIFDecoder::decode(imgData.data(), imgData.size());
                            if (!gifResult.frames.empty()) {
                                auto& firstFrame = gifResult.frames[0];
                                if (firstFrame.width > 0 && firstFrame.height > 0) {
                                        auto* tex = new CCTexture2D();
                                        if (tex->initWithData(
                                            firstFrame.pixels.data(),
                                            kCCTexture2DPixelFormat_RGBA8888,
                                            firstFrame.width,
                                            firstFrame.height,
                                            CCSize(static_cast<float>(firstFrame.width), static_cast<float>(firstFrame.height))
                                        )) {
                                            tex->autorelease();
                                            cacheProfileImgTexture(accountID, tex);
                                            if (auto* page = static_cast<PaimonProfilePage*>(imgGifSafeRef.data())) {
                                                page->displayProfileImg(accountID, tex);
                                            }
                                        } else {
                                            tex->release();
                                        }
                                }
                            }
                        }
                    } else {
                        PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                    }
                });
            });
            return;
        }

        // Imagen estatica: usar helper pa cargar y convertir
        auto loaded = ImageLoadHelper::loadStaticImage(path, 25);
        if (!loaded.success) {
            std::string errKey = loaded.error;
            // Traduce error si es key de localizacion
            if (errKey == "image_open_error" || errKey == "invalid_image_data" || errKey == "texture_error") {
                PaimonNotify::create(Localization::get().getString("profile." + errKey).c_str(), NotificationIcon::Error)->show();
            } else {
                PaimonNotify::create(errKey.c_str(), NotificationIcon::Error)->show();
            }
            return;
        }

        // Retain textura para que sobreviva al popup
        CC_SAFE_RETAIN(loaded.texture);

        int accountID = this->m_accountID;
        Ref<ProfilePage> previewCbRef = this;

        auto popup = CapturePreviewPopup::create(
            loaded.texture,
            accountID,
            loaded.buffer,
            loaded.width,
            loaded.height,
            [previewCbRef, accountID](bool ok, int id, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                auto* page = static_cast<PaimonProfilePage*>(previewCbRef.data());
                if (!page || !page->getParent()) return;
                if (ok && buf) {
                    // Convierte RGBA a PNG en memoria
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::rgbaToPngBuffer(buf.get(), w, h, pngData)) {
                        return;
                    }

                        auto* accountManager = GJAccountManager::get();
                        if (!accountManager) {
                            PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
                            return;
                        }
                        std::string username = accountManager->m_username;

                        auto pngSpinner = PaimonLoadingOverlay::create("Uploading...", 30.f);
                        pngSpinner->show(page, 100);
                        Ref<PaimonLoadingOverlay> loading = pngSpinner;

                        Ref<ProfilePage> imgUploadRef = previewCbRef;

                        ThumbnailAPI::get().uploadProfileImg(accountID, pngData, username, "image/png", [imgUploadRef, accountID, pngData, loading, buf, w, h](bool success, std::string const& msg) {
                            if (loading) loading->dismiss();

                if (success) {
                    bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);

                                if (isPending) {
                                    PaimonNotify::create("Image submitted! Pending moderator verification.", NotificationIcon::Warning)->show();
                                } else {
                                    PaimonNotify::create("Profile image uploaded!", NotificationIcon::Success)->show();
                                }

                    saveProfileImgToDisk(accountID, pngData);
                    ProfileImageService::get().clearProfileImgGifKey(accountID);

                    CCImage finalImg;
                                if (finalImg.initWithImageData(buf.get(), w * h * 4, CCImage::kFmtRawData, w, h)) {
                                    // Ref::adopt toma ownership del refcount=1 del `new CCTexture2D()`
                                    // sin retener extra. Sin adopt el Ref retiene (refcount=2) y al
                                    // destruirse solo baja a 1 â€” leak permanente del objeto CCTexture2D.
                                    auto finalTex = geode::Ref<CCTexture2D>::adopt(new CCTexture2D());
                                    if (finalTex->initWithImage(&finalImg)) {
                                        // Ref<> hace retain/release automaticamente
                                        cacheProfileImgTexture(accountID, finalTex);
                                        if (auto* page = static_cast<PaimonProfilePage*>(imgUploadRef.data())) {
                                            page->displayProfileImg(accountID, finalTex);
                                        }
                                    }
                                }
                            } else {
                                PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                            }
                        });
                }
            }
        );
        if (popup) popup->show();
    }

    // === FUNCIONES DE MuSICA DE PERFIL ===

    void onOpenProfileSettings(CCObject*) {
        if (!this->m_ownProfile) return;

        auto popup = ProfileSettingsPopup::create(this->m_accountID);
        if (!popup) return;

        WeakRef<PaimonProfilePage> self = this;
        popup->setOnMusicCallback([self]() {
            if (auto page = self.lock()) {
                page->onConfigureProfileMusic(nullptr);
            }
        });
        popup->setOnImageCallback([self]() {
            if (auto page = self.lock()) {
                page->onAddProfileImg(nullptr);
            }
        });
        popup->setOnBadgeCallback([self]() {
            auto page = self.lock();
            if (!page) return;
            int accID = page->m_accountID;
            CustomBadgeService::get().fetchBadge(accID, [self, accID](bool, std::string const& currentBadge) {
                auto page = self.lock();
                if (!page) return;
                auto picker = CustomBadgePickerPopup::create(accID, currentBadge);
                if (picker) picker->show();
            });
        });
        popup->setOnCommentBgCallback([self]() {
            auto page = self.lock();
            if (!page) return;
            int accID = page->m_accountID;
            // Descarga config y abre CommentBgSettingsPopup
            ThumbnailAPI::get().downloadProfileConfig(accID, [accID](bool success, ProfileConfig const& config) {
                ProfileConfig effectiveConfig = success ? config : ProfileConfig();
                auto popup = CommentBgSettingsPopup::create(accID, effectiveConfig);
                if (popup) popup->show();
            });
        });
        popup->show();
    }

    void onConfigureProfileMusic(CCObject*) {
        // Solo en perfil propio
        if (!this->m_ownProfile) {
            PaimonNotify::create("You can only configure music on your own profile", NotificationIcon::Warning)->show();
            return;
        }

        // Abre popup de musica
        if (auto popup = ProfileMusicPopup::create(this->m_accountID)) {
            popup->show();
        }
    }

    void onToggleProfileMusic(CCObject*) {
        auto& musicManager = ProfileMusicManager::get();

        if (musicManager.isPlaying()) {
            if (musicManager.isPaused()) {
                musicManager.resumeProfileMusic();
                m_fields->m_musicPlaying = true;
                updatePauseButtonSprite(true);
            } else {
                musicManager.pauseProfileMusic();
                m_fields->m_musicPlaying = false;
                updatePauseButtonSprite(false);
            }
        } else {
            // Reproduce si no esta sonando
            AudioContextCoordinator::get().activateProfile(this->m_accountID);
            m_fields->m_musicPlaying = true;
            updatePauseButtonSprite(true);
        }
    }

    void updatePauseButtonSprite(bool isPlaying) {
        if (!m_fields->m_musicPauseBtn) return;

        // Cambia sprite segun estado
        CCSprite* newSpr = nullptr;
        if (isPlaying) {
            newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_fxOnBtn_001.png");
            if (!newSpr) newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_pauseBtn_001.png");
        } else {
            newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_fxOffBtn_001.png");
            if (!newSpr) newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playBtn_001.png");
        }

        if (newSpr) {
            float targetSize = 25.0f;
            float currentSize = std::max(newSpr->getContentWidth(), newSpr->getContentHeight());
            if (currentSize > 0) {
                newSpr->setScale(targetSize / currentSize);
            }
            m_fields->m_musicPauseBtn->setSprite(newSpr);
        }
    }

    void checkAndPlayProfileMusic(int accountID,
                                  std::optional<ProfileMusicManager::ProfileMusicConfig> resolvedConfig = std::nullopt,
                                  bool hasResolvedConfig = false,
                                  bool allowServerFetch = true) {
        // Verifica si musica de perfiles esta habilitada
        if (!ProfileMusicManager::get().isEnabled()) {
            m_fields->m_menuMusicPaused = false;
            return;
        }

        auto& musicMgr = ProfileMusicManager::get();

        // Si la ProfileConfig cacheada del fondo activa el modo Audio Video,
        // resolvemos el path del .mp4 cacheado del fondo y forzamos esa
        // config sobre la musica del perfil.  Tiene prioridad sobre cualquier
        // songID/isCustom configurado.
        auto applyVideoAudioOverride = [accountID](ProfileMusicManager::ProfileMusicConfig& cfg) -> bool {
            auto bgConfig = ProfileThumbs::get().getProfileConfig(accountID);
            if (!bgConfig.hasConfig || !bgConfig.useVideoAudio) return false;

            std::string videoPath;
            auto videoKey = fmt::format("profileimg_video_{}", accountID);
            videoPath = VideoThumbnailSprite::getCachedPathForKey(videoKey);
            if (videoPath.empty()) {
                auto legacyKey = fmt::format("profile_video_{}", accountID);
                videoPath = VideoThumbnailSprite::getCachedPathForKey(legacyKey);
            }

            cfg.useVideoAudio = true;
            cfg.videoAudioPath = videoPath;  // puede estar vacio si todavia no se descargo
            cfg.enabled = true;               // forzar enabled aunque venga off
            return true;
        };

        // Optimistic: reproduce desde cache
        ProfileMusicManager::ProfileMusicConfig cachedConfig;
        bool hasCachedConfig = musicMgr.tryGetImmediateConfig(accountID, cachedConfig);
        bool useVideoAudioOverride = applyVideoAudioOverride(cachedConfig);
        bool playableFromCache = hasCachedConfig &&
            (cachedConfig.songID > 0 || cachedConfig.isCustom || cachedConfig.useVideoAudio) &&
            cachedConfig.enabled;
        // Para useVideoAudio no necesitamos el cache MP3 estandar — el WAV
        // del video se resuelve aparte.  Para el resto si.
        bool dataReady = cachedConfig.useVideoAudio
            ? !cachedConfig.videoAudioPath.empty()
            : (hasCachedConfig && musicMgr.isCached(accountID));
        if (playableFromCache && dataReady) {
            if (m_fields->m_musicPauseBtn) {
                m_fields->m_musicPauseBtn->setVisible(true);
                if (auto* sm = getSocialsMenu()) sm->updateLayout();
            }

            bool alreadyHandlingThisProfile = musicMgr.getCurrentPlayingProfile() == accountID &&
                (musicMgr.isPlaying() || musicMgr.isPaused() || musicMgr.isFadingOut());

            if (!alreadyHandlingThisProfile) {
                AudioContextCoordinator::get().activateProfile(accountID, cachedConfig);
                m_fields->m_musicPlaying = true;
                updatePauseButtonSprite(true);
            } else {
                m_fields->m_musicPlaying = !musicMgr.isPaused();
                updatePauseButtonSprite(!musicMgr.isPaused());
            }
        } else if (useVideoAudioOverride && cachedConfig.videoAudioPath.empty()) {
            // useVideoAudio activo pero el video todavia no esta cacheado.
            // No reproducimos: cuando el video llegue, ensureAnimatedProfileImg
            // hara su trabajo y la proxima invocacion de checkAndPlayProfileMusic
            // tendra el path resuelto.
            log::info("[ProfilePage] useVideoAudio set but video not yet cached for {}", accountID);
        }

        // Verifica config fresca del servidor
        Ref<ProfilePage> self = this;
        auto cachedCopy = hasCachedConfig
            ? std::optional<ProfileMusicManager::ProfileMusicConfig>(cachedConfig)
            : std::nullopt;

        auto applyResolvedConfig = [self, accountID, cachedCopy, applyVideoAudioOverride]
            (bool success, ProfileMusicManager::ProfileMusicConfig const& configIn) {
            if (!self || !self->getParent()) return;
            if (self->m_accountID != accountID) return;

            auto* page = static_cast<PaimonProfilePage*>(self.data());
            if (!page || page->m_fields->m_leaveForClose) return;

            // Aplicar override de useVideoAudio antes de validar.  Una copia
            // local porque el lambda recibe el config por referencia const.
            ProfileMusicManager::ProfileMusicConfig config = configIn;
            applyVideoAudioOverride(config);

            bool hasPlayableSource = config.songID > 0 || config.isCustom || config.useVideoAudio;
            if (!success || !hasPlayableSource || !config.enabled) {
                if (page->m_fields->m_musicPlaying) {
                    ProfileMusicManager::get().stopProfileMusic();
                    page->m_fields->m_musicPlaying = false;
                }
                if (page->m_fields->m_musicPauseBtn) {
                    page->m_fields->m_musicPauseBtn->setVisible(false);
                }
                page->m_fields->m_menuMusicPaused = false;
                return;
            }

            bool configChanged = !cachedCopy.has_value()
                || cachedCopy->songID != config.songID
                || cachedCopy->startMs != config.startMs
                || cachedCopy->endMs != config.endMs
                || cachedCopy->updatedAt != config.updatedAt
                || cachedCopy->isCustom != config.isCustom
                || cachedCopy->useVideoAudio != config.useVideoAudio
                || cachedCopy->videoAudioPath != config.videoAudioPath;

            if (!configChanged && page->m_fields->m_musicPlaying) {
                if (page->m_fields->m_musicPauseBtn) {
                    page->m_fields->m_musicPauseBtn->setVisible(true);
                    if (auto* socialsMenu = page->getSocialsMenu()) {
                        socialsMenu->updateLayout();
                    }
                }
                page->updatePauseButtonSprite(!ProfileMusicManager::get().isPaused());
                return;
            }

            if (page->m_fields->m_musicPauseBtn) {
                page->m_fields->m_musicPauseBtn->setVisible(true);
                if (auto* socialsMenu = page->getSocialsMenu()) {
                    socialsMenu->updateLayout();
                }
            }

            AudioContextCoordinator::get().updateProfileMusicConfig(accountID, config);
            page->m_fields->m_musicPlaying = true;
            page->updatePauseButtonSprite(true);
        };

        if (hasResolvedConfig) {
            applyResolvedConfig(resolvedConfig.has_value(), resolvedConfig.value_or(ProfileMusicManager::ProfileMusicConfig{}));
            return;
        }

        if (!allowServerFetch) {
            return;
        }

        musicMgr.getProfileMusicConfig(accountID, [applyResolvedConfig](bool success, const ProfileMusicManager::ProfileMusicConfig& config) {
            Loader::get()->queueInMainThread([applyResolvedConfig, success, config]() {
                applyResolvedConfig(success, config);
            });
        });
    }

    void cleanupProfileAudio() {
        if (m_fields->m_audioCleanedUp) return;
        m_fields->m_audioCleanedUp = true;

        auto& musicMgr = ProfileMusicManager::get();
        bool hadProfileAudio = musicMgr.isPlaying() || musicMgr.isPaused() || musicMgr.isFadingOut();
        auto sessionToken = AudioContextCoordinator::get().getCurrentProfileSessionToken();
        if (hadProfileAudio) {
            musicMgr.forceStop();
        }
        AudioContextCoordinator::get().handleProfileClosedAfterForceStop(hadProfileAudio, sessionToken);
        m_fields->m_menuMusicPaused = false;
        m_fields->m_pausedForTemporaryExit = false;
    }

    $override
    void keyBackClicked() {
        m_fields->m_leaveForClose = true;
        cleanupProfileAudio();
        ProfilePage::keyBackClicked();
    }

    $override
    void onClose(CCObject* sender) {
        m_fields->m_leaveForClose = true;
        cleanupProfileAudio();
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->unschedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity));
        this->unschedule(schedule_selector(PaimonProfilePage::fadeStepTick));
        ProfilePage::onClose(sender);
    }

    $override
    void onEnterTransitionDidFinish() {
        ProfilePage::onEnterTransitionDidFinish();
        auto& musicMgr = ProfileMusicManager::get();
        if (m_fields->m_pausedForTemporaryExit && m_fields->m_musicPlaying &&
            musicMgr.isPlaying() && musicMgr.isPaused()) {
            musicMgr.resumeProfileMusic();
            updatePauseButtonSprite(true);
        }
        m_fields->m_pausedForTemporaryExit = false;
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->unschedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity));
        this->unschedule(schedule_selector(PaimonProfilePage::fadeStepTick));

        cleanupProfileAudio();

        ProfilePage::onExit();
    }

    void fadeMenuMusicStep(Ref<ProfilePage> safeRef, int step, int totalSteps, float fromVol, float toVol) {
        // Fade progresivo usando scheduler de Cocos2d
        float stepDelay = 500.0f / static_cast<float>(totalSteps) / 1000.0f; // a segundos

        // Aplica paso actual
        if (step >= totalSteps) {
            auto engine = FMODAudioEngine::sharedEngine();
            if (engine && engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setVolume(toVol);
            }
            return;
        }

        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        float eT = (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);
        float vol = fromVol + (toVol - fromVol) * eT;

        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(std::max(0.0f, std::min(1.0f, vol)));
        }

        // Guarda estado en Fields
        m_fields->m_fadeStep = step + 1;
        m_fields->m_fadeTotalSteps = totalSteps;
        m_fields->m_fadeFromVol = fromVol;
        m_fields->m_fadeToVol = toVol;

        // Programa siguiente paso
        this->unschedule(schedule_selector(PaimonProfilePage::fadeStepTick));
        this->scheduleOnce(
            schedule_selector(PaimonProfilePage::fadeStepTick),
            stepDelay
        );
    }

    void fadeStepTick(float) {
        if (!this->getParent()) return;
        if (m_fields->m_leaveForClose) return;
        Ref<ProfilePage> safeRef = this;
        this->fadeMenuMusicStep(
            safeRef,
            m_fields->m_fadeStep,
            m_fields->m_fadeTotalSteps,
            m_fields->m_fadeFromVol,
            m_fields->m_fadeToVol
        );
    }
};
