#include "../utils/FilesystemPath.hpp"
#include "../ui/PaimonPopup.hpp"
#include "../ui/PaimonUI.hpp"
#include <Geode/modify/LeaderboardsLayer.hpp>
#include <Geode/ui/NodeEvent.hpp>
#include "../core/ModAuthFlow.hpp"
#include "../framework/HookConventions.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/Localization.hpp"
#include "../utils/PaimonNotification.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <fstream>
#include <algorithm>
#include <memory>
#include <optional>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/BetaUploadWarning.hpp"
#include "../features/community/ui/CommunityHubLayer.hpp"
#include "../features/community/ui/CommunityHubIcon.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../utils/stb_image.h"
#include "../utils/ImageLoadHelper.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../utils/ThreadTracker.hpp"
#include "../features/scorecell/ui/LeaderboardLayoutPopup.hpp"
#include "../features/scorecell/ScoreCellRefresh.hpp"
#include "../core/modules/ModuleRegistry.hpp"

using namespace geode::prelude;

class ProfilePreviewPopup : public PaimonPopup {
protected:
    std::vector<uint8_t> m_data;
    std::string m_username;
    Ref<CCTexture2D> m_texture;
    geode::CopyableFunction<void()> m_callback;

    bool init() {
        if (!PaimonPopup::init(360.f, 180.f)) return false;

        this->setTitle("Preview Profile");
        this->addInfoButton("Preview Profile",
            "This is how your profile background will look on the leaderboard. Press "
            "<cg>Upload</c> to make it live, or close to keep your current one.");

        CCTexture2D* texture = m_texture;

        ProfileConfig config;
        config.backgroundType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
        config.blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
        config.darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
        config.useGradient = false;
        config.colorA = {255, 255, 255};
        config.colorB = {255, 255, 255};
        config.separatorColor = {0, 0, 0};
        config.separatorOpacity = 50;

        CCSize previewSize = {300.f, 120.f};
        auto previewNode = ProfileThumbs::get().createProfileNode(texture, config, previewSize);
        if (previewNode) {
            previewNode->setAnchorPoint({0.5f, 0.5f});
            previewNode->ignoreAnchorPointForPosition(false);
            previewNode->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint{0, 5});
            m_mainLayer->addChild(previewNode);
        }

        auto uploadBtn = CCMenuItemSpriteExtra::create(
            paimon::ui::makeButtonSprite("Upload", paimon::ui::Btn::Green, 90.f),
            this,
            menu_selector(ProfilePreviewPopup::onUpload)
        );
        auto menu = CCMenu::create();
        menu->addChild(uploadBtn);
        menu->setPosition({m_mainLayer->getContentSize().width / 2, 30});
        m_mainLayer->addChild(menu);

        paimon::markDynamicPopup(this);
        return true;
    }

    void onUpload(CCObject*) {
        auto cb = std::move(m_callback);
        this->onClose(nullptr);
        paimon::showBetaUploadWarningIfNeeded([cb]() {
            if (cb) cb();
        });
    }

public:
    static ProfilePreviewPopup* create(std::vector<uint8_t> const& data, std::string const& username, CCTexture2D* texture, geode::CopyableFunction<void()> callback) {
        auto ret = new ProfilePreviewPopup();
        if (ret) {
            ret->m_data = data;
            ret->m_username = username;
            ret->m_texture = texture;
            ret->m_callback = callback;
        }
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

namespace {

// bounded worker-side read for profile uploads: size policy is enforced
// before touching the main thread. on failure the error toast is posted to
// main (text produced there) and nullopt comes back; an empty file yields an
// empty buffer, which callers treat as before.
std::optional<std::vector<uint8_t>> readProfileUploadBytes(
    std::filesystem::path const& path, std::uintmax_t maxBytes,
    std::function<std::string()> openErrorText,
    std::function<std::string()> tooLargeText) {
    auto failWith = [](std::function<std::string()> const& makeText) {
        geode::Loader::get()->queueInMainThread([makeText] {
            if (paimon::isRuntimeShuttingDown()) return;
            PaimonNotify::show(makeText().c_str(), NotificationIcon::Error);
        });
    };

    std::error_code sizeEc;
    auto const onDisk = std::filesystem::file_size(path, sizeEc);
    if (sizeEc) {
        failWith(openErrorText);
        return std::nullopt;
    }
    if (onDisk > maxBytes) {
        failWith(tooLargeText);
        return std::nullopt;
    }
    auto data = paimon::file::readBytes(path, maxBytes + 1);
    if (data.empty() && onDisk > 0) {
        failWith(openErrorText);
        return std::nullopt;
    }
    return data;
}

} // namespace

class $modify(PaimonLeaderboardsLayer, LeaderboardsLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LeaderboardsLayer::init");
    }

    $override
    bool init(LeaderboardType type, LeaderboardStat stat) {
        if (!LeaderboardsLayer::init(type, stat)) return false;
        this->addOnExitCallback([this] {
            ProfileThumbs::get().clearAllCache();
            return true;
        });
        log::info("[LeaderboardsLayer] init: type={}", static_cast<int>(type));
        
        LayerBackgroundManager::get().applyBackground(this, "leaderboards");

        createPaimonButtons();
        updateTabColors(type);
        
        return true;
    }

    void updateTabColors(LeaderboardType type) {
        auto getTabMenu = [this](std::string const& tabID, size_t fallbackIndex) -> CCMenu* {
            if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID(tabID))) {
                return byId;
            }
            std::vector<CCMenu*> topMenus;
            auto winH = CCDirector::get()->getWinSize().height;
            for (auto* node : CCArrayExt<CCNode*>(this->getChildren())) {
                auto menu = typeinfo_cast<CCMenu*>(node);
                if (!menu) continue;
                if (menu->getPositionY() > winH * 0.55f && menu->getChildrenCount() >= 1) {
                    topMenus.push_back(menu);
                }
            }
            std::sort(topMenus.begin(), topMenus.end(), [](CCMenu* a, CCMenu* b) {
                return a->getPositionX() < b->getPositionX();
            });
            if (fallbackIndex < topMenus.size()) {
                return topMenus[fallbackIndex];
            }
            return nullptr;
        };

        std::vector<std::string> tabIDs = {"top-100-menu", "global-menu", "creators-menu", "friends-menu"};

        size_t activeIdx = tabIDs.size();
        switch (type) {
            case LeaderboardType::Default: activeIdx = 0; break;
            case LeaderboardType::Global: activeIdx = 1; break;
            case LeaderboardType::Creator: activeIdx = 2; break;
            case LeaderboardType::Friends: activeIdx = 3; break;
            default: break;
        }

        // one pass: the active lookup is deterministic, so coloring inline
        // gives the same menu the second getTabMenu call would return.
        for (size_t i = 0; i < tabIDs.size(); ++i) {
            auto* menu = getTabMenu(tabIDs[i], i);
            auto* btn = menu ? menu->getChildByType<CCMenuItemSpriteExtra>(0) : nullptr;
            if (btn) btn->setColor(i == activeIdx ? ccColor3B{0, 255, 0} : ccColor3B{255, 255, 255});
        }
    }

    $override
    void onTop(CCObject* sender) {
        LeaderboardsLayer::onTop(sender);
        updateTabColors(LeaderboardType::Default); 
    }

    $override
    void onGlobal(CCObject* sender) {
        LeaderboardsLayer::onGlobal(sender);
        updateTabColors(LeaderboardType::Global); 
    }

    $override
    void onCreators(CCObject* sender) {
        LeaderboardsLayer::onCreators(sender);
        updateTabColors(LeaderboardType::Creator);
    }

    void createPaimonButtons() {
        // init can re-run; don't duplicate the menu
        if (this->getChildByID("paimon-leaderboards-side-menu"_spr)) return;

        auto menu = CCMenu::create();
        menu->setID("paimon-leaderboards-side-menu"_spr);
        menu->setPosition({30, 100}); 
        this->addChild(menu);

        CCNode* modSprite = paimon::community::createCommunityHubButtonSprite();
        if (modSprite) modSprite->setScale(0.7f);

        auto modBtn = CCMenuItemSpriteExtra::create(
            modSprite,
            this,
            menu_selector(PaimonLeaderboardsLayer::onOpenModerators)
        );
        modBtn->setPosition({0, 0});
        menu->addChild(modBtn);
        
        auto uploadSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
        if (uploadSprite) {
            uploadSprite->setScale(0.7f);
        }
        
        auto uploadBtn = CCMenuItemSpriteExtra::create(
            uploadSprite,
            this,
            menu_selector(PaimonLeaderboardsLayer::onUploadBanner)
        );
        uploadBtn->setPosition({0, -35}); 
        menu->addChild(uploadBtn);

        // gear button opens the gjscorecell fx settings popup
        if (paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) {
            constexpr float S = 30.f;
            auto gearContainer = CCNode::create();
            gearContainer->setContentSize({S, S});
            gearContainer->setAnchorPoint({0.5f, 0.5f});

            CCNode* base = paimon::SpriteHelper::safeCreateScale9WithFrameName("GJ_button_05.png");
            if (auto* s9 = typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(base)) {
                s9->setContentSize({S, S});
            }
            if (!base) {
                base = paimon::SpriteHelper::createColorPanel(S, S, {0, 0, 0}, 120, 5.f);
            }
            if (base) {
                base->setAnchorPoint({0.5f, 0.5f});
                base->setPosition({S / 2.f, S / 2.f});
                gearContainer->addChild(base);
            }

            auto gear = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
            if (gear) {
                float md = std::max(gear->getContentWidth(), gear->getContentHeight());
                if (md > 0) gear->setScale((S * 0.72f) / md);
                gear->setPosition({S / 2.f, S / 2.f});
                gearContainer->addChild(gear);
            }

            auto gearBtn = CCMenuItemSpriteExtra::create(
                gearContainer, this, menu_selector(PaimonLeaderboardsLayer::onOpenScoreCellSettings));
            gearBtn->setPosition({0, -70});
            menu->addChild(gearBtn);
        }
    }

    void onOpenScoreCellSettings(CCObject*) {
        auto popup = paimon::scorecell::LeaderboardLayoutPopup::create();
        if (!popup) return;
        popup->setOnClose([]() {
            paimon::scorecell::refreshAllCells();
        });
        popup->show();
    }

    void onOpenModerators(CCObject*) {
        if (auto* scene = CommunityHubLayer::scene()) TransitionManager::get().pushScene(scene);
    }

    void onUploadBanner(CCObject*) {
        bool canUploadGIF = Mod::get()->getSavedValue<bool>("is-verified-vip", false)
                         || paimon::modauth::isVerified()
                         || paimon::modauth::isVerified(true);

        WeakRef<PaimonLeaderboardsLayer> self = this;
        pt::pickImage([self, canUploadGIF](geode::Result<std::optional<std::filesystem::path>> result) {
            auto layer = self.lock();
            if (!layer) return;
            auto pathOpt = std::move(result).unwrapOr(std::nullopt);
            if (!pathOpt || pathOpt->empty()) return;

            auto path = std::move(*pathOpt);
            auto ext = geode::utils::string::toLower(
                paimon::paths::toUtf8(path.extension())
            );

            if (ext == ".gif") {
                if (!canUploadGIF) {
                     PaimonNotify::show("GIFs are restricted to Mods/Admins/Donators", NotificationIcon::Error);
                     return;
                }
                layer->processProfileGIF(path);
            } else {
                layer->processProfileImage(path);
            }
        });
    }

    void processProfileGIF(std::filesystem::path path) {
        paimon::ThreadTracker::get().spawn([path]() {
            geode::utils::thread::setName("PaimonProfileGIF");
            // bounded read on the worker: the 10 mb policy is enforced before
            // touching the main thread (empty file still uploads, as before).
            constexpr std::uintmax_t kMaxGifBytes = 10ull * 1024 * 1024;
            auto dataOpt = readProfileUploadBytes(path, kMaxGifBytes,
                [] { return std::string("Failed to read GIF file"); },
                [] { return std::string("GIF too large (max 10MB)"); });
            if (!dataOpt) return;
            auto data = std::move(*dataOpt);

            geode::Loader::get()->queueInMainThread([data = std::move(data)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;

                auto* accountManager = GJAccountManager::get();
                if (!accountManager) {
                    PaimonNotify::show("Account manager unavailable", NotificationIcon::Error);
                    return;
                }

                int accountID = accountManager->m_accountID;
                std::string username = accountManager->m_username;

                PaimonNotify::show(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info);

                paimon::showBetaUploadWarningIfNeeded([accountID, data = std::move(data), username]() mutable {
                    ThumbnailAPI::get().uploadProfileGIF(accountID, data, username, [](bool success, std::string const& msg) {
                        if (success) {
                            PaimonNotify::show(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success);
                        } else {
                            PaimonNotify::show((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error);
                        }
                    });
                });
            });
        });
    }

    void processProfileImage(std::filesystem::path path) {
        paimon::ThreadTracker::get().spawn([path]() {
            geode::utils::thread::setName("PaimonProfileImage");
            // bounded read on the worker: the 7 mb policy is enforced before
            // decoding or touching the main thread.
            constexpr std::uintmax_t kMaxImageBytes = 7ull * 1024 * 1024;
            auto rawOpt = readProfileUploadBytes(path, kMaxImageBytes,
                [] { return Localization::get().getString("profile.image_open_error"); },
                [] { return std::string("Image too large (max 7MB)"); });
            if (!rawOpt) return;
            auto rawData = std::move(*rawOpt);

            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load_from_memory(rawData.data(), static_cast<int>(rawData.size()), &w, &h, &ch, 4);

            geode::Loader::get()->queueInMainThread([rawData = std::move(rawData), px, w, h]() mutable {
                if (paimon::isRuntimeShuttingDown()) {
                    if (px) stbi_image_free(px);
                    return;
                }
                auto* accountManager = GJAccountManager::get();
                if (!accountManager) {
                    if (px) stbi_image_free(px);
                    PaimonNotify::show("Account manager unavailable", NotificationIcon::Error);
                    return;
                }

                int accountID = accountManager->m_accountID;
                std::string username = accountManager->m_username;

                CCTexture2D* texture = nullptr;
                if (px) {
                    if (w > 0 && h > 0) {
                        auto img = ImageLoadHelper::createFromRGBA(px, w, h, false);
                        if (img.success) {
                            texture = img.texture;
                            texture->autorelease();
                        }
                    }
                    stbi_image_free(px);
                }

                if (!texture) {
                    auto* image = new CCImage();
                    if (!image->initWithImageData(const_cast<uint8_t*>(rawData.data()), rawData.size())) {
                        image->release();
                        PaimonNotify::show("Failed to decode image", NotificationIcon::Error);
                        return;
                    }
                    auto* tex = new CCTexture2D();
                    if (!tex->initWithImage(image)) {
                        image->release();
                        tex->release();
                        PaimonNotify::show("Failed to create texture", NotificationIcon::Error);
                        return;
                    }
                    image->release();
                    tex->autorelease();
                    texture = tex;
                }

                auto popup = ProfilePreviewPopup::create(rawData, username, texture, [rawData, accountID, username]() {
                    PaimonNotify::show(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info);
                    ThumbnailAPI::get().uploadProfile(accountID, rawData, username, [](bool success, std::string const& msg) {
                        if (success) {
                            bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                            if (isPending) {
                                PaimonNotify::show("Background submitted! Pending moderator verification.", NotificationIcon::Warning);
                            } else {
                                PaimonNotify::show(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success);
                            }
                        } else {
                            PaimonNotify::show((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error);
                        }
                    });
                });
                if (popup) popup->show();
            });
        });
    }
};
