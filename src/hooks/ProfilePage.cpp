#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>
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
#include "../features/badges/services/RoleService.hpp"
#include "../features/badges/services/RoleBadges.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../features/profiles/ui/RateProfilePopup.hpp"
#include "../features/profiles/ui/ProfileReviewsPopup.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include "../features/profiles/services/ProfileImageCache.hpp"
#include "../core/Settings.hpp"
#include "../core/RuntimeLifecycle.hpp"
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
#include "../utils/CommentBgHider.hpp"
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
#include "../features/global-icon/GlobalIconRender.hpp"
#include "../features/global-icon/services/GlobalIconService.hpp"
#include "../features/profiles/services/OwnProfileStats.hpp"

using namespace geode::prelude;

// CCScale9Sprite::create crashes if the sprite doesn't exist (it doesn't return
// nullptr). Use paimon::SpriteHelper::safeCreateScale9() from the shared header.

// profileimg texture cache for instant loading between popups. Uses Ref<> for
// automatic refcount with a shutdown guard to avoid release() when CCPoolManager
// is already destroyed.
// (Implementation moved to features/profiles/services/ProfileImageCache.cpp)


// Clear the profileimg cache during game shutdown. Static destructors run in
// an undefined order and CCPoolManager may already be dead, so use take() to
// extract the Ref<> without calling release().






    // skip MP4 video files (handled by ensureAnimatedProfileImg)
    // ftyp box can start at different offsets, search first 12 bytes



class $modify(PaimonProfilePage, ProfilePage) {
    static void onModify(auto& self) {
        // Depends on stable node IDs
        paimon::hooks::afterNodeIdsOrLate(self, "ProfilePage::loadPageFromUserInfo");
    }

    enum class ProfileBackdropKind : uint8_t {
        None,
        Media,
        Gradient,
    };

    struct Fields {
        Ref<CCMenuItemSpriteExtra> m_gearBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_addModBtn = nullptr;       // add-moderator button (admins only)
        Ref<CCMenuItemSpriteExtra> m_banBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_musicPauseBtn = nullptr;  // pause-music button
        Ref<CCClippingNode> m_profileImgClip = nullptr;   // profile image clip
        Ref<CCNode> m_profileImgBorder = nullptr;          // profile image border
        bool m_isApprovedMod = false;
        bool m_isAdmin = false;
        bool m_musicPlaying = false;  // music state
        bool m_menuMusicPaused = false; // menu music paused
        // music fade state
        int m_fadeStep = 0;
        int m_fadeTotalSteps = 0;
        float m_fadeFromVol = 0.0f;
        float m_fadeToVol = 0.0f;
        bool m_hasProfileBackdrop = false;
        ProfileBackdropKind m_backdropKind = ProfileBackdropKind::None;
        bool m_leaveForClose = false;
        bool m_pausedForTemporaryExit = false;
        bool m_audioCleanedUp = false;
        // WeakRef<>: GD's statsMenu can be rebuilt by other mods, leaving this
        // label dangling. WeakRef lets us check with lock().
        WeakRef<CCLabelBMFont> m_thumbCountLabel;
        int64_t m_statusLastSeen = 0;
        bool m_statusOnline = false;

        // PERF: cache the username-menu to avoid getChildByIDRecursive on hot
        // paths. The profile page does ~9 lookups of the same node during init +
        // button clicks, each a full-tree DFS (~0.1-0.5ms on large trees). WeakRef
        // invalidates only when the node disappears (e.g. a layout reset by another mod).
        WeakRef<CCMenu> m_usernameMenuCached = nullptr;
    };

    // PERF: caches the username-menu via WeakRef. The first call does
    // getChildByIDRecursive and stores it; later calls are refcell lookups (~5ns
    // vs ~100us for a recursive tree DFS). If the node was removed (another mod
    // changes layout, scene reload), WeakRef::lock returns null and we search
    // again. typeinfo_cast guards against the ID being taken by a node of another type.
    CCMenu* getUsernameMenu() {
        if (auto cached = m_fields->m_usernameMenuCached.lock()) {
            // Verify it's still in the ProfilePage tree; if another mod moved it
            // out, invalidate and re-search.
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
        return m_fields->m_isApprovedMod || m_fields->m_isAdmin;
    }

    // Get the left-menu safely
    CCMenu* getLeftMenu() {
        if (!this->m_mainLayer) return nullptr;
        auto node = this->m_mainLayer->getChildByID("left-menu");
        return node ? typeinfo_cast<CCMenu*>(node) : nullptr;
    }

    // Get the socials-menu safely
    CCMenu* getSocialsMenu() {
        if (!this->m_mainLayer) return nullptr;
        auto node = this->m_mainLayer->getChildByID("socials-menu");
        return node ? typeinfo_cast<CCMenu*>(node) : nullptr;
    }

    static void scaleToFit(CCNode* spr, float targetSize) {
        if (!spr) return;
        float curSize = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (curSize > 0) spr->setScale(targetSize / curSize);
    }

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

    // Create the add-moderator button if it doesn't exist
    void ensureAddModeratorButton(CCMenu* menu) {        if (!menu || m_fields->m_addModBtn) return;
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

    // Create the reviews button if it doesn't exist (same pattern as
    // ensureGearButton). Returns true if it created it, so the integrity checker
    // knows to redo the layout.
    bool ensureReviewsButton(CCMenu* menu) {
        if (!menu) return false;
        // Dedupe across the WHOLE ProfilePage tree, not just `menu`. The profile
        // redesign relocates this button into its own bottom row; a local-only
        // check would think it's missing and the integrity checker would keep
        // recreating it, piling up duplicate buttons.
        if (this->getChildByIDRecursive("profile-reviews-btn"_spr)) return false;
        auto reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_chatBtn_001.png");
        if (!reviewIcon) reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
        if (!reviewIcon) return false;
        scaleToFit(reviewIcon, 26.f);
        auto reviewBtn = CCMenuItemSpriteExtra::create(reviewIcon, this, menu_selector(PaimonProfilePage::onProfileReviews));
        reviewBtn->setID("profile-reviews-btn"_spr);
        menu->addChild(reviewBtn);
        return true;
    }

    // Create the ban button (hidden by default) and store it in Fields.
    // Visibility is handled by refreshBanButtonVisibility at each call site.
    void createBanButtonInto(CCMenu* menu) {
        if (!menu) return;
        auto banSpr = ButtonSprite::create("X", 40, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
        banSpr->setScale(0.5f);
        auto banBtn = CCMenuItemSpriteExtra::create(banSpr, this, menu_selector(PaimonProfilePage::onBanUser));
        banBtn->setID("ban-user-button"_spr);
        banBtn->setVisible(false);
        menu->addChild(banBtn);
        m_fields->m_banBtn = banBtn;
    }

    // Periodic button-integrity checker
    void verifyButtonIntegrity(float dt) {
        if (!this->getParent()) return;
        if (!this->m_mainLayer) return;
        auto* leftMenu = getLeftMenu();
        if (!leftMenu) return;

        bool needsLayout = false;

        // 1. Ban button: visibility by rank
        if (!m_fields->m_banBtn || !m_fields->m_banBtn->getParent()) {
            // Recreate the ban button if it was lost
            createBanButtonInto(leftMenu);
            needsLayout = true;
            log::debug("[ProfilePage] Boton de ban recreado por verificador de integridad");
        }

        // Update ban visibility
        {
            bool shouldShow = !this->m_ownProfile && (m_fields->m_isApprovedMod || m_fields->m_isAdmin);
            if (m_fields->m_banBtn->isVisible() != shouldShow) {
                m_fields->m_banBtn->setVisible(shouldShow);
                m_fields->m_banBtn->setEnabled(shouldShow);
                needsLayout = true;
            }
        }

        // 2. Reviews button
        if (ensureReviewsButton(leftMenu)) {
            needsLayout = true;
            log::debug("[ProfilePage] Boton de reviews recreado por verificador de integridad");
        }

        // 3. Gear button
        if (this->m_ownProfile && (m_fields->m_isApprovedMod || m_fields->m_isAdmin)) {
            if (!m_fields->m_gearBtn || !m_fields->m_gearBtn->getParent()) {
                m_fields->m_gearBtn = nullptr;
                ensureGearButton(leftMenu);
                needsLayout = true;
                log::debug("[ProfilePage] Boton gear recreado por verificador de integridad");
            }
        }

        // 4. Add-moderator button
        if (this->m_ownProfile && m_fields->m_isAdmin) {
            if (!m_fields->m_addModBtn || !m_fields->m_addModBtn->getParent()) {
                m_fields->m_addModBtn = nullptr;
                ensureAddModeratorButton(leftMenu);
                needsLayout = true;
                log::debug("[ProfilePage] Boton add-mod recreado por verificador de integridad");
            }
        }

        // 5. Keep the status dot at the end of the username-menu (right of the name)
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

    // Moderator/admin badge on the profile

    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    void addRoleBadgesToProfile(paimon::roles::UserRoles const& roles) {
        auto menu = getUsernameMenu();
        if (!menu) return;
        paimon::badges::applyRoleBadges(
            menu, roles, this,
            menu_selector(PaimonProfilePage::onPaimonBadge),
            20.0f, nullptr
        );
    }

    void addModeratorBadge(bool isMod, bool isAdmin) {
        auto menu = getUsernameMenu();
        if (!menu) return;

        // Avoid duplicates
        if (this->getChildByIDRecursive("paimon-moderator-badge"_spr)) return;
        if (this->getChildByIDRecursive("paimon-admin-badge"_spr)) return;

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

        // Avoid duplicates
        if (this->getChildByIDRecursive("paimon-user-status-dot"_spr)) return;

        // Circle 20% smaller, on the same row as the name/badges.
        constexpr float dotRadius = 4.0f;
        auto* dotNode = PaimonDrawNode::create();
        if (!dotNode) return;

        ccColor4F fillColor;
        if (online) {
            fillColor = ccc4f(0.0f, 1.0f, 0.0f, 1.0f); // bright green
        } else {
            fillColor = ccc4f(0.5f, 0.5f, 0.5f, 1.0f); // gray
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
        PopupManager::get().alert("User Status", message).showInstant();
    }

    void fetchAndShowUserStatus(int accountID) {
        Ref<ProfilePage> self = this;
        // Instant presence: send heartbeat first so the server marks us online,
        // then fetch the viewed user's status.
        paimon::forum::ForumApi::get().sendHeartbeat([self, accountID](paimon::forum::Result<bool> hbResult) {
            paimon::forum::ForumApi::get().getUserStatus(accountID,
                [self, accountID](paimon::forum::Result<paimon::forum::UserStatus> result) {
                    Loader::get()->queueInMainThread([self, accountID, result]() {
                        if (paimon::isRuntimeShuttingDown()) return;
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

        // Avoid duplicates (tree-wide: the redesign relocates this badge)
        if (this->getChildByIDRecursive("paimon-custom-badge"_spr)) return;

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
                        if (paimon::isRuntimeShuttingDown()) return;
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
                    // Retain the texture across the deferred task: the RAM cache may evict
                    // and free it before this runs, leaving `tex` dangling (crash inside
                    // CCSprite::initWithTexture on the freed vtable).
                    geode::Ref<cocos2d::CCTexture2D> texRef = tex;
                    Loader::get()->queueInMainThread([self, texRef, targetHeight]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        if (!self->getParent()) return;
                        auto menu = self->getUsernameMenu();
                        if (!menu) return;
                        if (menu->getChildByID("paimon-custom-badge"_spr)) return;
                        auto* spr = CCSprite::createWithTexture(texRef.data());
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

        // Avoid duplicates across the whole tree: the redesign relocates this
        // badge into its bottom row, so a stats-menu-only check would recreate it.
        if (this->getChildByIDRecursive("paimon-thumb-count-btn"_spr)) return;

        // Only show if user has at least 1 approved thumbnail
        if (uploadCount <= 0) return;

        auto* statsMenuCC = typeinfo_cast<CCMenu*>(statsMenu);
        if (!statsMenuCC) return;

        // Icon sprite: use the mod's thumbnail icon or a GD frame
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
        auto* thumbBtn = CCMenuItemSpriteExtra::create(            combinedSprite,
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
        // direct access to GJUserScore's m_userName field
        if (this->m_score && !this->m_score->m_userName.empty()) {
            return this->m_score->m_userName;
        }
        // fallback: read from labels if m_score isn't available yet
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

        // never show on your own profile
        if (this->m_ownProfile) {
            m_fields->m_banBtn->setVisible(false);
            m_fields->m_banBtn->setEnabled(false);
            return;
        }

        bool show = canShowModerationControls();
        m_fields->m_banBtn->setVisible(show);
        m_fields->m_banBtn->setEnabled(show);

        // if visible, also disable it when the profile is mod/admin;
        // use /api/moderators to stay consistent
        auto targetName = getViewedUsername();
        if (show && !targetName.empty()) {
            auto targetLower = geode::utils::string::toLower(targetName);
            Ref<ProfilePage> self = this;
            int currentAccount = this->m_accountID;
            HttpClient::get().get("/api/moderators", [self, targetLower, currentAccount](bool ok, std::string const& resp) {
                if (!ok) return;
                // check we're still alive and on the same profile
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
                        // already on the main thread
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

    // Soft dark overlay shared by the profile-img variants (static / GIF / video
    // / gradient). Identical except the container. Subtly darkens for
    // username/label legibility, like ProfileImgPopup (alpha 60).
    static void addProfileImgDarkOverlay(CCNode* clip, CCSize const& imgArea) {
        if (!clip) return;
        auto overlay = CCLayerColor::create(ccc4(0, 0, 0, 60));
        if (!overlay) return;
        overlay->setContentSize(imgArea);
        overlay->setPosition(ccp(0, 0));
        overlay->setAnchorPoint(ccp(0, 0));
        clip->addChild(overlay);
    }

    // Creates the clipped CCClippingNode shared by all profile-background
    // variants (image / gif / video / gradient). `rounded` chooses rounded
    // corners (static image / gradient) vs square (gif / video). The caller adds
    // content before mounting.
    static CCClippingNode* makeProfileBackdropClip(CCSize const& imgArea, CCPoint const& popupCenter, bool rounded) {
        auto stencil = rounded
            ? paimon::SpriteHelper::createRoundedRectStencil(imgArea.width, imgArea.height)
            : paimon::SpriteHelper::createRectStencil(imgArea.width, imgArea.height);
        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);
        return clip;
    }

    // Mounts an already-populated clip as the profile background: dark overlay,
    // z-order, Fields state, and re-scheduling the style tick. Shared by the
    // image / gif / video / gradient variants.
    void mountProfileBackdropClip(CCClippingNode* clip, CCSize const& imgArea, CCNode* layer, ProfileBackdropKind kind) {
        addProfileImgDarkOverlay(clip, imgArea);
        layer->addChild(clip, paimon::settings::profiles::profileImgZLayer());
        auto f = m_fields.self();
        f->m_profileImgClip = clip;
        f->m_hasProfileBackdrop = true;
        f->m_backdropKind = kind;
        styleProfileInternalBgs(layer);
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 1.5f);
    }

    // Clears the profile-background clip: stops any playing video before removing
    // it (avoids stuck playback/audio when switching backgrounds) and resets the
    // Ref<>. Shared by the video / gradient / addOrUpdate / clearProfileBgVisual variants.
    void clearProfileImgClip() {
        auto f = m_fields.self();
        if (!f->m_profileImgClip) return;
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
        auto layout = getProfileBackdropLayout();
        CCSize imgArea = layout.imgArea;
        CCPoint popupCenter = layout.popupCenter;

        auto clip = makeProfileBackdropClip(imgArea, popupCenter, /*rounded=*/false);

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

        mountProfileBackdropClip(clip, imgArea, layer, ProfileBackdropKind::Media);
    }

    void displayProfileImgVideo(std::string const& videoKey) {
        clearProfileImgClip();

        auto video = VideoThumbnailSprite::createFromCache(videoKey);
        if (!video) return;

        auto layer = this->m_mainLayer;
        if (!layer) return;
        auto layout = getProfileBackdropLayout();
        CCSize imgArea = layout.imgArea;
        CCPoint popupCenter = layout.popupCenter;

        auto clip = makeProfileBackdropClip(imgArea, popupCenter, /*rounded=*/false);

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

        mountProfileBackdropClip(clip, imgArea, layer, ProfileBackdropKind::Media);

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
    bool hasLocalAnimatedProfileMedia(int accountID) const {
        auto videoKey       = fmt::format("profileimg_video_{}", accountID);
        auto gifKey         = getProfileImgGifCacheKey(accountID);
        if (VideoThumbnailSprite::isCached(videoKey))       return true;
        // NOTA: NO consultar "profile_video_{id}" aqui. Esa clave es el video del
        // BANNER del perfil (cacheado por el leaderboard / score cells), no el
        // backdrop del perfil. Tratarla como "media local del perfil" hacia que
        // el banner se confundiera con el fondo del perfil al volver desde el
        // leaderboard.
        if (!gifKey.empty() && AnimatedGIFSprite::isCached(gifKey)) return true;
        return false;
    }

    // Includes a static image in RAM/disk besides GIF/video. Without this, a
    // cached server config of "icon-gradient" or "none" (from an earlier mode)
    // wipes the background a few ms after painting the local image.
    bool hasLocalProfileBackgroundAsset(int accountID) const {
        if (hasLocalAnimatedProfileMedia(accountID)) return true;
        if (getProfileImgCachedTexture(accountID)) return true;
        std::error_code ec;
        auto path = getProfileImgCachePath(accountID);
        if (!std::filesystem::exists(path, ec)) return false;
        auto const fileSize = std::filesystem::file_size(path, ec);
        return !ec && fileSize > 0;
    }

    // After uploading image/GIF/video, the server may still return the old
    // backgroundType (e.g. icon-gradient). Update the config.
    struct ProfileBackdropLayout {
        cocos2d::CCSize imgArea;
        cocos2d::CCPoint popupCenter;
    };

    ProfileBackdropLayout getProfileBackdropLayout() const {
        ProfileBackdropLayout layout;
        auto layer = this->m_mainLayer;
        if (!layer) return layout;

        auto layerSize = layer->getContentSize();
        cocos2d::CCSize popupSize = cocos2d::CCSize(440.f, 290.f);
        cocos2d::CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        auto popupGeo = paimon::compat::InfoLayerLocator::findPopupGeometry(layer);
        if (popupGeo.found) {
            popupSize = popupGeo.size;
            popupCenter = popupGeo.center;
        }

        float padding = 3.f;
        layout.imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);
        layout.popupCenter = popupCenter;
        return layout;
    }

    // Prevents async callbacks (config bundle / downloadProfileConfig) from
    // overwriting or clearing an already-shown image/video background.
    bool shouldBlockServerBackdropOverride(int accountID) {
        if (m_fields->m_backdropKind == ProfileBackdropKind::Media) {
            return true;
        }
        return hasLocalProfileBackgroundAsset(accountID);
    }

    bool tryShowCachedProfileMedia(int accountID) {
        if (accountID <= 0) return false;
        if (ensureAnimatedProfileImg(accountID)) {
            return true;
        }
        if (auto* cachedTex = getProfileImgCachedTexture(accountID)) {
            displayProfileImg(accountID, cachedTex);
            return true;
        }
        if (auto* diskTex = loadProfileImgFromDisk(accountID)) {
            cacheProfileImgTexture(accountID, diskTex);
            displayProfileImg(accountID, diskTex);
            return true;
        }
        return false;
    }

    // ProfilePage::loadPageFromUserInfo rebuilds the layer's children; the custom
    // background clip often loses its parent (flash: appears for an instant and disappears).
    void refreshProfileBackdropAfterVanillaLayout() {
        if (!this->getParent() || !this->m_mainLayer) return;
        if (this->m_accountID <= 0) return;

        auto f = m_fields.self();
        auto const accountID = this->m_accountID;

        if (f->m_profileImgClip && f->m_profileImgClip->getParent()) {
            auto layout = getProfileBackdropLayout();
            f->m_profileImgClip->setPosition(layout.popupCenter);
            f->m_profileImgClip->setContentSize(layout.imgArea);
            if (m_fields->m_hasProfileBackdrop) {
                styleProfileInternalBgs(this->m_mainLayer);
            }
            return;
        }

        auto cachedCfg = ProfileThumbs::get().getProfileConfig(accountID);
        if (cachedCfg.hasConfig && cachedCfg.backgroundType == "none" &&
            !hasLocalAnimatedProfileMedia(accountID) &&
            !hasLocalProfileBackgroundAsset(accountID)) {
            return;
        }
        if (cachedCfg.hasConfig && cachedCfg.backgroundType == "icon-gradient" &&
            !hasLocalProfileBackgroundAsset(accountID)) {
            cocos2d::ccColor3B liveA, liveB;
            if (getLiveProfileColors(liveA, liveB)) {
                displayProfileBgGradient(
                    liveA, liveB,
                    cachedCfg.gradientEffect, cachedCfg.gradientSpeed
                );
            }
            return;
        }

        // Only re-show media if the config already confirmed this user uses
        // image/video. Without a cached config, don't show, to avoid the
        // ScoreCell thumbnail appearing as a backdrop.
        if (f->m_backdropKind == ProfileBackdropKind::Media ||
            (hasLocalProfileBackgroundAsset(accountID) && cachedCfg.hasConfig)) {
            tryShowCachedProfileMedia(accountID);
        }
    }

    void syncConfigForUploadedMediaBackground(int accountID) {
        if (accountID <= 0) return;
        ThumbnailAPI::get().downloadProfileConfig(accountID,
            [accountID](bool ok, ProfileConfig const& existing) {
                ProfileConfig cfg = ok ? existing : ProfileConfig{};
                cfg.hasConfig = true;
                if (cfg.backgroundType != "icon-gradient" && cfg.backgroundType != "none") {
                    ProfileThumbs::get().cacheProfileConfig(accountID, cfg);
                    return;
                }
                cfg.backgroundType = "gradient";
                cfg.useGradient = false;
                ThumbnailAPI::get().uploadProfileConfig(accountID, cfg,
                    [accountID, cfg](bool success, std::string const&) {
                        if (success) {
                            ProfileThumbs::get().cacheProfileConfig(accountID, cfg);
                        }
                    });
            });
    }

    bool ensureAnimatedProfileImg(int accountID) {
        auto gifKey = getProfileImgGifCacheKey(accountID);
        auto videoKey = fmt::format("profileimg_video_{}", accountID);

        // check for cached video first (use correct video key)
        if (VideoThumbnailSprite::isCached(videoKey)) {
            displayProfileImgVideo(videoKey);
            return true;
        }

        // NOTA: NO consultar la clave legacy "profile_video_{id}". Esa clave es
        // ahora propiedad EXCLUSIVA del banner del perfil (fondo descargado por
        // GJScoreCell / LeaderboardsLayer via processProfileBackgroundBytes).
        // Compartir la clave hacia que el backdrop del perfil mostrara el banner
        // del leaderboard al volver al perfil. Si el backdrop es un video,
        // se reconstruye abajo desde el disco (profileimg_cache/{id}.dat) bajo la
        // clave correcta "profileimg_video_{id}".

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

        // Placeholder: static first frame while the async GIF finishes decoding.
        // maxFrames=1 — decoding the whole GIF here runs on the main thread and
        // stalled the frame when opening profiles with large animated backdrops.
        auto gifResult = GIFDecoder::decode(bytes->data(), bytes->size(), 1);
        if (!gifResult.frames.empty()) {
            auto& firstFrame = gifResult.frames[0];
            if (firstFrame.width > 0 && firstFrame.height > 0) {
                auto* tex = new CCTexture2D();
                if (tex->initWithData(
                        firstFrame.pixels.data(),
                        kCCTexture2DPixelFormat_RGBA8888,
                        firstFrame.width,
                        firstFrame.height,
                        CCSize(static_cast<float>(firstFrame.width), static_cast<float>(firstFrame.height)))) {
                    tex->autorelease();
                    cacheProfileImgTexture(accountID, tex);
                    displayProfileImg(accountID, tex);
                } else {
                    tex->release();
                }
            }
        }

        Ref<ProfilePage> safeRef = this;
        AnimatedGIFSprite::createAsync(*bytes, gifKey, [safeRef, accountID, gifKey](AnimatedGIFSprite* sprite) {
            if (!sprite) return;
            Loader::get()->queueInMainThread([safeRef, accountID, gifKey]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (!safeRef || !safeRef->getParent()) return;
                auto* page = static_cast<PaimonProfilePage*>(safeRef.data());
                if (!page || page->m_accountID != accountID) return;
                page->displayProfileImgGif(gifKey);
            });
        });

        return false;
    }

    void addOrUpdateProfileImgOnPage(int accountID, bool isSelf = false) {
        auto f = m_fields.self();
        f->m_hasProfileBackdrop = false;
        f->m_backdropKind = ProfileBackdropKind::None;
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));

        // clear previous - stop video first to prevent stale playback
        clearProfileImgClip();
        if (f->m_profileImgBorder) { f->m_profileImgBorder->removeFromParent(); f->m_profileImgBorder = nullptr; }

        // If the cached config says the user chose the icon gradient, paint it
        // directly and skip downloading the image/GIF/video banner; no point
        // downloading what won't show.
        //
        // Bug-2 fix: the cached config can say "icon-gradient" while the user
        // already has a local profile video/GIF (uploaded on another device, or
        // before the config re-synced from the server). Taking the gradient
        // shortcut without checking the local cache makes the just-configured
        // video show ONCE (on upload) then never again, because the cached server
        // config blocks it. So check the local cache first and, if a video/GIF
        // exists, show it: local media is the freshest source of truth.
        {
            auto cachedCfg = ProfileThumbs::get().getProfileConfig(accountID);
            bool hasLocalAnimatedMedia = hasLocalAnimatedProfileMedia(accountID);
            bool hasLocalBgAsset = hasLocalProfileBackgroundAsset(accountID);

            if (cachedCfg.hasConfig && cachedCfg.backgroundType == "icon-gradient" &&
                !hasLocalBgAsset) {
                // LIVE colors: read from the current player (GameManager for own
                // profile, GJUserScore for others) each render. If player data
                // isn't here yet (m_score hasn't arrived), paint nothing and let
                // getUserInfoFinished trigger a repaint later with the correct colors.
                cocos2d::ccColor3B liveA, liveB;
                if (getLiveProfileColors(liveA, liveB)) {
                    this->displayProfileBgGradient(
                        liveA, liveB,
                        cachedCfg.gradientEffect, cachedCfg.gradientSpeed
                    );
                }
                // Without live data we don't render: the config no longer stores
                // a snapshot; colors come ONLY from local.
                return;
            }
            if (cachedCfg.hasConfig && cachedCfg.backgroundType == "none" &&
                !hasLocalAnimatedMedia) {
                // The user reset their background: paint nothing extra.
                return;
            }
            // Special case: config says icon-gradient/none but local media
            // exists. Fall through to the normal flow below, which calls
            // ensureAnimatedProfileImg() and shows the local video/GIF.
        }

        // Only show cached media immediately if the config is already available
        // (hasConfig == true and not "none"/"icon-gradient" — those were handled
        // above). If the config isn't cached, show nothing now: downloadProfileConfig
        // decides when it arrives, avoiding a flash where the ScoreCell thumbnail
        // appears as a backdrop.
        auto cachedCfgForMedia = ProfileThumbs::get().getProfileConfig(accountID);
        bool configAllowsMedia = cachedCfgForMedia.hasConfig &&
            cachedCfgForMedia.backgroundType != "none" &&
            cachedCfgForMedia.backgroundType != "icon-gradient";

        bool queuedAnimated = false;
        if (configAllowsMedia || hasLocalProfileBackgroundAsset(accountID)) {
            queuedAnimated = ensureAnimatedProfileImg(accountID);

            if (!queuedAnimated) {
                CCTexture2D* cachedTex = getProfileImgCachedTexture(accountID);
                if (cachedTex) {
                    this->displayProfileImg(accountID, cachedTex);
                } else {
                    if (auto* diskTex = loadProfileImgFromDisk(accountID)) {
                        cacheProfileImgTexture(accountID, diskTex);
                        this->displayProfileImg(accountID, diskTex);
                    }
                }
            }
        }

        // Ensure we have the config (background type) for when the user chose
        // icon-gradient but it isn't cached yet. If the downloaded config shows
        // icon-gradient, repaint the banner as a gradient and clear any previous image.
        WeakRef<PaimonProfilePage> cfgSelf = this;
        ThumbnailAPI::get().downloadProfileConfig(accountID,
            [cfgSelf, accountID](bool ok, ProfileConfig const& cfg) {
                if (!ok || !cfg.hasConfig) return;
                ProfileThumbs::get().cacheProfileConfig(accountID, cfg);
                if (cfg.backgroundType == "icon-gradient") {
                    Loader::get()->queueInMainThread([cfgSelf, accountID, cfg]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto page = cfgSelf.lock();
                        if (!page) return;
                        if (page->m_accountID != accountID) return;
                        // Bug-2 fix: do not paint over a locally-configured
                        // video/GIF. The server config can lag behind the
                        // local media (different device, recent upload, etc.).
                        auto* selfPaimon = static_cast<PaimonProfilePage*>(page.data());
                        if (selfPaimon && selfPaimon->shouldBlockServerBackdropOverride(accountID)) {
                            return;
                        }
                        // LIVE colors: see the comment in addOrUpdateProfileImgOnPage.
                        // If there's no live data yet, paint nothing:
                        // getUserInfoFinished handles it when the GJUserScore arrives.
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
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto page = cfgSelf.lock();
                        if (!page) return;
                        if (page->m_accountID != accountID) return;
                        auto* selfPaimon = static_cast<PaimonProfilePage*>(page.data());
                        if (selfPaimon && selfPaimon->shouldBlockServerBackdropOverride(accountID)) {
                            return;
                        }
                        page->clearProfileBgVisual();
                    });
                } else {
                    // Config says it should show an image (thumbnail/gradient/etc).
                    // If downloadProfileImg already cached the texture but didn't
                    // show it (config wasn't ready), show it now.
                    Loader::get()->queueInMainThread([cfgSelf, accountID]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto page = cfgSelf.lock();
                        if (!page) return;
                        if (page->m_accountID != accountID) return;
                        auto* selfPaimon = static_cast<PaimonProfilePage*>(page.data());
                        if (!selfPaimon) return;
                        if (selfPaimon->m_fields->m_hasProfileBackdrop) return;
                        selfPaimon->tryShowCachedProfileMedia(accountID);
                    });
                }
            });

        // download from the server in the background (update cache)
        // Ref<> keeps the ProfilePage alive until the callback finishes
        Ref<ProfilePage> self = this;
        ThumbnailAPI::get().downloadProfileImg(accountID, [self, accountID](bool success, CCTexture2D* texture) {
            if (!self || !self->getParent()) return;

            if (success) {
                // Ref<> retains on assignment and releases the previous one automatically
                auto* page = static_cast<PaimonProfilePage*>(self.data());
                if (!page) return;
                if (self->m_accountID != accountID) return;

                // If a config with a non-image backgroundType (icon-gradient /
                // none) was cached meanwhile, respect it and don't paint the image.
                auto cfg = ProfileThumbs::get().getProfileConfig(accountID);
                if (cfg.hasConfig && cfg.backgroundType == "icon-gradient" &&
                    !page->shouldBlockServerBackdropOverride(accountID)) {
                    return;
                }
                if (cfg.hasConfig && cfg.backgroundType == "none" &&
                    !page->shouldBlockServerBackdropOverride(accountID)) {
                    return;
                }

                if (texture) {
                    cacheProfileImgTexture(accountID, texture);
                }

                // If the config isn't cached yet, don't show the image as a
                // backdrop yet; downloadProfileConfig decides when it arrives.
                // Avoids a flash where the ScoreCell image briefly appears as the
                // profile background.
                if (!cfg.hasConfig && !page->shouldBlockServerBackdropOverride(accountID)) {
                    return;
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

    // Tint an entire CCScale9Sprite (center + borders)
    static void tintScale9(CCScale9Sprite* s9, ccColor3B const& color, GLubyte opacity) {
        if (!s9) return;

        // Enable cascade for children
        s9->setCascadeColorEnabled(true);
        s9->setCascadeOpacityEnabled(true);
        s9->setColor(color);
        s9->setOpacity(opacity);

        // Tint direct children of the inner batch node
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

    // Hide decorative backgrounds and add a dark panel
    void styleProfileInternalBgs(CCNode* root) {
        if (!root) return;

        auto walk = [&](auto const& self, CCNode* parent) -> void {
            if (!parent) return;
            auto* children = parent->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                // Hide the decorative icon-background so the profile backdrop
                // shows through behind the icon row.
                if (child->getID() == "icon-background") {
                    child->setVisible(false);
                }

                // Hide decorative border
                if (child->getID() == "alphalaneous.happy_textures/special-border") {
                    child->setVisible(false);
                }

                // GJCommentListLayer: opacity 0, hide borders
                if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(child)) {
                    commentList->setOpacity(0);

                    auto* listChildren = commentList->getChildren();
                    if (listChildren) {
                        for (auto* lc : CCArrayExt<CCNode*>(listChildren)) {
                            if (!lc) continue;
                            auto id = lc->getID();
                            // Borders with a node ID
                            if (id == "left-border" || id == "right-border" ||
                                id == "top-border" || id == "bottom-border") {
                                lc->setVisible(false);
                            }
                        }
                    }

                    // Hide CommentCells' internal backgrounds
                    hideCommentCellBgs(commentList);
                }

                self(self, child);
            }
        };

        walk(walk, root);
    }

    void hideCommentCellBgs(CCNode* listNode) {
        paimon::commentbg::hideCommentCellBgs(listNode);
    }

    // Hook: GD finished loading user info
    $override
    void getUserInfoFinished(GJUserScore* score) {
        ProfilePage::getUserInfoFinished(score);

        refreshProfileBackdropAfterVanillaLayout();

        if (m_fields->m_hasProfileBackdrop) {
            if (auto* layer = this->m_mainLayer) {
                styleProfileInternalBgs(layer);
            }
        }

        // Now that we have the GJUserScore, if the profile chose icon-gradient,
        // repaint with the player's LIVE colors.
        if (this->m_accountID > 0 && !shouldBlockServerBackdropOverride(this->m_accountID)) {
            auto cachedCfg = ProfileThumbs::get().getProfileConfig(this->m_accountID);
            if (cachedCfg.hasConfig && cachedCfg.backgroundType == "icon-gradient") {
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
        // don't rate your own profile
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

    // Clear paimon buttons to avoid duplicates
    void cleanPaimonButtons(CCMenu* menu) {
        if (!menu) return;
        // These get recreated below in loadPageFromUserInfo AND are relocated by
        // the profile redesign into its own rails (rd-options-rail / rd-bottom-row).
        // Remove them across the WHOLE ProfilePage tree (not just `menu`): a
        // local-only check leaves the relocated copy in place, so on reload the
        // ensure*/create code makes a fresh copy and the two stack up — the
        // visible duplicate buttons after pressing the refresh button.
        static std::string const relocatableIDs[] = {
            "profile-reviews-btn"_spr,
            "ban-user-button"_spr,
            "thumbs-gear-button"_spr,
            "add-moderator-button"_spr,
        };
        for (auto const& id : relocatableIDs) {
            while (auto* btn = this->getChildByIDRecursive(id)) {
                btn->removeFromParent();
            }
        }
        // fav-creator-btn is created/managed elsewhere and is not relocated by the
        // redesign; only clear it from the local menu to avoid removing a button
        // that this path would not recreate.
        while (auto* btn = menu->getChildByID("fav-creator-btn"_spr)) {
            btn->removeFromParent();
        }
        m_fields->m_gearBtn = nullptr;
        m_fields->m_banBtn = nullptr;
        m_fields->m_addModBtn = nullptr;
    }

    void cleanPaimonSocialsButtons(CCMenu* menu) {
        if (!menu) return;
        // Tree-wide removal (see cleanPaimonButtons): these are recreated below and
        // relocated by the redesign, so a local-only check duplicates them on
        // reload. (profile-music-button / add-profileimg-button are legacy ids no
        // longer created; harmless to include.)
        static std::string const paimonSocialIDs[] = {
            "profile-settings-button"_spr,
            "profile-music-button"_spr,
            "add-profileimg-button"_spr,
            "profile-music-pause-button"_spr,
        };
        for (auto const& id : paimonSocialIDs) {
            while (auto* btn = this->getChildByIDRecursive(id)) {
                btn->removeFromParent();
            }
        }
        m_fields->m_musicPauseBtn = nullptr;
    }

    // Get the profile popup's position/size
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

    // Hook: GD builds the profile icon panels
    $override
    void loadPageFromUserInfo(GJUserScore* score) {
        // Own profile: stamp live local stats onto the score BEFORE vanilla
        // builds labels. Without this, disabling profile redesign shows the
        // stale cached GJUserScore (redesign was the only place that used GSM).
        if (this->m_ownProfile && score) {
            paimon::profiles::applyLiveOwnProfileStats(score);
        }

        ProfilePage::loadPageFromUserInfo(score);

        // Defense in depth: if node-ids already built labels from an older score
        // copy, rewrite the visible numbers (no-op when redesign hides stats-menu).
        if (this->m_ownProfile && score && this->m_mainLayer) {
            paimon::profiles::refreshVanillaStatsLabels(this->m_mainLayer, score);
        }

        // Global Icons: if this user shares a custom icon, replace the profile's
        // SimplePlayer with theirs (no-op if the feature doesn't apply).
        if (this->m_mainLayer) {
            int gAccountID = score ? score->m_accountID : this->m_accountID;
            paimon::globalicon::renderProfileCube(this->m_mainLayer, gAccountID);
        }

        // GD rebuilds the popup layout here; re-attach the custom background.
        refreshProfileBackdropAfterVanillaLayout();

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

        auto popCenter = getPopupCenter();
        auto popSize = getPopupSize();

        auto leftMenuNode = this->m_mainLayer->getChildByID("left-menu");
        CCMenu* menu = leftMenuNode ? typeinfo_cast<CCMenu*>(leftMenuNode) : nullptr;

        if (!menu) {
            menu = CCMenu::create();
            menu->setID("left-menu");
            menu->setZOrder(10);
            this->m_mainLayer->addChild(menu);

            // Only position it if we created the menu (fallback)
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

        // Clear previous paimon buttons
        cleanPaimonButtons(menu);

        // Clear the thumbnail-count badge on reload. Use the REAL id
        // ("paimon-thumb-count-btn") and search the WHOLE tree: the redesign
        // relocates this badge into its bottom row, so the old stats-menu-only
        // cleanup (with the wrong icon/text ids) left the relocated copy in place
        // and addThumbnailCountBadge then stacked a fresh duplicate on reload.
        while (auto* btn = this->getChildByIDRecursive("paimon-thumb-count-btn"_spr)) {
            btn->removeFromParent();
        }
        m_fields->m_thumbCountLabel = nullptr;

        ensureReviewsButton(menu);

        if (!this->m_ownProfile) {
            if (auto bottomMenu = this->m_mainLayer->getChildByIDRecursive("bottom-menu")) {
                // Recursive check: the redesign relocates this button, so a
                // local-only check would keep recreating duplicates.
                if (!this->getChildByIDRecursive("rate-profile-btn"_spr)) {
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

        // Ban button (mods/admins)
        createBanButtonInto(menu);
        refreshBanButtonVisibility();

        // Moderation buttons (own profile)
        if (this->m_ownProfile) {
            if (m_fields->m_isApprovedMod || m_fields->m_isAdmin) {
                ensureGearButton(menu);
            }
            if (m_fields->m_isAdmin) {
                ensureAddModeratorButton(menu);
            }
        }

        menu->updateLayout();

        // Buttons in socials-menu
        auto* socialsMenu = getSocialsMenu();
        bool createdSocialsMenu = false;
        if (!socialsMenu) {
            auto newSocialsMenu = CCMenu::create();
            newSocialsMenu->setID("socials-menu");
            newSocialsMenu->setZOrder(10);
            this->m_mainLayer->addChild(newSocialsMenu);
            socialsMenu = newSocialsMenu;
            createdSocialsMenu = true;

            // Only position it if we created the menu (fallback)
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

        // Add buttons after the native ones

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

        // Clear the custom badge from the username-menu on reloads
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

        // Moderator/admin badge
        if (score) {
            std::string badgeUsername = score->m_userName;

            bool isMod = false;
            bool isAdmin = false;
            if (auto cached = paimon::roles::RoleService::get().lookup(badgeUsername)) {
                if (cached->any()) this->addRoleBadgesToProfile(*cached);
            } else if (moderatorCacheGet(badgeUsername, isMod, isAdmin)) {
                if (isMod || isAdmin) {
                    paimon::roles::UserRoles roles;
                    roles.admin = isAdmin;
                    roles.mod = isMod || isAdmin;
                    this->addRoleBadgesToProfile(roles);
                }
            }

            // Authoritative role set for badges (admin/mod/vip/helper/idea).
            {
                Ref<PaimonProfilePage> rbSelf = this;
                std::string capturedUser = badgeUsername;
                int viewedForBadges = this->m_accountID;
                paimon::roles::RoleService::get().fetch(badgeUsername,
                    [rbSelf, capturedUser, viewedForBadges](paimon::roles::UserRoles roles) {
                        if (!roles.any()) return;
                        if (!rbSelf || !rbSelf->getParent()) return;
                        if (rbSelf->m_accountID != viewedForBadges) return;
                        rbSelf->addRoleBadgesToProfile(roles);
                    });
            }

            // Profile Bundle: mod status + badge + stats in 1 request
            // Replaces individual checkUserStatus + fetchBadge + getProfileStats calls
            int viewedAccountID = this->m_accountID;
            Ref<PaimonProfilePage> bundleSelf = this;

            HttpClient::get().downloadProfileBundle(viewedAccountID, badgeUsername,
                [bundleSelf, viewedAccountID, badgeUsername](bool success, std::string const& response) {
                    auto queueMusicFallback = [bundleSelf, viewedAccountID]() {
                        Loader::get()->queueInMainThread([bundleSelf, viewedAccountID]() {
                            if (paimon::isRuntimeShuttingDown()) return;
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
                    bool isVip = json["isVip"].asBool().unwrapOr(false);
                    bool isHelper = json["isHelper"].asBool().unwrapOr(false);
                    bool isIdea = json["isIdea"].asBool().unwrapOr(false);

                    // Update caches
                    moderatorCacheInsert(badgeUsername, isMod, isAdmin);
                    ModerationService::get().updateUserStatusCache(badgeUsername, isMod, isAdmin);
                    {
                        paimon::roles::UserRoles cacheRoles;
                        cacheRoles.admin = isAdmin;
                        cacheRoles.mod = isMod || isAdmin;
                        cacheRoles.vip = isVip;
                        cacheRoles.helper = isHelper;
                        cacheRoles.idea = isIdea;
                        paimon::roles::RoleService::get().update(badgeUsername, cacheRoles);
                    }

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

                    // Profile config from the bundle. The bundle already returns
                    // the full config blob under "config". Cache it in
                    // ProfileThumbs so applyVideoAudioOverride / displayProfileBg
                    // find useVideoAudio without an extra server round-trip.
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
                        // Cache on the main thread to avoid racing the render
                        // that might be reading simultaneously.
                        Loader::get()->queueInMainThread([viewedAccountID, pcfg]() {
                            if (paimon::isRuntimeShuttingDown()) return;
                            ProfileThumbs::get().cacheProfileConfig(viewedAccountID, pcfg);
                        });
                    }

                    // Determine whether this is the local user's own profile.
                    // The profile bundle is served from a Cloudflare edge cache
                    // that is NOT purged when the user changes their profile
                    // music — only the dedicated /api/profile-music endpoint is
                    // invalidated on upload/delete. So right after an upload the
                    // bundle's "music" block can be stale and still describe the
                    // previously configured song (old songID / old updatedAt),
                    // which then drives both playback and the audio download
                    // cache-buster, serving the previous clip. For our own
                    // profile we therefore ignore the bundle music and reconcile
                    // against the authoritative endpoint instead.
                    bool isOwnProfileMusic = false;
                    if (auto* am = GJAccountManager::get()) {
                        isOwnProfileMusic = (am->m_accountID == viewedAccountID);
                    }

                    // Music config from the bundle
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
                        // Only cache/trust the bundle music for other users'
                        // profiles. For our own profile the bundle may be stale,
                        // so we leave bundleMusicConfig empty and force the
                        // authoritative fetch below.
                        if (!isOwnProfileMusic) {
                            ProfileMusicManager::get().injectBundleConfig(viewedAccountID, musicCfg);
                            bundleMusicConfig = musicCfg;
                        }
                    }

                    Loader::get()->queueInMainThread([bundleSelf, viewedAccountID, isMod, isAdmin, isVip, isHelper, isIdea, emoteName, uploadCount, bundleMusicConfig, hasBundleMusicConfig, isOwnProfileMusic]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        if (!bundleSelf || !bundleSelf->getParent()) return;
                        if (bundleSelf->m_accountID != viewedAccountID) return;

                        // Role badges (admin/mod/vip/helper/idea)
                        paimon::roles::UserRoles roles;
                        roles.admin = isAdmin;
                        roles.mod = isMod || isAdmin;
                        roles.vip = isVip;
                        roles.helper = isHelper;
                        roles.idea = isIdea;
                        if (roles.any()) {
                            bundleSelf->addRoleBadgesToProfile(roles);
                        }

                        // Custom emote badge
                        if (!emoteName.empty()) {
                            bundleSelf->addCustomBadgeToProfile(emoteName);
                        }

                        // Upload count badge
                        bundleSelf->addThumbnailCountBadge(uploadCount);

                        if (isOwnProfileMusic) {
                            // Bundle music may be stale for our own profile right
                            // after an upload; force the authoritative fetch so a
                            // freshly changed/custom song is picked up instead of
                            // re-downloading the previously configured one.
                            bundleSelf->checkAndPlayProfileMusic(viewedAccountID, std::nullopt, false, true);
                        } else {
                            bundleSelf->checkAndPlayProfileMusic(viewedAccountID, bundleMusicConfig, hasBundleMusicConfig, !hasBundleMusicConfig);
                        }
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

        auto layout = getProfileBackdropLayout();
        CCSize imgArea = layout.imgArea;
        CCPoint popupCenter = layout.popupCenter;

        auto clip = makeProfileBackdropClip(imgArea, popupCenter, /*rounded=*/true);

        auto imgSprite = CCSprite::createWithTexture(tex);
        if (!imgSprite) return;

        float scaleX = imgArea.width / imgSprite->getContentWidth();
        float scaleY = imgArea.height / imgSprite->getContentHeight();
        imgSprite->setScale(std::max(scaleX, scaleY));
        imgSprite->setAnchorPoint(ccp(0.5f, 0.5f));
        imgSprite->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));
        clip->addChild(imgSprite);

        mountProfileBackdropClip(clip, imgArea, layer, ProfileBackdropKind::Media);
    }

    // Periodically re-apply opacity 0 to icon-background
    void tickStyleBgs(float) {
        if (!this->getParent()) return;
        if (!m_fields->m_hasProfileBackdrop) return;
        if (auto* layer = this->m_mainLayer) {
            styleProfileInternalBgs(layer);
        }
    }

    void refreshProfileBackdropTick(float) {
        this->unschedule(schedule_selector(PaimonProfilePage::refreshProfileBackdropTick));
        refreshProfileBackdropAfterVanillaLayout();
    }

    $override
    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;

            m_fields->m_isApprovedMod = false;
            m_fields->m_isAdmin = false;
            PaimonDebug::log("[ProfilePage] Inicializando perfil - status moderador: false");

            // Saved mod state
            bool wasVerified = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            bool wasAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
            if (wasVerified) {
                m_fields->m_isApprovedMod = true;
                m_fields->m_isAdmin = wasAdmin;
            }

            // Verify with the server if it's your own profile
            if (ownProfile) {
                auto gm = GameManager::get();
                if (gm && !gm->m_playerName.empty()) {
                    std::string username = gm->m_playerName;
                    Ref<ProfilePage> self = this;
                    ThumbnailAPI::get().checkModerator(username, [self](bool isApproved, bool isAdmin) {
                        Loader::get()->queueInMainThread([self, isApproved, isAdmin]() {
                            if (paimon::isRuntimeShuttingDown()) return;
                            if (!self->getParent()) return;
                            auto* page = static_cast<PaimonProfilePage*>(self.data());
                            if (!page) return;
                            bool effectiveMod = isApproved || isAdmin;
                            page->m_fields->m_isApprovedMod = effectiveMod;
                            page->m_fields->m_isAdmin = isAdmin;

                            Mod::get()->setSavedValue("is-verified-moderator", effectiveMod);
                            Mod::get()->setSavedValue("is-verified-admin", isAdmin);

                            if (effectiveMod) {
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

            // Mark profile open to restore the BG on close
            m_fields->m_menuMusicPaused = true;

            checkAndPlayProfileMusic(accountID, std::nullopt, false, false);

            addOrUpdateProfileImgOnPage(accountID, ownProfile);

            // loadPageFromUserInfo / show can detach the clip; short retry.
            this->schedule(schedule_selector(PaimonProfilePage::refreshProfileBackdropTick), 0.12f);

            this->schedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity), 0.5f);

            // Hide vanilla hint labels on next frame
            this->scheduleOnce(
                schedule_selector(PaimonProfilePage::hideHintLabels), 0.f
            );

            // Record a profile view (other profiles only)
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
        if (!m_fields->m_isApprovedMod && !m_fields->m_isAdmin) {
            log::warn("[ProfilePage] Usuario NO es moderador ni admin, bloqueando acceso al centro de verificacion");
            PopupManager::get().alert(Localization::get().getString("profile.access_denied"), Localization::get().getString("profile.moderators_only"), Localization::get().getString("general.ok")).showInstant();
            return;
        }
        
        log::info("[ProfilePage] Abriendo centro de verificacion para moderador");
        auto scene = VerificationCenterLayer::scene();
        if (scene) {
            TransitionManager::get().pushScene(scene);
        }
    }

    void onAddProfileImg(CCObject*) {
        // Only allow editing your own profile's background.
        if (!this->m_ownProfile) {
            PaimonNotify::create(
                Localization::get().getString("profile.cant_edit_other").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }

        // Instead of opening the file picker directly, show a chooser so the user
        // can pick uploading media, using the icon-based gradient, or resetting
        // the background.
        auto* picker = ProfileBgPickerPopup::create(this->m_accountID);
        if (!picker) {
            // Fallback: if the popup couldn't be created for some reason, go
            // straight to the classic upload-media flow.
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

    // Opens the effect + speed sub-popup for the player-icon-based gradient. The
    // initial effect/speed comes from the cached config if present, so the user
    // keeps editing what they had.
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

    // Uploads ProfileConfig with backgroundType = "icon-gradient" + effect. Note:
    // the current icon colors are NOT uploaded. Colors are always read LIVE each
    // render (GameManager for own profile, GJUserScore for others) so cube changes
    // are reflected without re-uploading.
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

        // Show the spinner before downloading/uploading the config so the user
        // knows something is happening.
        auto loading = PaimonLoadingOverlay::create(
            Localization::get().getString("profilebg.gradient.uploading").c_str(),
            30.f
        );
        Ref<PaimonLoadingOverlay> loadingRef = loading;
        if (loading) loading->show(this, 100);

        // Recover the previous config (if any) so we don't lose other fields like
        // blur/darkness/widthFactor/comment-bg, etc. Note we do NOT touch
        // cfg.colorA / cfg.colorB: leave whatever was there (or its defaults).
        // Real colors are computed each render from local.
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
                // colorA / colorB intentionally untouched: the render always uses
                // live colors, not the config's.

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

                        // Re-cache the updated config and drop the image cache so
                        // the render uses the gradient.
                        ProfileThumbs::get().cacheProfileConfig(accountID, cfg);

                        PaimonNotify::create(
                            Localization::get().getString("profilebg.gradient.applied").c_str(),
                            NotificationIcon::Success
                        )->show();

                        if (auto page = self.lock()) {
                            // LIVE colors: the render always reads from the live
                            // player, not the config (which no longer stores a
                            // color snapshot). For your own profile GameManager is
                            // always available, so this practically never fails.
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

    // Enables "Audio Video" mode for your own profile: makes the background
    // video's audio play as profile music for visitors, and clears any configured
    // music from the server. Only makes sense if the current background is a
    // video; otherwise warn the user and do nothing to avoid leaving the config
    // in a weird state.
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

        // Verify we have a cached background video: if the profile has no video,
        // "Audio Video" adds nothing.
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

        // Recover the previous config to preserve the rest of the fields (blur,
        // darkness, widthFactor, comment-bg, etc.). Only change the useVideoAudio
        // flag. Leave backgroundType as is; if the user uploaded a video it's
        // already resolved in the render.
        ThumbnailAPI::get().downloadProfileConfig(accountID,
            [self, accountID, loadingRef]
            (bool, ProfileConfig const& existing) {
                ProfileConfig cfg   = existing;
                cfg.hasConfig       = true;
                cfg.useVideoAudio   = true;

                ThumbnailAPI::get().uploadProfileConfig(accountID, cfg,
                    [self, accountID, cfg, loadingRef](bool success, std::string const& msg) {
                        // Delete configured music in parallel: even if the config
                        // upload fails we want to remove the old music so it
                        // doesn't compete with the video audio. We don't wait on
                        // the callback to show the result; knowing it was
                        // attempted is enough.
                        auto* accountManager = GJAccountManager::get();
                        std::string username = accountManager ? std::string(accountManager->m_username) : std::string();
                        ProfileMusicManager::get().deleteProfileMusic(accountID, username,
                            [accountID](bool delOk, std::string const& delMsg) {
                                if (delOk) {
                                    log::info("[ProfileBg] Cleared configured music after Audio Video for account {}", accountID);
                                } else {
                                    // No-op if there was no configured music.
                                    log::info("[ProfileBg] deleteProfileMusic returned: {}", delMsg);
                                }
                            });
                        // Invalidate the local music cache so the next visit reads
                        // the new config.
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

    // Reset the profile background: clears the server config + local cache.
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

        // Upload a "neutral" config with backgroundType = "none" so visitors see
        // neither thumbnail nor gradient. The binary asset (image/GIF/video) stays
        // on the server but the config wins at render time. Deleting it from the
        // bucket would need a dedicated delete endpoint.
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

    // Returns the current icon colors of the viewed profile's player. For your own
    // profile it uses GameManager (always live). For others it uses the
    // GJUserScore's color1/color2 that GD already loaded. This lets the gradient
    // reflect color changes without re-uploading the config.
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

    // Replaces the profile background banner with a CCLayerGradient painted in the
    // player's icon colors. Keeps the same geometry (clipped to the popup area) as
    // the image version so the rest of the profile styling still works. If
    // effect != "none", animates the gradient with that effect.
    void displayProfileBgGradient(cocos2d::ccColor3B colorA,
                                  cocos2d::ccColor3B colorB,
                                  std::string const& effect = "none",
                                  float speed = 1.0f) {
        // Clear the previous banner (image / gif / video)
        clearProfileImgClip();

        auto layer = this->m_mainLayer;
        if (!layer) return;

        auto layout = getProfileBackdropLayout();
        CCSize imgArea = layout.imgArea;
        CCPoint popupCenter = layout.popupCenter;

        auto clip = makeProfileBackdropClip(imgArea, popupCenter, /*rounded=*/true);

        // Animated gradient in the icon colors with the requested effect.
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

        // Same soft dark overlay as the image flow, to preserve username/label contrast.
        mountProfileBackdropClip(clip, imgArea, layer, ProfileBackdropKind::Gradient);
    }

    void clearProfileBgVisual() {
        clearProfileImgClip();
        auto f = m_fields.self();
        f->m_hasProfileBackdrop = false;
        f->m_backdropKind = ProfileBackdropKind::None;
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

            Ref<ProfilePage> videoUploadRef = this;

            paimon::showBetaUploadWarningIfNeeded([videoUploadRef, accountID, videoData = std::move(videoData), username, loading]() mutable {
                ThumbnailAPI::get().uploadProfileVideo(accountID, videoData, username, [videoUploadRef, accountID, videoData, loading](bool success, std::string const& msg) {
                    if (loading) loading->dismiss();

                    if (success) {
                        PaimonNotify::create("Profile video uploaded!", NotificationIcon::Success)->show();
                        saveProfileImgToDisk(accountID, videoData);
                        ProfileImageService::get().rememberProfileImgGifKey(accountID, fmt::format("profileimg_video_{}", accountID));
                        auto* page = static_cast<PaimonProfilePage*>(videoUploadRef.data());
                        if (!page || !page->getParent() || page->m_accountID != accountID) return;
                        page->syncConfigForUploadedMediaBackground(accountID);
                        page->ensureAnimatedProfileImg(accountID);
                    } else {
                        PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                    }
                });
            });
            return;
        }

        if (ImageLoadHelper::isGIF(path)) {
            // GIF: upload directly
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
                        if (page) page->syncConfigForUploadedMediaBackground(accountID);
                        if (page && page->ensureAnimatedProfileImg(accountID)) {
                            return;
                        }

                        if (paimon::format::isGif(imgData.data(), imgData.size())) {
                            // Only the first frame is used as a placeholder.
                            auto gifResult = GIFDecoder::decode(imgData.data(), imgData.size(), 1);
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

        // Static image: use the helper to load and convert
        auto loaded = ImageLoadHelper::loadStaticImage(path, 25);
        if (!loaded.success) {
            std::string errKey = loaded.error;
            // Translate the error if it's a localization key
            if (errKey == "image_open_error" || errKey == "invalid_image_data" || errKey == "texture_error") {
                PaimonNotify::create(Localization::get().getString("profile." + errKey).c_str(), NotificationIcon::Error)->show();
            } else {
                PaimonNotify::create(errKey.c_str(), NotificationIcon::Error)->show();
            }
            return;
        }

        // loadStaticImage() returns the texture with refcount=1 (the caller owns
        // it). CapturePreviewPopup stores it in a Ref<> (retains/releases its own
        // reference), so we do NOT retain here: we release our reference after
        // creating the popup (see below) to avoid leaking it.
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
                    // Convert RGBA to PNG in memory
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
                    if (auto* uploadPage = static_cast<PaimonProfilePage*>(imgUploadRef.data())) {
                        uploadPage->syncConfigForUploadedMediaBackground(accountID);
                    }

                    CCImage finalImg;
                                if (finalImg.initWithImageData(buf.get(), w * h * 4, CCImage::kFmtRawData, w, h)) {
                                    // Ref::adopt takes ownership of new CCTexture2D()'s refcount=1 without an
                                    // extra retain. Without adopt the Ref retains (refcount=2) and only
                                    // drops to 1 on destruction -- a permanent CCTexture2D leak.
                                    auto finalTex = geode::Ref<CCTexture2D>::adopt(new CCTexture2D());
                                    if (finalTex->initWithImage(&finalImg)) {
                                        // Ref<> retains/releases automatically
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
        // The popup already retained the texture via its Ref<> m_texture; release
        // the caller's reference (refcount=1 from loadStaticImage). If popup is
        // null, this frees the unused texture instead of leaking it.
        CC_SAFE_RELEASE(loaded.texture);
    }

    // Profile music functions

    void onOpenProfileSettings(CCObject*) {        if (!this->m_ownProfile) return;

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
            // Download config and open CommentBgSettingsPopup
            ThumbnailAPI::get().downloadProfileConfig(accID, [accID](bool success, ProfileConfig const& config) {
                ProfileConfig effectiveConfig = success ? config : ProfileConfig();
                auto popup = CommentBgSettingsPopup::create(accID, effectiveConfig);
                if (popup) popup->show();
            });
        });
        popup->setOnGlobalIconCallback([self](bool enabled) {
            auto page = self.lock();
            if (!page) return;
            int accID = page->m_accountID;
            auto* am = GJAccountManager::get();
            std::string username = am ? std::string(am->m_username) : std::string();
            if (enabled) {
                PaimonNotify::create(Localization::get().getString("globalicon.uploading").c_str(), NotificationIcon::Info)->show();
                paimon::globalicon::GlobalIconService::get().uploadActiveIcons(accID, username,
                    [](bool ok, std::string const& msg) {
                        paimon::globalicon::GlobalIconService::setEnabledLocally(ok);
                        if (ok) {
                            PaimonNotify::create(Localization::get().getString("globalicon.upload_ok").c_str(), NotificationIcon::Success)->show();
                        } else if (msg == "no custom icons") {
                            PaimonNotify::create(Localization::get().getString("globalicon.no_custom").c_str(), NotificationIcon::Warning)->show();
                        } else if (msg == "More Icons not installed") {
                            PaimonNotify::create(Localization::get().getString("globalicon.requires_moreicons_title").c_str(), NotificationIcon::Error)->show();
                        } else {
                            // Show the HTTP detail for diagnosis (URL/key/etc.).
                            log::warn("[GlobalIcon] upload failed: {}", msg);
                            std::string detail = msg.substr(0, 64);
                            PaimonNotify::create(
                                (Localization::get().getString("globalicon.upload_failed") + ": " + detail).c_str(),
                                NotificationIcon::Error)->show();
                        }
                    });
            } else {
                PaimonNotify::create(Localization::get().getString("globalicon.clearing").c_str(), NotificationIcon::Info)->show();
                paimon::globalicon::GlobalIconService::get().clearIcons(accID, username,
                    [](bool ok, std::string const&) {
                        paimon::globalicon::GlobalIconService::setEnabledLocally(false);
                        PaimonNotify::create(
                            Localization::get().getString(ok ? "globalicon.cleared" : "globalicon.clear_failed").c_str(),
                            ok ? NotificationIcon::Success : NotificationIcon::Error)->show();
                    });
            }
        });
        popup->show();
    }

    void onConfigureProfileMusic(CCObject*) {
        // Own profile only
        if (!this->m_ownProfile) {
            PaimonNotify::create("You can only configure music on your own profile", NotificationIcon::Warning)->show();
            return;
        }

        // Open the music popup
        if (auto popup = ProfileMusicPopup::create(this->m_accountID)) {
            popup->show();
        }    }

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
            // Play if not already playing
            AudioContextCoordinator::get().activateProfile(this->m_accountID);
            m_fields->m_musicPlaying = true;
            updatePauseButtonSprite(true);
        }
    }

    void updatePauseButtonSprite(bool isPlaying) {
        if (!m_fields->m_musicPauseBtn) return;

        // Change sprite by state
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
        // Check whether profile music is enabled
        if (!ProfileMusicManager::get().isEnabled()) {
            m_fields->m_menuMusicPaused = false;
            return;
        }

        auto& musicMgr = ProfileMusicManager::get();

        // If the cached background ProfileConfig enables Audio Video mode, resolve
        // the cached background .mp4 path and force that config over the profile
        // music. It takes priority over any configured songID/isCustom.
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
            cfg.videoAudioPath = videoPath;  // may be empty if not downloaded yet
            cfg.enabled = true;               // force enabled even if it came off
            return true;
        };

        // Optimistic: play from cache
        ProfileMusicManager::ProfileMusicConfig cachedConfig;
        bool hasCachedConfig = musicMgr.tryGetImmediateConfig(accountID, cachedConfig);
        bool useVideoAudioOverride = applyVideoAudioOverride(cachedConfig);
        bool playableFromCache = hasCachedConfig &&
            (cachedConfig.songID > 0 || cachedConfig.isCustom || cachedConfig.useVideoAudio) &&
            cachedConfig.enabled;
        // For useVideoAudio we don't need the standard MP3 cache; the video's WAV
        // is resolved separately. For everything else we do.
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
            // useVideoAudio active but the video isn't cached yet. Don't play:
            // when the video arrives, ensureAnimatedProfileImg does its job and
            // the next checkAndPlayProfileMusic call will have the path resolved.
            log::info("[ProfilePage] useVideoAudio set but video not yet cached for {}", accountID);
        }

        // Check for fresh config from the server
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

            // Apply the useVideoAudio override before validating. Local copy
            // because the lambda receives the config by const reference.
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
                if (paimon::isRuntimeShuttingDown()) return;
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

    // Cancel all timers scheduled by the page (close / exit).
    void unscheduleProfileTimers() {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->unschedule(schedule_selector(PaimonProfilePage::refreshProfileBackdropTick));
        this->unschedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity));
        this->unschedule(schedule_selector(PaimonProfilePage::fadeStepTick));
    }

    $override
    void keyBackClicked() {
        // Android: avoid an extra hook on keyBackClicked due to a bad binding
        // thunk on ARM64 that causes SIGILL. Audio cleanup already happens in
        // onClose() / onExit().
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        ProfilePage::keyBackClicked();
#else
        m_fields->m_leaveForClose = true;
        cleanupProfileAudio();
        ProfilePage::keyBackClicked();
#endif
    }

    $override
    void onClose(CCObject* sender) {
        m_fields->m_leaveForClose = true;
        cleanupProfileAudio();
        unscheduleProfileTimers();
        ProfilePage::onClose(sender);
    }

    $override
    void onEnterTransitionDidFinish() {
        ProfilePage::onEnterTransitionDidFinish();
        refreshProfileBackdropAfterVanillaLayout();
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
        unscheduleProfileTimers();

        cleanupProfileAudio();

        ProfilePage::onExit();
    }

    void fadeMenuMusicStep(Ref<ProfilePage> safeRef, int step, int totalSteps, float fromVol, float toVol) {
        // Progressive fade using the Cocos2d scheduler
        float stepDelay = 500.0f / static_cast<float>(totalSteps) / 1000.0f; // to seconds

        // Apply the current step
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

        m_fields->m_fadeStep = step + 1;
        m_fields->m_fadeTotalSteps = totalSteps;
        m_fields->m_fadeFromVol = fromVol;
        m_fields->m_fadeToVol = toVol;

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

    void hideHintLabels(float) {
        if (auto* n = this->getChildByIDRecursive("my-levels-hint"))
            n->setVisible(false);
        if (auto* n = this->getChildByIDRecursive("my-lists-hint"))
            n->setVisible(false);
        // Make background white (non-redesign case; redesign uses prepareInPlace)
        CCNode* bg = this->getChildByID("background");
        if (!bg && this->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(child)) {
                    bg = child;
                    break;
                }
            }
        }
        if (auto* scale9 = typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(bg)) {
            scale9->setCascadeColorEnabled(true);
            scale9->setColor({255, 255, 255});
            scale9->setOpacity(255);
        } else if (auto* sprite = typeinfo_cast<CCSprite*>(bg)) {
            sprite->setColor({255, 255, 255});
            sprite->setOpacity(255);
        }
    }
};
