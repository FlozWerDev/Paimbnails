#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
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
#include <chrono>
#include <cmath>
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
#include "../core/Settings.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../features/moderation/services/ModeratorCache.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include <prevter.imageplus/include/events.hpp>
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/profiles/ui/ProfileSettingsPopup.hpp"
#include "../features/profiles/ui/CommentBgSettingsPopup.hpp"
#include "../features/profiles/ui/CustomBadgePickerPopup.hpp"
#include "../features/profiles/services/CustomBadgeService.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"

using namespace geode::prelude;

// CCScale9Sprite::create crashea si el sprite no existe (no retorna nullptr).
// Usar paimon::SpriteHelper::safeCreateScale9() del header compartido.

// cache de texturas de profileimg para carga instantanea entre popups.
// Usa Ref<> para manejo automatico de refcount, con guardia de shutdown
// para evitar release() cuando el CCPoolManager ya este destruido.
static std::mutex s_profileImgMutex;
static std::unordered_map<int, geode::Ref<CCTexture2D>> s_profileImgCache;
static std::list<int> s_profileImgLru;
static std::unordered_map<int, std::list<int>::iterator> s_profileImgLruMap;
static constexpr size_t MAX_PROFILEIMG_CACHE_SIZE = 64;
static constexpr size_t MAX_PROFILEIMG_CACHE_BYTES = 64ull * 1024 * 1024; // 64 MB
static size_t s_profileImgCacheBytes = 0;
static std::atomic<bool> s_profileImgShutdown{false};

static size_t estimateTextureBytes(CCTexture2D* tex) {
    if (!tex) return 0;
    return static_cast<size_t>(tex->getPixelsWide()) * static_cast<size_t>(tex->getPixelsHigh()) * 4;
}

static void touchProfileImgCache(int accountID) {
    auto it = s_profileImgLruMap.find(accountID);
    if (it != s_profileImgLruMap.end()) {
        s_profileImgLru.erase(it->second);
    }
    s_profileImgLru.push_back(accountID);
    s_profileImgLruMap[accountID] = std::prev(s_profileImgLru.end());
}

static void cacheProfileImgTexture(int accountID, CCTexture2D* texture) {
    if (!texture) return;
    std::lock_guard<std::mutex> lock(s_profileImgMutex);

    size_t incomingBytes = estimateTextureBytes(texture);

    // if replacing an existing entry, subtract old bytes
    auto oldIt = s_profileImgCache.find(accountID);
    if (oldIt != s_profileImgCache.end()) {
        size_t oldBytes = estimateTextureBytes(oldIt->second);
        if (s_profileImgCacheBytes >= oldBytes) s_profileImgCacheBytes -= oldBytes;
    }

    s_profileImgCache[accountID] = texture;
    s_profileImgCacheBytes += incomingBytes;
    touchProfileImgCache(accountID);

    // evict by entry count OR byte limit
    while ((s_profileImgCache.size() > MAX_PROFILEIMG_CACHE_SIZE ||
            s_profileImgCacheBytes > MAX_PROFILEIMG_CACHE_BYTES) &&
           !s_profileImgLru.empty()) {
        int removeID = s_profileImgLru.front();
        s_profileImgLru.pop_front();
        s_profileImgLruMap.erase(removeID);
        auto removeIt = s_profileImgCache.find(removeID);
        if (removeIt != s_profileImgCache.end()) {
            size_t removedBytes = estimateTextureBytes(removeIt->second);
            if (s_profileImgCacheBytes >= removedBytes) s_profileImgCacheBytes -= removedBytes;
            s_profileImgCache.erase(removeIt);
        }
    }
}

// Limpiar el cache de profileimg durante el cierre del juego.
// Los destructores estaticos se ejecutan en orden indefinido y
// CCPoolManager puede ya estar muerto â€” usamos take() para sacar
// los Ref<> sin llamar release().
$on_game(Exiting) {
    s_profileImgShutdown.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    for (auto& [id, ref] : s_profileImgCache) {
        (void)ref.take();
    }
    s_profileImgCache.clear();
    s_profileImgCacheBytes = 0;
    s_profileImgLru.clear();
    s_profileImgLruMap.clear();
}

// Acceso externo al cache de profileimg (usado por InfoLayer hook).
CCTexture2D* getProfileImgCachedTexture(int accountID) {
    if (s_profileImgShutdown.load(std::memory_order_acquire)) return nullptr;
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    auto it = s_profileImgCache.find(accountID);
    if (it != s_profileImgCache.end()) {
        touchProfileImgCache(accountID);
        return it->second;
    }
    return nullptr;
}

// --- helpers de cache de disco para profileimg ---
static std::filesystem::path getProfileImgCacheDir() {
    return Mod::get()->getSaveDir() / "profileimg_cache";
}

static std::filesystem::path getProfileImgCachePath(int accountID) {
    return getProfileImgCacheDir() / fmt::format("{}.dat", accountID);
}

static std::string getProfileImgGifCacheKey(int accountID) {
    auto key = ProfileImageService::get().getProfileImgGifKey(accountID);
    if (!key.empty()) {
        return key;
    }
    return fmt::format("profileimg_gif_{}", accountID);
}

// Limpia todo el cache de profileimg (RAM + disco)
void clearProfileImgCache() {
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    s_profileImgCache.clear();
    s_profileImgCacheBytes = 0;
    s_profileImgLru.clear();
    s_profileImgLruMap.clear();
    std::error_code ec;
    auto dir = getProfileImgCacheDir();
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::remove_all(dir, ec);
    }
}

static CCTexture2D* loadProfileImgFromDisk(int accountID) {
    auto path = getProfileImgCachePath(accountID);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return nullptr;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;

    auto size = file.tellg();
    if (size <= 0) return nullptr;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return nullptr;
    file.close();

    if (imgp::formats::isGif(data.data(), data.size())
        || imgp::formats::isAPng(data.data(), data.size())) {
        return nullptr;
    }

    // skip MP4 video files (handled by ensureAnimatedProfileImg)
    // ftyp box can start at different offsets — search first 12 bytes
    if (data.size() > 12) {
        for (size_t i = 0; i + 3 < 12 && i + 3 < data.size(); ++i) {
            if (data[i]=='f' && data[i+1]=='t' && data[i+2]=='y' && data[i+3]=='p') {
                return nullptr;
            }
        }
    }

    auto loaded = ImageLoadHelper::loadWithSTBFromMemory(data.data(), data.size());
    if (!loaded.success || !loaded.texture) {
        return nullptr;
    }
    loaded.texture->autorelease();
    return loaded.texture;
}

static void saveProfileImgToDisk(int accountID, std::vector<uint8_t> const& data) {
    auto cacheDir = getProfileImgCacheDir();
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    auto cachePath = getProfileImgCachePath(accountID);
    std::ofstream cacheFile(cachePath, std::ios::binary);
    if (cacheFile) {
        cacheFile.write(reinterpret_cast<char const*>(data.data()), data.size());
        cacheFile.close();
    }
}

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
        CCLabelBMFont* m_thumbCountLabel = nullptr;
    };

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
        auto menu = this->getChildByIDRecursive("username-menu");
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

    void addCustomBadgeToProfile(std::string const& emoteName) {
        if (emoteName.empty()) return;

        auto menu = this->getChildByIDRecursive("username-menu");
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
                            auto menu = typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("username-menu"));
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
                        auto menu = typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("username-menu"));
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

    void addThumbnailCountBadge(int uploadCount) {
        if (!this->m_mainLayer) return;

        // Find the stats-menu (Geode node-ids assigns this on ProfilePage)
        auto* statsMenu = this->m_mainLayer->getChildByIDRecursive("stats-menu");
        if (!statsMenu) {
            log::debug("[ProfilePage] stats-menu not found, skipping thumbnail count badge");
            return;
        }

        // Avoid duplicates
        if (statsMenu->getChildByID("paimon-thumb-count-icon"_spr)) return;

        // Only show if user has at least 1 approved thumbnail
        if (uploadCount <= 0) return;

        auto* statsMenuCC = typeinfo_cast<CCMenu*>(statsMenu);
        if (!statsMenuCC) return;

        // Create the count label (like SendDB's sends label)
        auto* countLabel = CCLabelBMFont::create(
            fmt::format("{}", uploadCount).c_str(),
            "bigFont.fnt"
        );
        countLabel->setScale(0.6f);
        countLabel->setAnchorPoint({0.0f, 0.0f});
        countLabel->setID("paimon-thumb-count-label"_spr);
        countLabel->setZOrder(2);

        // Text node that wraps the label for layout
        auto* textNode = CCNode::create();
        textNode->setAnchorPoint({0.0f, 0.0f});
        textNode->setContentSize(CCPoint{0.6f, 0.6f} * countLabel->getContentSize());
        textNode->setID("paimon-thumb-count-text"_spr);
        textNode->setZOrder(1);
        textNode->addChild(countLabel);
        textNode->setLayoutOptions(AxisLayoutOptions::create()
            ->setScaleLimits(0.0f, 1.0f)
        );

        // Icon sprite — use the mod's thumbnail icon or a GD frame
        auto* iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
        if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
        if (!iconSprite) iconSprite = CCSprite::create();
        if (iconSprite) {
            scaleToFit(iconSprite, 20.f);
            iconSprite->setAnchorPoint({0.5f, 0.5f});
            iconSprite->setID("paimon-thumb-count-icon"_spr);
            iconSprite->setZOrder(1);
            iconSprite->setLayoutOptions(AxisLayoutOptions::create()
                ->setScaleLimits(0.0f, 1.0f)
                ->setRelativeScale(0.8f)
                ->setNextGap(5.0f)
            );
        }

        statsMenuCC->addChild(textNode);
        if (iconSprite) statsMenuCC->addChild(iconSprite);
        statsMenuCC->updateLayout();

        m_fields->m_thumbCountLabel = countLabel;
        log::debug("[ProfilePage] Added thumbnail count badge: {} uploads", uploadCount);
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

        layer->addChild(clip, Mod::get()->getSettingValue<int64_t>("profile-img-zlayer"));
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 0.0f);
        styleProfileInternalBgs(layer);
    }

    void displayProfileImgVideo(std::string const& videoKey) {
        auto video = VideoThumbnailSprite::createFromCache(videoKey);
        if (!video) return;

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

        float scaleX = imgArea.width / std::max(1.0f, video->getContentWidth());
        float scaleY = imgArea.height / std::max(1.0f, video->getContentHeight());
        video->setScale(std::max(scaleX, scaleY));
        video->setAnchorPoint(ccp(0.5f, 0.5f));
        video->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));
        clip->addChild(video);
        video->setOnFirstVisibleFrame([accountID = this->m_accountID](VideoThumbnailSprite* readyVideo) {
            if (auto* tex = readyVideo->getTexture()) {
                cacheProfileImgTexture(accountID, tex);
            }
        });
        video->play();

        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 70));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-profileimg-dark-overlay"_spr);
        clip->addChild(dark);

        layer->addChild(clip, Mod::get()->getSettingValue<int64_t>("profile-img-zlayer"));
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 0.0f);
        styleProfileInternalBgs(layer);
    }

    bool ensureAnimatedProfileImg(int accountID) {
        auto gifKey = getProfileImgGifCacheKey(accountID);

        // check for cached video first
        if (!gifKey.empty() && VideoThumbnailSprite::isCached(gifKey)) {
            displayProfileImgVideo(gifKey);
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
            for (size_t i = 0; i + 3 < 12 && i + 3 < bytes->size(); ++i) {
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

        bool isAnimatedImg = imgp::formats::isGif(bytes->data(), bytes->size())
                          || imgp::formats::isAPng(bytes->data(), bytes->size());
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

        // limpiar anteriores
        if (f->m_profileImgClip) { f->m_profileImgClip->removeFromParent(); f->m_profileImgClip = nullptr; }
        if (f->m_profileImgBorder) { f->m_profileImgBorder->removeFromParent(); f->m_profileImgBorder = nullptr; }

        bool queuedAnimated = ensureAnimatedProfileImg(accountID);

        if (!queuedAnimated) {
            // 1) si hay cache en memoria, mostrar de inmediato
            CCTexture2D* cachedTex = nullptr;
            {
                std::lock_guard<std::mutex> lock(s_profileImgMutex);
                auto it = s_profileImgCache.find(accountID);
                if (it != s_profileImgCache.end() && it->second) {
                    cachedTex = it->second;
                }
            }
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

        // descargar del servidor en segundo plano (actualizar cache)
        // Ref<> mantiene vivo el ProfilePage hasta que termine el callback
        Ref<ProfilePage> self = this;
        ThumbnailAPI::get().downloadProfileImg(accountID, [self, accountID](bool success, CCTexture2D* texture) {
            if (!self || !self->getParent()) return;

            if (success) {
                // Ref<> hace retain en la asignacion y release del anterior automaticamente
                auto* page = static_cast<PaimonProfilePage*>(self.data());
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

                // Oculta icon-background y pone panel oscuro
                if (child->getID() == "icon-background") {
                    child->setVisible(false);

                    // Solo agrega una vez
                    if (!parent->getChildByID("paimon-icon-dark-panel"_spr)) {
                        auto* panel = paimon::SpriteHelper::createDarkPanel(
                            340.f, 45.f, 100, 8.f
                        );
                        if (panel) {
                            panel->setPosition(ccp(283.f, 200.f));
                            panel->setAnchorPoint(ccp(0.5f, 0.5f));
                            panel->setZOrder(child->getZOrder());
                            panel->setID("paimon-icon-dark-panel"_spr);
                            parent->addChild(panel);
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
                            // Nodos sin ID: fondos decorativos
                            if (id.empty()) {
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

                    // Busca fondos vanilla recursivamente
                    auto hideBgsRecursive = [](auto const& recurse, CCNode* node) -> void {
                        if (!node) return;
                        auto* kids = node->getChildren();
                        if (!kids) return;
                        for (auto* k : CCArrayExt<CCNode*>(kids)) {
                            if (!k) continue;

                            std::string kID = k->getID();
                            if (!kID.empty() && kID.find("paimon-") != std::string::npos) {
                                continue;
                            }

                            if (typeinfo_cast<CCLayerColor*>(k)) {
                                k->setVisible(false);
                                continue;
                            }

                            if (typeinfo_cast<CCScale9Sprite*>(k)) {
                                k->setVisible(false);
                                continue;
                            }

                            if (!typeinfo_cast<CCMenu*>(k)) {
                                recurse(recurse, k);
                            }
                        }
                    };

                    hideBgsRecursive(hideBgsRecursive, child);
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
        if (!this->m_mainLayer) return CCDirector::sharedDirector()->getWinSize() / 2;
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
            // Si ya esta verificado como mod o admin â†’ mostrar gear (centro de verificacion)
            if (m_fields->m_isApprovedMod || m_fields->m_isAdmin) {
                ensureGearButton(menu);
            }
            // Si es admin â†’ mostrar boton de anadir moderador
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

        // ── Limpiar badge custom del username-menu en reloads ──
        if (auto* usernameMenu = this->getChildByIDRecursive("username-menu")) {
            while (auto* old = usernameMenu->getChildByID("paimon-custom-badge"_spr))
                old->removeFromParent();
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

            Ref<ProfilePage> badgeSafeRef = this;
            // ── Profile Bundle: mod status + badge + stats in 1 request ──
            // Replaces individual checkUserStatus + fetchBadge + getProfileStats calls
            int viewedAccountID = this->m_accountID;
            Ref<PaimonProfilePage> bundleSelf = this;

            HttpClient::get().downloadProfileBundle(viewedAccountID, badgeUsername,
                [bundleSelf, viewedAccountID, badgeUsername](bool success, std::string const& response) {
                    if (!success || response.empty()) return;
                    auto parsed = matjson::parse(response);
                    if (!parsed.isOk()) return;
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

                    // Music config del bundle
                    if (json.contains("music") && json["music"].isObject()) {
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
                        ProfileMusicManager::get().injectBundleConfig(viewedAccountID, musicCfg);
                    }

                    Loader::get()->queueInMainThread([bundleSelf, viewedAccountID, badgeUsername, isMod, isAdmin, emoteName, uploadCount]() {
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

        layer->addChild(clip, Mod::get()->getSettingValue<int64_t>("profile-img-zlayer"));
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;

        // Aplica estilos a nodos existentes
        styleProfileInternalBgs(layer);
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 0.0f);
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
            checkAndPlayProfileMusic(accountID);

            // Carga imagen de perfil
            addOrUpdateProfileImgOnPage(accountID, ownProfile);

            // Schedule verificacion de integridad cada 0.5s
            this->schedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity), 0.5f);

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
        WeakRef<PaimonProfilePage> self = this;
        pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
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

    void processProfileImg(std::filesystem::path path) {
        if (ImageLoadHelper::isGIF(path)) {
            // GIF: subir directamente
            auto imgData = ImageLoadHelper::readBinaryFile(path, 10);
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

                    if (imgp::formats::isGif(imgData.data(), imgData.size())) {
                        auto decResult = imgp::decode::gif(imgData.data(), imgData.size());
                        if (decResult.isOk()) {
                            auto& decVal = decResult.unwrap();
                            if (auto* anim = std::get_if<imgp::DecodedAnimation>(&decVal)) {
                                if (!anim->frames.empty() && anim->width > 0 && anim->height > 0) {
                                    auto* tex = new CCTexture2D();
                                    if (tex->initWithData(
                                        anim->frames[0].data.get(),
                                        kCCTexture2DPixelFormat_RGBA8888,
                                        anim->width,
                                        anim->height,
                                        CCSize(static_cast<float>(anim->width), static_cast<float>(anim->height))
                                    )) {
                                        tex->autorelease();
                                        cacheProfileImgTexture(accountID, tex);
                                        static_cast<PaimonProfilePage*>(imgGifSafeRef.data())->displayProfileImg(accountID, tex);
                                    } else {
                                        tex->release();
                                    }
                                }
                            }
                        }
                    }
                } else {
                    PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                }
            });
            return;
        }

        // Imagen estatica: usar helper pa cargar y convertir
        auto loaded = ImageLoadHelper::loadStaticImage(path, 10);
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
                                    auto finalTex = geode::Ref<CCTexture2D>(new CCTexture2D());
                                    if (finalTex->initWithImage(&finalImg)) {
                                        // Ref<> hace retain/release automaticamente
                                        cacheProfileImgTexture(accountID, finalTex);
                                        static_cast<PaimonProfilePage*>(imgUploadRef.data())->displayProfileImg(accountID, finalTex);
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

    void checkAndPlayProfileMusic(int accountID) {
        // Verifica si musica de perfiles esta habilitada
        if (!ProfileMusicManager::get().isEnabled()) {
            m_fields->m_menuMusicPaused = false;
            return;
        }

        auto& musicMgr = ProfileMusicManager::get();

        // Optimistic: reproduce desde cache
        ProfileMusicManager::ProfileMusicConfig cachedConfig;
        bool hasCachedConfig = musicMgr.tryGetImmediateConfig(accountID, cachedConfig);
        if (hasCachedConfig && (cachedConfig.songID > 0 || cachedConfig.isCustom) && cachedConfig.enabled && musicMgr.isCached(accountID)) {
            if (m_fields->m_musicPauseBtn) {
                m_fields->m_musicPauseBtn->setVisible(true);
                if (auto* sm = getSocialsMenu()) sm->updateLayout();
            }
            AudioContextCoordinator::get().activateProfile(accountID, cachedConfig);
            m_fields->m_musicPlaying = true;
            updatePauseButtonSprite(true);
        }

        // Verifica config fresca del servidor
        Ref<ProfilePage> self = this;
        auto cachedCopy = hasCachedConfig
            ? std::optional<ProfileMusicManager::ProfileMusicConfig>(cachedConfig)
            : std::nullopt;
        musicMgr.getProfileMusicConfig(accountID, [self, accountID, cachedCopy](bool success, const ProfileMusicManager::ProfileMusicConfig& config) {
            Loader::get()->queueInMainThread([self, success, config, accountID, cachedCopy]() {
                if (!self || !self->getParent()) return;
                // Verifica que estamos en el mismo perfil
                if (self->m_accountID != accountID) return;
                auto* page = static_cast<PaimonProfilePage*>(self.data());
                if (page->m_fields->m_leaveForClose) return;

                if (!success || (config.songID <= 0 && !config.isCustom) || !config.enabled) {
                    // Detiene si no hay musica
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

                // No hace nada si config es igual a cache
                bool configChanged = !cachedCopy.has_value()
                    || cachedCopy->songID != config.songID
                    || cachedCopy->startMs != config.startMs
                    || cachedCopy->endMs != config.endMs
                    || cachedCopy->updatedAt != config.updatedAt
                    || cachedCopy->isCustom != config.isCustom;

                if (!configChanged && page->m_fields->m_musicPlaying) {
                    return; // ya sonando con config correcta
                }

                // Reproduce musica nueva/actualizada
                if (page->m_fields->m_musicPauseBtn) {
                    page->m_fields->m_musicPauseBtn->setVisible(true);
                    if (auto* socialsMenu = page->getSocialsMenu()) {
                        socialsMenu->updateLayout();
                    }
                }

                AudioContextCoordinator::get().updateProfileMusicConfig(accountID, config);
                page->m_fields->m_musicPlaying = true;
                page->updatePauseButtonSprite(true);
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
