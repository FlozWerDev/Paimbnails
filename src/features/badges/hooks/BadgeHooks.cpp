#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/binding/MultilineBitmapFont.hpp>
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../thumbnails/services/ThumbnailTransportClient.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
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
#include "../../../utils/Shaders.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../profiles/services/CustomBadgeService.hpp"

using namespace geode::prelude;

// === Compatibilidad con BadgeCache.hpp (legacy) ===
// Las funciones libres se mantienen como wrappers que delegan a ModeratorCache,
// para que ProfilePage.cpp y cualquier otro consumidor sigan compilando
// sin necesidad de reescritura inmediata.

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

// funcion compartida para mostrar info del badge, accesible desde ProfilePage.cpp
void showBadgeInfoPopup(CCNode* sender) {
    if (!sender) return;

    std::string title = "Unknown Rank";
    std::string desc = "No description available.";
    
    if (sender->getID() == "paimon-admin-badge"_spr) {
        title = "Paimbnails Admin";
        desc = "A <cj>Paimbnails Admin</c> is a developer or manager of the <cg>Paimbnails</c> mod. They have full control over the mod's infrastructure and content.";
    } else if (sender->getID() == "paimon-moderator-badge"_spr) {
        title = "Paimbnails Moderator";
        desc = "A <cj>Paimbnails Moderator</c> is a trusted user who helps review and manage thumbnails for the <cg>Paimbnails</c> mod. They ensure that content follows the guidelines.";
    }
    
    FLAlertLayer::create(title.c_str(), desc.c_str(), "OK")->show();
}

// ── Deferred emote retry ──
// When a CommentCell loads before the emote catalog is ready, we poll
// until the catalog is available and then re-render the emotes.
// Forward-declared — implementation after $modify (needs BadgeCommentCell definition).
static void deferEmoteRetry(WeakRef<CommentCell> weakSelf,
                            std::string text, std::string font, int retries);

namespace {
constexpr GLubyte kCommentDarkPanelOpacity = 92;
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
        (void)self.setHookPriorityAfterPost("CommentCell::loadFromComment", "geode.node-ids");
    }

    struct Fields {
        Ref<CCDrawNode> m_commentBgPanel = nullptr;
        Ref<CCClippingNode> m_commentBgClip = nullptr;
        Ref<CCLayerColor> m_commentBgDarkOverlay = nullptr;
        int m_commentBgToken = 0;
        int m_commentBgAccountID = 0;
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

            // Skip retry if custom bg is already installed (clip = image bg, panel = solid bg)
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
        // since the config may have changed between retries
        bool hasCustomBg = false;
        ProfileConfig config;
        if (m_comment && m_comment->m_accountID > 0) {
            config = ProfileThumbs::get().getProfileConfig(m_comment->m_accountID);
            hasCustomBg = (config.commentBgType != "none" && config.commentBgType != "");
        }

        // Early return only if panel exists, layout unchanged, AND no custom bg
        if (m_fields->m_commentBgPanel && !hasCustomBg) {
            auto oldSize = m_fields->m_commentBgPanel->getContentSize();
            auto oldPos = m_fields->m_commentBgPanel->getPosition();
            if (std::abs(oldSize.width - layout.size.width) < 0.5f &&
                std::abs(oldSize.height - layout.size.height) < 0.5f &&
                std::abs(oldPos.x - layout.origin.x) < 0.5f &&
                std::abs(oldPos.y - layout.origin.y) < 0.5f) {
                hideVanillaCommentBackgrounds();
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
            // Default dark panel
            auto* panel = paimon::SpriteHelper::createDarkPanel(
                layout.size.width,
                layout.size.height,
                kCommentDarkPanelOpacity,
                layout.radius
            );
            if (!panel) return;

            panel->setPosition(layout.origin);
            panel->setZOrder(-12);
            panel->setID("paimon-comment-bg-panel"_spr);
            this->addChild(panel);
            m_fields->m_commentBgPanel = panel;
        }
    }

    void installCustomCommentBackground(CommentBackgroundLayout const& layout, ProfileConfig const& config) {
        auto cellSize = this->getContentSize();

        if (config.commentBgType == "solid") {
            // Solid color background — bake color directly into vertices
            // (createDarkPanel is black; setColor on black = black, so use createColorPanel)
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
        }
        else if (config.commentBgType == "thumbnail" || config.commentBgType == "banner") {
            // Image-based background — load async then render
            int accountID = m_comment ? m_comment->m_accountID : 0;
            int token = m_fields->m_commentBgToken;

            if (config.commentBgType == "thumbnail" && !config.commentBgThumbnailId.empty()) {
                // Load thumbnail by level ID (supports multi-thumbnail via position)
                int levelId = 0;
                auto levelIdRes = geode::utils::numFromString<int>(config.commentBgThumbnailId);
                if (!levelIdRes) return;
                levelId = levelIdRes.unwrap();
                int targetPos = config.commentBgThumbnailPos;

                WeakRef<BadgeCommentCell> weakSelf = this;

                // Use getThumbnails to support position selection
                ThumbnailAPI::get().getThumbnails(levelId,
                    [weakSelf, token, accountID, layout, config, cellSize, targetPos](bool success, std::vector<ThumbnailInfo> const& thumbs) {
                        Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, success, thumbs, targetPos]() {
                            auto selfRef = weakSelf.lock();
                            auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
                            if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) return;
                            if (success && !thumbs.empty()) {
                                // Find the thumbnail at the target position
                                int idx = std::clamp(targetPos, 1, static_cast<int>(thumbs.size())) - 1;
                                auto& thumb = thumbs[idx];

                                if (!thumb.url.empty()) {
                                    HttpClient::get().downloadFromUrl(thumb.url,
                                        [weakSelf, token, accountID, layout, config, cellSize](bool ok, std::vector<uint8_t> const& data, int, int) {
                                            Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, ok, data]() {
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
                    // Load profile image (profileimg) as background
                    WeakRef<BadgeCommentCell> weakSelf = this;
                    ThumbnailAPI::get().downloadProfileImg(accountID,
                        [weakSelf, token, accountID, layout, config, cellSize](bool success, CCTexture2D* texture) {
                            Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, success, texture]() {
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
                    // Default: load profile background from cache
                    auto& thumbs = ProfileThumbs::get();
                    if (auto* cached = thumbs.getCachedProfile(accountID)) {
                        if (cached->texture) {
                            installImageCommentBackground(layout, cached->texture.data(), config, cellSize);
                        } else if (!cached->gifKey.empty()) {
                            installGifCommentBackground(layout, cached->gifKey, config, cellSize);
                        }
                        return;
                    }

                    // Not cached — queue load
                    std::string username = m_comment ? m_comment->m_userName : "";
                    WeakRef<BadgeCommentCell> weakSelf = this;
                    thumbs.queueLoad(accountID, username,
                        [weakSelf, token, accountID, layout, config, cellSize](bool success, CCTexture2D*) {
                            Loader::get()->queueInMainThread([weakSelf, token, accountID, layout, config, cellSize, success]() {
                                auto selfRef = weakSelf.lock();
                                auto* self = static_cast<BadgeCommentCell*>(selfRef.data());
                                if (!self || !self->shouldHandleCommentProfileResult(token, accountID)) return;
                                if (success) {
                                    auto* cached = ProfileThumbs::get().getCachedProfile(accountID);
                                    if (cached) {
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

        // Remove existing custom background nodes
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

        // Create blurred background sprite
        // Use smaller blur buffer — comment cells are small, blur is low-freq
        CCSize targetSize = layout.size;
        targetSize.width = std::max(targetSize.width, 256.f);
        targetSize.height = std::max(targetSize.height, 128.f);

        float blurIntensity = config.commentBgBlur;
        CCNode* bgNode = nullptr;

        bool usePaimonBlur = (config.commentBgBlurType == "paimon");
        auto blurredBg = usePaimonBlur
            ? BlurSystem::getInstance()->createPaimonBlurSprite(texture, targetSize, blurIntensity)
            : BlurSystem::getInstance()->createBlurredSprite(texture, targetSize, blurIntensity);
        if (blurredBg) {
            blurredBg->setPosition(targetSize * 0.5f);
            bgNode = blurredBg;
        } else {
            // Fallback: use shader blur
            auto tempSprite = CCSprite::createWithTexture(texture);
            if (!tempSprite) return;
            float scaleX = targetSize.width / texture->getContentSize().width;
            float scaleY = targetSize.height / texture->getContentSize().height;
            tempSprite->setScale(std::max(scaleX, scaleY));
            tempSprite->setAnchorPoint({0.5f, 0.5f});
            tempSprite->setPosition(targetSize * 0.5f);

            auto shader = Shaders::getBlurCellShader();
            if (shader) tempSprite->setShaderProgram(shader);
            bgNode = tempSprite;
        }

        if (!bgNode) return;

        // Scale bgNode to fill the layout area
        CCSize bgSize = bgNode->getContentSize();
        if (bgSize.width > 0 && bgSize.height > 0) {
            float scaleToFitX = layout.size.width / bgSize.width;
            float scaleToFitY = layout.size.height / bgSize.height;
            bgNode->setScale(std::max(scaleToFitX, scaleToFitY));
        }
        bgNode->setAnchorPoint({0.5f, 0.5f});
        bgNode->setPosition(layout.size / 2);

        // Create clipping node with rounded-rect stencil
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(layout.size.width, layout.size.height, layout.radius);
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(layout.size);
        clipper->setPosition(layout.origin);
        clipper->setZOrder(-13);
        clipper->setID("paimon-comment-bg-clip"_spr);
        clipper->addChild(bgNode);
        this->addChild(clipper);
        m_fields->m_commentBgClip = clipper;

        // Dark overlay for readability
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

    void installGifCommentBackground(CommentBackgroundLayout const& layout, std::string const& gifKey, ProfileConfig const& config, CCSize const& cellSize) {
        if (gifKey.empty()) return;

        // Remove existing custom background nodes
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

        // Check for video first
        if (VideoThumbnailSprite::isCached(gifKey)) {
            auto bgVideo = VideoThumbnailSprite::createFromCache(gifKey);
            if (bgVideo) {
                float scaleX = layout.size.width / std::max(1.f, bgVideo->getContentSize().width);
                float scaleY = layout.size.height / std::max(1.f, bgVideo->getContentSize().height);
                bgVideo->setScale(std::max(scaleX, scaleY));
                bgVideo->setAnchorPoint({0.5f, 0.5f});
                bgVideo->setPosition(layout.size * 0.5f);

                auto shader = Shaders::getBlurCellShader();
                if (shader) bgVideo->setShaderProgram(shader);

                bgVideo->play();
                bgNode = bgVideo;
            }
        }

        // Fallback to animated GIF
        if (!bgNode) {
            auto bgGif = AnimatedGIFSprite::createFromCache(gifKey);
            if (bgGif) {
                float scaleX = layout.size.width / bgGif->getContentSize().width;
                float scaleY = layout.size.height / bgGif->getContentSize().height;
                bgGif->setScale(std::max(scaleX, scaleY));
                bgGif->setAnchorPoint({0.5f, 0.5f});
                bgGif->setPosition(layout.size * 0.5f);

                float norm = (config.commentBgBlur - 1.0f) / 9.0f;
                bgGif->m_intensity = std::min(1.7f, norm * 2.5f);
                if (bgGif->getTexture()) {
                    bgGif->m_texSize = bgGif->getTexture()->getContentSizeInPixels();
                }

                auto shader = Shaders::getBlurCellShader();
                if (shader) bgGif->setShaderProgram(shader);

                bgGif->play();
                bgNode = bgGif;
            }
        }

        if (!bgNode) return;

        // Create clipping node with rounded-rect stencil
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(layout.size.width, layout.size.height, layout.radius);
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(layout.size);
        clipper->setPosition(layout.origin);
        clipper->setZOrder(-13);
        clipper->setID("paimon-comment-bg-clip"_spr);
        clipper->addChild(bgNode);
        this->addChild(clipper);
        m_fields->m_commentBgClip = clipper;

        // Dark overlay
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

        if (auto* cached = thumbs.getCachedProfile(accountID)) {
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
        clearCommentProfileBackground();
        CommentCell::onExit();
    }

    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    $override
    void loadFromComment(GJComment* comment) {
        clearCommentProfileBackground();
        CommentCell::loadFromComment(comment);
        
        if (!comment) return;

        installDarkCommentPanel();
        scheduleCommentPanelRefresh(m_fields->m_commentBgToken, 3);

        // ── Font tag + emote rendering in all comments ──
        {
            std::string commentText = comment->m_commentString;
            auto fontResult = paimon::fonts::parseFontTag(commentText);
            bool serviceLoaded = paimon::emotes::EmoteService::get().isLoaded();
            bool hasEmoteSyntax = paimon::emotes::EmoteRenderer::hasEmoteSyntax(fontResult.remainingText);
            bool hasEmotes = serviceLoaded && hasEmoteSyntax;

            if (fontResult.hasTag || hasEmotes) {
                this->tryRenderWithFont(fontResult.remainingText, fontResult.fontFile);
            } else if (!serviceLoaded && hasEmoteSyntax) {
                // Emote catalog not loaded yet — schedule deferred retry
                WeakRef<CommentCell> weakSelf = static_cast<CommentCell*>(this);
                deferEmoteRetry(weakSelf, fontResult.remainingText, fontResult.fontFile, 10);
            }
        }

        // ── Text selection: attach selector for copy support ──
        {
            std::string commentText = comment->m_commentString;
            auto fontResult = paimon::fonts::parseFontTag(commentText);
            CCNode* textNode = m_mainLayer->getChildByID("comment-text-area");
            if (!textNode) textNode = m_mainLayer->getChildByID("comment-text-label");
            if (!textNode) textNode = m_mainLayer->getChildByID("paimon-emote-overlay"_spr);

            // Fallback para comentarios de perfil donde los IDs de Geode no se asignan
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

        // Mod/admin badge — from cache or network
        bool isMod = false;
        bool isAdmin = false;
        if (moderatorCacheGet(username, isMod, isAdmin)) {
            if (isMod || isAdmin) {
                this->addBadgeToComment(isMod, isAdmin);
            }
            // fall through to also fetch custom badge below
        } else {
            // Fetch mod status from server
            WeakRef<BadgeCommentCell> weakSelf = this;
            ThumbnailAPI::get().checkUserStatus(username, [weakSelf, username](bool isMod, bool isAdmin) {
                moderatorCacheInsert(username, isMod, isAdmin);
                Loader::get()->queueInMainThread([weakSelf, username, isMod, isAdmin]() {
                    auto self = weakSelf.lock();
                    if (!self || !self->getParent() || !self->m_comment) return;
                    if (std::string(self->m_comment->m_userName) != username) return;
                    if (isMod || isAdmin) {
                        self->addBadgeToComment(isMod, isAdmin);
                    }
                });
            });
        }

        // Custom emote badge — always fetch (independent of mod status)
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

    void addBadgeToComment(bool isMod, bool isAdmin) {
        // busco el menu del username
        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;
        
        // si ya existe no lo duplico
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

        // lo escalo para que no sea muy grande
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

        // lo meto antes del porcentaje si existe
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

        // avoid duplicates
        if (menu->getChildByID("paimon-custom-badge"_spr)) return;

        // find the emote info
        auto emoteOpt = paimon::emotes::EmoteService::get().getEmoteByName(emoteName);
        if (!emoteOpt) return;

        auto emoteInfo = *emoteOpt;
        float targetHeight = 15.5f;

        WeakRef<BadgeCommentCell> weakSelf = this;

        paimon::emotes::EmoteCache::get().loadEmote(emoteInfo,
            [weakSelf, targetHeight, emoteName](cocos2d::CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
                if (!tex && !(isGif && !gifData.empty())) return;

                if (isGif && !gifData.empty()) {
                    // GIF badge: dispatch to main thread then create animated sprite
                    auto dataCopy = gifData;
                    Loader::get()->queueInMainThread([weakSelf, targetHeight, emoteName, dataCopy = std::move(dataCopy)]() mutable {
                        auto self = weakSelf.lock();
                        if (!self || !self->getParent() || !self->m_comment) return;
                        AnimatedGIFSprite::createAsync(dataCopy, emoteName, [weakSelf, targetHeight](AnimatedGIFSprite* gifSpr) {
                            if (!gifSpr) return;
                            auto self = weakSelf.lock();
                            if (!self || !self->getParent() || !self->m_comment) return;
                            auto menu = typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("username-menu"));
                            if (!menu) return;
                            if (menu->getChildByID("paimon-custom-badge"_spr)) return;
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
                    // Static badge
                    Loader::get()->queueInMainThread([weakSelf, tex, targetHeight]() {
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
        // Use geode node-ids to find the comment text elements reliably.
        // GD uses TextArea for multi-line comments and CCLabelBMFont for single-line.
        // The hook runs after geode.node-ids assigns IDs.

        // Already processed?
        if (m_mainLayer->getChildByID("paimon-emote-overlay"_spr)) return;

        CCNode* targetNode = nullptr;
        cocos2d::ccColor3B textColor = {255, 255, 255};
        float maxWidth = 315.f;
        float fontSize = 1.f;
        CCPoint position = {0.f, 0.f};
        CCPoint anchorPoint = {0.f, 0.5f};

        // Try TextArea first (multi-line comments)
        if (auto* textArea = typeinfo_cast<TextArea*>(m_mainLayer->getChildByID("comment-text-area"))) {
            targetNode = textArea;
            position = textArea->getPosition();
            anchorPoint = textArea->getAnchorPoint();
            maxWidth = textArea->getContentSize().width * textArea->getScaleX();
            fontSize = textArea->getScale();

            // Extract color from the TextArea's label children
            if (auto* bitmapFont = textArea->m_label) {
                auto* lines = bitmapFont->m_lines;
                if (lines && lines->count() > 0) {
                    auto* firstLine = static_cast<CCLabelBMFont*>(lines->objectAtIndex(0));
                    if (firstLine && firstLine->getChildren() && firstLine->getChildren()->count() > 0) {
                        textColor = static_cast<CCSprite*>(firstLine->getChildren()->objectAtIndex(0))->getColor();
                    }
                }
            }
        }
        // Try CCLabelBMFont (single-line labels)
        else if (auto* label = typeinfo_cast<CCLabelBMFont*>(m_mainLayer->getChildByID("comment-text-label"))) {
            targetNode = label;
            position = label->getPosition();
            anchorPoint = label->getAnchorPoint();
            maxWidth = 270.f;
            fontSize = label->getScale();
            textColor = label->getColor();
        }

        if (!targetNode) return;

        // Adaptive font scaling for long comments (inspired by Comment Emojis Reloaded)
        // Gradually reduce font size for comments >80 chars to fit more text.
        float adjustedFontSize = fontSize;
        size_t textLen = commentText.size();
        if (textLen > 80) {
            float reduction = std::min(static_cast<float>(textLen - 80) * 0.004f, 0.25f);
            adjustedFontSize = fontSize * (1.f - reduction);
        }

        // Use emoteSize=0 (auto-detect from font metrics) so emotes match text height.
        // forceRender=true when using a custom font so the renderer always produces a
        // properly word-wrapped node even when there are no emote tokens.
        bool isCustomFont = (fontFile != "chatFont.fnt");
        auto emoteNode = paimon::emotes::EmoteRenderer::renderComment(
            commentText, 0.f, maxWidth, fontFile.c_str(), adjustedFontSize, isCustomFont
        );
        if (!emoteNode) return;

        // Apply the original text color to all text labels in the emote node
        for (auto* child : CCArrayExt<CCNode*>(emoteNode->getChildren())) {
            if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(child)) {
                lbl->setColor(textColor);
            }
        }

        // Compute the top-left corner of the original text node in parent coords.
        // This gives us a fixed Y reference from GD's native layout, ensuring
        // all custom fonts start at the same vertical position.
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

// ── Deferred emote retry implementation ──
// Polls every 0.5 s until the emote catalog loads (up to `retries` attempts).
static void deferEmoteRetry(WeakRef<CommentCell> weakSelf,
                            std::string text, std::string font, int retries) {
    paimon::scheduleMainThreadDelay(0.5f,
        [weakSelf, text = std::move(text), font = std::move(font), retries]() {
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;

            if (paimon::emotes::EmoteService::get().isLoaded()) {
                static_cast<BadgeCommentCell*>(self.data())->tryRenderWithFont(text, font);
                return;
            }

            if (retries > 1) {
                deferEmoteRetry(weakSelf, text, font, retries - 1);
            }
        });
}

// BadgeProfilePage se fusiono en PaimonProfilePage (ProfilePage.cpp)
// para evitar undefined behavior con doble $modify sobre la misma clase.

