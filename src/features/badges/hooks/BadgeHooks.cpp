#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../../../framework/HookConventions.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../thumbnails/services/ThumbnailTransportClient.hpp"
#include <list>
#include <mutex>
#include <algorithm>
#include "../../moderation/services/ModeratorCache.hpp"
#include "../../emotes/EmoteRenderer.hpp"
#include "../../emotes/services/EmoteService.hpp"
#include "../../emotes/services/EmoteCache.hpp"
#include "../../fonts/FontTag.hpp"
#include "../../profiles/services/ProfileThumbs.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/CommentTextSelector.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../profiles/services/CustomBadgeService.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../services/RoleService.hpp"
#include "../services/RoleBadges.hpp"

using namespace geode::prelude;

// Legacy BadgeCache wrappers delegate to ModeratorCache.

std::map<std::string, std::pair<bool, bool>> g_moderatorCache;
std::list<std::string> g_moderatorCacheOrder;

void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin) {
    ModeratorCache::get().insert(username, isMod, isAdmin);
}

bool moderatorCacheGet(std::string const& username, bool& isMod, bool& isAdmin) {
    auto status = ModeratorCache::get().lookup(username);
    if (!status) return false;
    isMod = status->isMod;
    isAdmin = status->isAdmin;
    return true;
}

void showBadgeInfoPopup(CCNode* sender) {
    paimon::badges::showRoleBadgeInfoPopup(sender);
}

// Defined after $modify because it needs BadgeCommentCell.
static void deferEmoteRetry(WeakRef<CommentCell> weakSelf,
                            std::string text, std::string font, int retries);

namespace {
// Default comment panel: translucent dark blue-gray.
constexpr GLubyte kCommentDarkPanelOpacity = 60;
constexpr cocos2d::ccColor3B kCommentPanelColor = {30, 33, 48};
constexpr float kCommentInsetX = 2.0f;
constexpr float kCommentInsetY = 1.0f;

struct CommentBackgroundLayout {
    CCPoint origin;
    CCSize size;
    float radius;
};

CommentBackgroundLayout getCommentBackgroundLayout(CCSize const& cellSize) {
    auto size = CCSize(
        std::max(8.0f, cellSize.width - kCommentInsetX * 2.0f),
        std::max(8.0f, cellSize.height - kCommentInsetY * 2.0f)
    );

    float radius = std::clamp(size.height * 0.16f, 4.0f, 6.5f);
    return {
        {kCommentInsetX, kCommentInsetY},
        size,
        radius,
    };
}
}

class $modify(BadgeCommentCell, CommentCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "CommentCell::loadFromComment");
    }

    struct Fields {
        Ref<CCNode> m_commentBgPanel = nullptr;
        Ref<CCClippingNode> m_commentBgClip = nullptr;
        Ref<CCLayerColor> m_commentBgDarkOverlay = nullptr;
        int m_commentBgToken = 0;
        int m_commentBgAccountID = 0;
        int m_vanillaBgHideTicks = 0;
    };

    void clearCommentProfileBackground() {
        ++m_fields->m_commentBgToken;
        m_fields->m_commentBgAccountID = 0;

        if (m_fields->m_commentBgPanel) {
            m_fields->m_commentBgPanel->removeFromParent();
            m_fields->m_commentBgPanel = nullptr;
        }
        if (m_fields->m_commentBgClip) {
            m_fields->m_commentBgClip->removeFromParent();
            m_fields->m_commentBgClip = nullptr;
        }
        if (m_fields->m_commentBgDarkOverlay) {
            m_fields->m_commentBgDarkOverlay->removeFromParent();
            m_fields->m_commentBgDarkOverlay = nullptr;
        }
    }

    void hideVanillaCommentBackgrounds() {
        auto hideRecursive = [](auto const& self, CCNode* node) -> void {
            if (!node) return;
            auto* children = node->getChildren();
            if (!children) return;

            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                std::string nodeID = child->getID();
                if (!nodeID.empty() && nodeID.find("paimon-") != std::string::npos) {
                    continue;
                }

                if (typeinfo_cast<CCLayerColor*>(child)) {
                    child->setVisible(false);
                    continue;
                }

                if (typeinfo_cast<CCScale9Sprite*>(child)) {
                    child->setVisible(false);
                    continue;
                }

                if (!typeinfo_cast<CCMenu*>(child)) {
                    self(self, child);
                }
            }
        };

        hideRecursive(hideRecursive, this);
    }

    bool shouldHandleCommentProfileResult(int token, int accountID) {
        return m_comment && m_fields->m_commentBgToken == token && m_comment->m_accountID == accountID;
    }

    bool shouldRefreshCommentPanel(int token) {
        return m_comment && m_fields->m_commentBgToken == token;
    }

    void scheduleCommentPanelRefresh(int token, int retries) {
        WeakRef<CommentCell> weakSelf = static_cast<CommentCell*>(this);
        paimon::scheduleMainThreadDelay(0.05f, [weakSelf, token, retries]() {
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;

            auto* commentCell = static_cast<BadgeCommentCell*>(self.data());
            if (!commentCell || !commentCell->shouldRefreshCommentPanel(token)) {
                return;
            }

            if (commentCell->m_fields->m_commentBgClip || commentCell->m_fields->m_commentBgPanel) {
                return;
            }

            commentCell->installDarkCommentPanel();

            if (retries > 1) {
                commentCell->scheduleCommentPanelRefresh(token, retries - 1);
            }
        });
    }

    void installDarkCommentPanel() {
        auto cellSize = this->getContentSize();
        if (cellSize.width < 40.0f || cellSize.height < 18.0f) {
            return;
        }

        auto layout = getCommentBackgroundLayout(cellSize);

        // Check config FIRST — custom bg always needs re-evaluation
        bool hasCustomBg = false;
        ProfileConfig config;
        if (m_comment && m_comment->m_accountID > 0) {
            config = ProfileThumbs::get().getProfileConfig(m_comment->m_accountID);
            hasCustomBg = (config.commentBgType != "none" && config.commentBgType != "");
        }

        if (m_fields->m_commentBgPanel && !hasCustomBg) {
            auto oldSize = m_fields->m_commentBgPanel->getContentSize();
            auto oldPos = m_fields->m_commentBgPanel->getPosition();
            if (std::abs(oldSize.width - layout.size.width) < 0.5f &&
                std::abs(oldSize.height - layout.size.height) < 0.5f &&
                std::abs(oldPos.x - layout.origin.x) < 0.5f &&
                std::abs(oldPos.y - layout.origin.y) < 0.5f) {
                hideVanillaCommentBackgrounds();
                ensureVanillaBgLayerHidden();
                return;
            }
        }

        if (m_fields->m_commentBgPanel) {
            m_fields->m_commentBgPanel->removeFromParent();
            m_fields->m_commentBgPanel = nullptr;
        }
        if (m_fields->m_commentBgClip) {
            m_fields->m_commentBgClip->removeFromParent();
            m_fields->m_commentBgClip = nullptr;
        }
        if (m_fields->m_commentBgDarkOverlay) {
            m_fields->m_commentBgDarkOverlay->removeFromParent();
            m_fields->m_commentBgDarkOverlay = nullptr;
        }

        hideVanillaCommentBackgrounds();

        if (hasCustomBg) {
            installCustomCommentBackground(layout, config);
        } else {
            auto* panel = paimon::SpriteHelper::createColorPanel(
                layout.size.width,
                layout.size.height,
                kCommentPanelColor,
                kCommentDarkPanelOpacity,
                layout.radius
            );
            if (!panel) return;

            panel->setPosition(layout.origin);
            panel->setZOrder(-12);
            panel->setID("paimon-comment-bg-panel"_spr);
            this->addChild(panel);
            m_fields->m_commentBgPanel = panel;
            ensureVanillaBgLayerHidden();
        }
    }

    void installCustomCommentBackground(CommentBackgroundLayout const& layout, ProfileConfig const& config) {
        auto cellSize = this->getContentSize();

        if (config.commentBgType == "solid") {
            // Solid color background — bake color directly into vertices
            auto* panel = paimon::SpriteHelper::createColorPanel(
                layout.size.width,
                layout.size.height,
                config.commentBgSolidColor,
                static_cast<GLubyte>(config.commentBgSolidOpacity),
                layout.radius
            );
            if (!panel) return;

            panel->setPosition(layout.origin);
            panel->setZOrder(-12);
            panel->setID("paimon-comment-bg-panel"_spr);
            this->addChild(panel);
            m_fields->m_commentBgPanel = panel;
            ensureVanillaBgLayerHidden();
        }
        else if (config.commentBgType == "thumbnail" || config.commentBgType == "banner") {
            // Image-based background — load async then render
            int accountID = m_comment ? m_comment->m_accountID : 0;
            int token = m_fields->m_commentBgToken;

            if (config.commentBgType == "thumbnail" && !config.commentBgThumbnailId.empty()) {
                int levelId = 0;
                auto levelIdRes = geode::utils::numFromString<int>(config.commentBgThumbnailId);
                if (!levelIdRes) return;
                levelId = levelIdRes.unwrap();
                int targetPos = config.commentBgThumbnailPos;

                WeakRef<BadgeCommentCell> weakSelf = this;

                ThumbnailAPI::get().getThumbnails(levelId,
                    [weakSelf, token, accountID, layout, config, cellSize, targetPos](bool success, std::vector<ThumbnailInfo> const& thumbs) {
                        Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, success, thumbs, targetPos]() {
                            if (paimon::isRuntimeShuttingDown()) return;
                            auto selfRef = weakSelf.lock();
                            auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
                            if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) return;
                            if (success && !thumbs.empty()) {
                                int idx = std::clamp(targetPos, 1, static_cast<int>(thumbs.size())) - 1;
                                auto& thumb = thumbs[idx];

                                if (!thumb.url.empty()) {
                                    HttpClient::get().downloadFromUrl(thumb.url,
                                        [weakSelf, token, accountID, layout, config, cellSize](bool ok, std::vector<uint8_t> const& data, int, int) {
                                            Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, ok, data]() {
                                                if (paimon::isRuntimeShuttingDown()) return;
                                                auto selfRef = weakSelf.lock();
                                                auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
                                                if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) return;
                                                if (ok && !data.empty()) {
                                                    auto* tex = ThumbnailTransportClient::bytesToTexture(data);
                                                    if (tex) {
                                                        self->installImageCommentBackground(layout, tex, config, cellSize);
                                                    }
                                                }
                                            });
                                        }
                                    );
                                }
                            }
                        });
                    }
                );
            }
            else if (config.commentBgType == "banner") {
                if (config.commentBgBannerMode == "image") {
                    WeakRef<BadgeCommentCell> weakSelf = this;
                    ThumbnailAPI::get().downloadProfileImg(accountID,
                        [weakSelf, token, accountID, layout, config, cellSize](bool success, CCTexture2D* texture) {
                            Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, success, texture]() {
                                if (paimon::isRuntimeShuttingDown()) return;
                                auto selfRef = weakSelf.lock();
                                auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
                                if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) return;
                                if (success && texture) {
                                    self->installImageCommentBackground(layout, texture, config, cellSize);
                                }
                            });
                        }
                    );
                } else {
                    auto& thumbs = ProfileThumbs::get();
                    if (auto cached = thumbs.getCachedProfile(accountID)) {
                        if (cached->texture) {
                            installImageCommentBackground(layout, cached->texture.data(), config, cellSize);
                        } else if (!cached->gifKey.empty()) {
                            installGifCommentBackground(layout, cached->gifKey, config, cellSize);
                        }
                        return;
                    }

                    std::string username = m_comment ? m_comment->m_userName : "";
                    WeakRef<BadgeCommentCell> weakSelf = this;
                    thumbs.queueLoad(accountID, username,
                        [weakSelf, token, accountID, layout, config, cellSize](bool success, CCTexture2D*) {
                            Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, success]() {
                                if (paimon::isRuntimeShuttingDown()) return;
                                auto selfRef = weakSelf.lock();
                                auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
                                if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) return;
                                if (success) {
                                    auto cached = ProfileThumbs::get().getCachedProfile(accountID);
                                    if (cached.has_value()) {
                                        if (cached->texture) {
                                            self->installImageCommentBackground(layout, cached->texture.data(), config, cellSize);
                                        } else if (!cached->gifKey.empty()) {
                                            self->installGifCommentBackground(layout, cached->gifKey, config, cellSize);
                                        }
                                    }
                                }
                            });
                        }
                    );
                }
            }
        }
    }

    void installImageCommentBackground(CommentBackgroundLayout const& layout, CCTexture2D* texture, ProfileConfig const& config, CCSize const& cellSize) {
        if (!texture) return;

        if (m_fields->m_commentBgClip) {
            m_fields->m_commentBgClip->removeFromParent();
            m_fields->m_commentBgClip = nullptr;
        }
        if (m_fields->m_commentBgDarkOverlay) {
            m_fields->m_commentBgDarkOverlay->removeFromParent();
            m_fields->m_commentBgDarkOverlay = nullptr;
        }
        if (m_fields->m_commentBgPanel) {
            m_fields->m_commentBgPanel->removeFromParent();
            m_fields->m_commentBgPanel = nullptr;
        }

        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(layout.size.width, layout.size.height, layout.radius);
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(layout.size);
        clipper->setPosition(layout.origin);
        clipper->setZOrder(-13);
        clipper->setID("paimon-comment-bg-clip"_spr);
        this->addChild(clipper);
        m_fields->m_commentBgClip = clipper;
        ensureVanillaBgLayerHidden();

        float darkness = config.commentBgDarkness;
        if (darkness > 0.0f) {
            auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(darkness * 255)});
            overlay->setContentSize(layout.size);
            overlay->setPosition(layout.origin);
            overlay->setZOrder(-12);
            overlay->setID("paimon-comment-bg-dark"_spr);
            this->addChild(overlay);
            m_fields->m_commentBgDarkOverlay = overlay;
        }

        CCSize targetSize = layout.size;
        targetSize.width = std::max(targetSize.width, 256.f);
        targetSize.height = std::max(targetSize.height, 128.f);

        float blurIntensity = config.commentBgBlur;
        bool usePaimonBlur = (config.commentBgBlurType == "paimon");

        int token = m_fields->m_commentBgToken;
        int accountID = m_comment ? m_comment->m_accountID : 0;
        WeakRef<BadgeCommentCell> weakSelf = this;
        Ref<CCClippingNode> clipRef = clipper;

        auto attachBlurred = [weakSelf, token, accountID, layout, clipRef](CCNode* bgNode) {
            if (!bgNode) return;
            auto selfRef = weakSelf.lock();
            auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
            if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) return;
            if (self->m_fields->m_commentBgClip.data() != clipRef.data() || !clipRef->getParent()) return;

            CCSize bgSize = bgNode->getContentSize();
            if (bgSize.width > 0 && bgSize.height > 0) {
                float scaleToFitX = layout.size.width / bgSize.width;
                float scaleToFitY = layout.size.height / bgSize.height;
                bgNode->setScale(std::max(scaleToFitX, scaleToFitY));
            }
            bgNode->setAnchorPoint({0.5f, 0.5f});
            bgNode->setPosition(layout.size / 2);
            clipRef->addChild(bgNode, -1);
        };

        // Blur asynchronously and cache the result to keep scrolling responsive.
        std::string cacheKey = fmt::format("cbg:{}:{}:{}", accountID,
            config.commentBgType == "thumbnail"
                ? fmt::format("t{}_{}", config.commentBgThumbnailId, config.commentBgThumbnailPos)
                : fmt::format("b{}", config.commentBgBannerMode),
            usePaimonBlur ? "p" : "g");

        Ref<CCTexture2D> texRef = texture;
        auto onBlur = [attachBlurred, texRef](CCSprite* blurred) {
            if (blurred) {
                attachBlurred(blurred);
                return;
            }
            auto tempSprite = CCSprite::createWithTexture(texRef.data());
            if (!tempSprite) return;
            attachBlurred(tempSprite);
        };

        if (usePaimonBlur) {
            BlurSystem::getInstance()->buildPaimonBlurAsync(texture, targetSize, blurIntensity, cacheKey, onBlur);
        } else {
            BlurSystem::getInstance()->buildGaussianBlurAsync(texture, targetSize, blurIntensity, cacheKey, onBlur);
        }
    }

    void installGifCommentBackground(CommentBackgroundLayout const& layout, std::string const& gifKey, ProfileConfig const& config, CCSize const& cellSize) {
        if (gifKey.empty()) return;

        if (m_fields->m_commentBgClip) {
            m_fields->m_commentBgClip->removeFromParent();
            m_fields->m_commentBgClip = nullptr;
        }
        if (m_fields->m_commentBgDarkOverlay) {
            m_fields->m_commentBgDarkOverlay->removeFromParent();
            m_fields->m_commentBgDarkOverlay = nullptr;
        }
        if (m_fields->m_commentBgPanel) {
            m_fields->m_commentBgPanel->removeFromParent();
            m_fields->m_commentBgPanel = nullptr;
        }

        CCNode* bgNode = nullptr;

        // Prefer a static frame; per-cell realtime blur/video is too expensive.
        if (VideoThumbnailSprite::isCached(gifKey)) {
            auto bgVideo = VideoThumbnailSprite::createFromCache(gifKey);
            if (bgVideo) {
                float scaleX = layout.size.width / std::max(1.f, bgVideo->getContentSize().width);
                float scaleY = layout.size.height / std::max(1.f, bgVideo->getContentSize().height);
                bgVideo->setScale(std::max(scaleX, scaleY));
                bgVideo->setAnchorPoint({0.5f, 0.5f});
                bgVideo->setPosition(layout.size * 0.5f);

                if (bgVideo->hasVisibleFrame()) {
                    bgVideo->pause();
                } else {
                    bgVideo->setOnFirstVisibleFrame([](VideoThumbnailSprite* spr) {
                        if (spr) spr->pause();
                    });
                    bgVideo->play();
                }
                bgNode = bgVideo;
            }
        }

        if (!bgNode) {
            auto bgGif = AnimatedGIFSprite::createFromCache(gifKey);
            if (bgGif) {
                float scaleX = layout.size.width / bgGif->getContentSize().width;
                float scaleY = layout.size.height / bgGif->getContentSize().height;
                bgGif->setScale(std::max(scaleX, scaleY));
                bgGif->setAnchorPoint({0.5f, 0.5f});
                bgGif->setPosition(layout.size * 0.5f);

                bgGif->stop();
                bgNode = bgGif;
            }
        }

        if (!bgNode) return;

        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(layout.size.width, layout.size.height, layout.radius);
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(layout.size);
        clipper->setPosition(layout.origin);
        clipper->setZOrder(-13);
        clipper->setID("paimon-comment-bg-clip"_spr);
        clipper->addChild(bgNode);
        this->addChild(clipper);
        m_fields->m_commentBgClip = clipper;
        ensureVanillaBgLayerHidden();

        float darkness = config.commentBgDarkness;
        if (darkness > 0.0f) {
            auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(darkness * 255)});
            overlay->setContentSize(layout.size);
            overlay->setPosition(layout.origin);
            overlay->setZOrder(-12);
            overlay->setID("paimon-comment-bg-dark"_spr);
            this->addChild(overlay);
            m_fields->m_commentBgDarkOverlay = overlay;
        }
    }

    void refreshCommentProfileBackground(GJComment* comment) {
        if (!comment || comment->m_accountID <= 0) {
            return;
        }

        auto& thumbs = ProfileThumbs::get();
        int accountID = comment->m_accountID;
        int token = m_fields->m_commentBgToken;
        std::string username = comment->m_userName;

        m_fields->m_commentBgAccountID = accountID;
        thumbs.notifyVisible(accountID);

        if (auto cached = thumbs.getCachedProfile(accountID)) {
            if (cached->texture || !cached->gifKey.empty()) {
                installDarkCommentPanel();
            }
            return;
        }

        if (thumbs.isNoProfile(accountID)) {
            return;
        }

        WeakRef<BadgeCommentCell> weakSelf = this;
        thumbs.queueLoad(accountID, username, [weakSelf, accountID, token](bool success, CCTexture2D*) {
            Loader::get()->queueInMainThread([weakSelf, accountID, token, success]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto selfRef = weakSelf.lock();
                auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
                if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) {
                    return;
                }

                if (success) {
                    self->installDarkCommentPanel();
                }
            });
        });
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(BadgeCommentCell::hideVanillaBgLayerTick));
        clearCommentProfileBackground();
        CommentCell::onExit();
    }

    // Briefly re-hide GD's background after updateBGColor().
    void hideVanillaBgLayerTick(float) {
        if (!(m_fields->m_commentBgPanel || m_fields->m_commentBgClip)) {
            this->unschedule(schedule_selector(BadgeCommentCell::hideVanillaBgLayerTick));
            return;
        }
        if (m_backgroundLayer && m_backgroundLayer->isVisible()) {
            m_backgroundLayer->setVisible(false);
        }
        if (++m_fields->m_vanillaBgHideTicks >= 6) {
            this->unschedule(schedule_selector(BadgeCommentCell::hideVanillaBgLayerTick));
        }
    }

    void ensureVanillaBgLayerHidden() {
        if (m_backgroundLayer) {
            m_backgroundLayer->setVisible(false);
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(m_backgroundLayer)) {
                rgba->setOpacity(0);
            }
        }
        this->unschedule(schedule_selector(BadgeCommentCell::hideVanillaBgLayerTick));
        if (m_fields->m_commentBgPanel || m_fields->m_commentBgClip) {
            m_fields->m_vanillaBgHideTicks = 0;
            this->schedule(schedule_selector(BadgeCommentCell::hideVanillaBgLayerTick), 0.2f);
        }
    }

    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    $override
    void loadFromComment(GJComment* comment) {
        this->unschedule(schedule_selector(BadgeCommentCell::hideVanillaBgLayerTick));
        clearCommentProfileBackground();
        this->setUserObject("paimon-comment-bgs-hidden"_spr, nullptr);
        // Drop old emotes before GD rebuilds recycled text.
        if (m_mainLayer) {
            if (auto* oldEmote = m_mainLayer->getChildByID("paimon-emote-overlay"_spr)) {
                oldEmote->removeFromParent();
            }
        }
        CommentCell::loadFromComment(comment);
        
        if (!comment) return;

        installDarkCommentPanel();
        scheduleCommentPanelRefresh(m_fields->m_commentBgToken, 1);

        {
            std::string commentText = comment->m_commentString;
            auto fontResult = paimon::fonts::parseFontTag(commentText);
            bool serviceLoaded = paimon::emotes::EmoteService::get().isLoaded();
            bool hasEmoteSyntax = paimon::emotes::EmoteRenderer::hasEmoteSyntax(fontResult.remainingText);
            bool hasEmotes = serviceLoaded && hasEmoteSyntax;
            bool hasMention = paimon::emotes::EmoteRenderer::hasMentionSyntax(fontResult.remainingText);

            if (fontResult.hasTag || hasEmotes || hasMention) {
                this->tryRenderWithFont(fontResult.remainingText, fontResult.fontFile);
            } else if (!serviceLoaded && hasEmoteSyntax) {
                WeakRef<CommentCell> weakSelf = static_cast<CommentCell*>(this);
                deferEmoteRetry(weakSelf, fontResult.remainingText, fontResult.fontFile, 10);
            }
        }

        {
            std::string commentText = comment->m_commentString;
            auto fontResult = paimon::fonts::parseFontTag(commentText);
            CCNode* textNode = m_mainLayer->getChildByID("comment-text-area");
            if (!textNode) textNode = m_mainLayer->getChildByID("comment-text-label");
            if (!textNode) textNode = m_mainLayer->getChildByID("paimon-emote-overlay"_spr);

            if (!textNode) {
                auto* children = m_mainLayer->getChildren();
                if (children) {
                    for (auto* obj : CCArrayExt<CCObject*>(children)) {
                        if (auto* area = typeinfo_cast<TextArea*>(obj)) {
                            textNode = area;
                            break;
                        }
                    }
                }
                if (!textNode && children) {
                    for (auto* obj : CCArrayExt<CCObject*>(children)) {
                        if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(obj)) {
                            textNode = lbl;
                            break;
                        }
                    }
                }
            }

            if (textNode) {
                paimon::CommentTextSelector::attach(
                    m_mainLayer,
                    fontResult.remainingText,
                    textNode,
                    fontResult.fontFile
                );
            }
        }

        std::string username = comment->m_userName;
        int accountID = comment->m_accountID;

        {
            WeakRef<BadgeCommentCell> weakSelf = this;
            std::string capturedUser = username;
            paimon::roles::RoleService::get().fetch(username,
                [weakSelf, capturedUser](paimon::roles::UserRoles roles) {
                    if (!roles.any()) return;
                    auto self = weakSelf.lock();
                    if (!self || !self->getParent() || !self->m_comment) return;
                    if (std::string(self->m_comment->m_userName) != capturedUser) return;
                    self->addRoleBadgesToComment(roles);
                });
        }

        if (accountID > 0) {
            WeakRef<BadgeCommentCell> weakSelf = this;
            CustomBadgeService::get().fetchBadge(accountID, [weakSelf, accountID](bool success, std::string const& emoteName) {
                if (!success || emoteName.empty()) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent() || !self->m_comment) return;
                if (self->m_comment->m_accountID != accountID) return;
                self->addCustomBadgeToComment(emoteName);
            });
        }
    }

    void addRoleBadgesToComment(paimon::roles::UserRoles const& roles) {
        auto menu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("username-menu"));
        if (!menu) return;
        auto* percentage = this->getChildByIDRecursive("percentage-label");
        paimon::badges::applyRoleBadges(
            menu, roles, this,
            menu_selector(BadgeCommentCell::onPaimonBadge),
            15.5f, percentage
        );
    }

    void addBadgeToComment(bool isMod, bool isAdmin) {
        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;
        
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

        float targetHeight = 15.5f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(BadgeCommentCell::onPaimonBadge)
        );
        btn->setID(badgeID);
        
        auto menuNode = typeinfo_cast<CCMenu*>(menu);
        if (!menuNode) return;

        if (auto percentage = this->getChildByIDRecursive("percentage-label")) {
            menuNode->insertBefore(btn, percentage);
        } else {
            menuNode->addChild(btn);
        }

        menuNode->updateLayout();
    }

    void addCustomBadgeToComment(std::string const& emoteName) {
        if (emoteName.empty()) return;

        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;

        if (menu->getChildByID("paimon-custom-badge"_spr)) return;

        auto emoteOpt = paimon::emotes::EmoteService::get().getEmoteByName(emoteName);
        if (!emoteOpt) return;

        auto emoteInfo = *emoteOpt;
        float targetHeight = 15.5f;

        WeakRef<BadgeCommentCell> weakSelf = this;

        paimon::emotes::EmoteCache::get().loadEmote(emoteInfo,
            [weakSelf, targetHeight, emoteName](cocos2d::CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
                if (!tex && !(isGif && !gifData.empty())) return;

                if (isGif && !gifData.empty()) {
                    auto dataCopy = gifData;
                    Loader::get()->queueInMainThread([weakSelf, targetHeight, emoteName, dataCopy = std::move(dataCopy)]() mutable {
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto self = weakSelf.lock();
                        if (!self || !self->getParent() || !self->m_comment) return;
                        AnimatedGIFSprite::createAsync(dataCopy, emoteName, [weakSelf, targetHeight](AnimatedGIFSprite* gifSpr) {
                            if (!gifSpr) return;
                            auto self = weakSelf.lock();
                            if (!self || !self->getParent() || !self->m_comment) return;
                            auto menu = typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("username-menu"));
                            if (!menu) return;
                            if (menu->getChildByID("paimon-custom-badge"_spr)) return;
                            gifSpr->stop();
                            float maxDim = std::max(gifSpr->getContentWidth(), gifSpr->getContentHeight());
                            if (maxDim > 0) gifSpr->setScale(targetHeight / maxDim);
                            auto btn = CCMenuItemSpriteExtra::create(gifSpr, self, nullptr);
                            btn->setID("paimon-custom-badge"_spr);
                            if (auto percentage = self->getChildByIDRecursive("percentage-label")) {
                                menu->insertBefore(btn, percentage);
                            } else {
                                menu->addChild(btn);
                            }
                            menu->updateLayout();
                        });
                    });
                } else {
                    Loader::get()->queueInMainThread([weakSelf, tex, targetHeight]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto self = weakSelf.lock();
                        if (!self || !self->getParent() || !self->m_comment) return;
                        auto menu = typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("username-menu"));
                        if (!menu) return;
                        if (menu->getChildByID("paimon-custom-badge"_spr)) return;
                        auto* spr = CCSprite::createWithTexture(tex);
                        if (!spr) return;
                        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
                        if (maxDim > 0) spr->setScale(targetHeight / maxDim);
                        auto btn = CCMenuItemSpriteExtra::create(spr, self, nullptr);
                        btn->setID("paimon-custom-badge"_spr);
                        if (auto percentage = self->getChildByIDRecursive("percentage-label")) {
                            menu->insertBefore(btn, percentage);
                        } else {
                            menu->addChild(btn);
                        }
                        menu->updateLayout();
                    });
                }
            });
    }

    void tryRenderEmotes(std::string const& commentText) {
        tryRenderWithFont(commentText, "chatFont.fnt");
    }

    void tryRenderWithFont(std::string const& commentText, std::string const& fontFile) {

        if (m_mainLayer->getChildByID("paimon-emote-overlay"_spr)) return;

        CCNode* targetNode = nullptr;
        cocos2d::ccColor3B textColor = {255, 255, 255};
        float maxWidth = 315.f;
        float fontSize = 1.f;
        CCPoint position = {0.f, 0.f};
        CCPoint anchorPoint = {0.f, 0.5f};

        if (auto* textArea = typeinfo_cast<TextArea*>(m_mainLayer->getChildByID("comment-text-area"))) {
            targetNode = textArea;
            position = textArea->getPosition();
            anchorPoint = textArea->getAnchorPoint();
            maxWidth = textArea->getContentSize().width * textArea->getScaleX();
            fontSize = textArea->getScale();

            if (auto* bitmapFont = textArea->m_label) {
                auto* lines = bitmapFont->m_lines;
                if (lines && lines->count() > 0) {
                    auto* firstLine = typeinfo_cast<CCLabelBMFont*>(lines->objectAtIndex(0));
                    if (firstLine) {
                        if (auto* firstChild = firstLine->getChildByType<CCSprite>(0)) {
                            textColor = firstChild->getColor();
                        }
                    }
                }
            }
        }
        else if (auto* label = typeinfo_cast<CCLabelBMFont*>(m_mainLayer->getChildByID("comment-text-label"))) {
            targetNode = label;
            position = label->getPosition();
            anchorPoint = label->getAnchorPoint();
            maxWidth = 270.f;
            fontSize = label->getScale();
            textColor = label->getColor();
        }

        if (!targetNode) return;

        // Reduce font size for long comments so emotes still fit.
        float adjustedFontSize = fontSize;
        size_t textLen = commentText.size();
        if (textLen > 80) {
            float reduction = std::min(static_cast<float>(textLen - 80) * 0.004f, 0.25f);
            adjustedFontSize = fontSize * (1.f - reduction);
        }

        bool isCustomFont = (fontFile != "chatFont.fnt");
        auto emoteNode = paimon::emotes::EmoteRenderer::renderComment(
            commentText, 0.f, maxWidth, fontFile.c_str(), adjustedFontSize, isCustomFont,
            /*animateGifs=*/false
        );
        if (!emoteNode) return;

        for (auto* child : CCArrayExt<CCNode*>(emoteNode->getChildren())) {
            if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(child)) {
                lbl->setColor(textColor);
            }
        }

        auto nodeSize = targetNode->getContentSize();
        float scX = targetNode->getScaleX();
        float scY = targetNode->getScaleY();
        float leftX = position.x - nodeSize.width * anchorPoint.x * scX;
        float topY  = position.y + nodeSize.height * (1.f - anchorPoint.y) * scY;

        emoteNode->setID("paimon-emote-overlay"_spr);
        emoteNode->setAnchorPoint({0.f, 1.f});
        emoteNode->setPosition({leftX, topY});

            if (auto* textArea = m_mainLayer->getChildByID("comment-text-area")) {
                textArea->setVisible(false);
            }
            if (auto* textLabel = m_mainLayer->getChildByID("comment-text-label")) {
                textLabel->setVisible(false);
            }
            targetNode->setVisible(false);
        m_mainLayer->addChild(emoteNode, targetNode->getZOrder() + 1);
    }
};

// Retry until emotes load, aborting if the recycled cell's text changes.
static void deferEmoteRetry(WeakRef<CommentCell> weakSelf,
                            std::string text, std::string font, int retries) {
    paimon::scheduleMainThreadDelay(0.5f,
        [weakSelf, text = std::move(text), font = std::move(font), retries]() {
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;

            auto* commentCell = static_cast<BadgeCommentCell*>(self.data());
            if (!commentCell->m_comment) return;
            std::string currentText = commentCell->m_comment->m_commentString;
            auto currentParse = paimon::fonts::parseFontTag(currentText);
            if (currentParse.remainingText != text) {
                return;
            }

            if (paimon::emotes::EmoteService::get().isLoaded()) {
                commentCell->tryRenderWithFont(text, font);
                return;
            }

            if (retries > 1) {
                deferEmoteRetry(weakSelf, text, font, retries - 1);
            }
        });
}

// ProfilePage owns the merged badge hook.
