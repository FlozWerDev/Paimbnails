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
#include "../utils/ScissorClipNode.hpp"

using namespace geode::prelude;
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../framework/HookConventions.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../features/scorecell/ScoreCellSettings.hpp"
#include "../features/scorecell/ScoreCellRefresh.hpp"
#include "../features/scorecell/fx/ScoreCellHoverWatcher.hpp"
#include "../features/profiles/services/ProfileGradientEffects.hpp"
#include <Geode/binding/GameManager.hpp>

using namespace Shaders;

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
        paimon::hooks::afterNodeIdsOrLate(self, "GJScoreCell::loadFromScore");
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
        bool m_buttonsMoved = false; // avoid moving buttons repeatedly
        Ref<geode::LoadingSpinner> m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false; // don't touch cells that are being destroyed
        Ref<CCNode> m_iconGradient = nullptr; // icon-color gradient background (paimon FX)
        Ref<paimon::scorecell::ScoreCellHoverWatcher> m_hoverWatcher = nullptr;
    };
    
    void showLoadingSpinner() {
        auto f = m_fields.self();
        
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
        
        // 10px diameter ~ loadingCircle.png * 0.25
        auto spinner = geode::LoadingSpinner::create(10.f);
        
        // place it on the right where the thumbnail goes
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

    void pushGameColorLayersBehind(CCNode* node, int maxDepth = 3) {
        if (!node || maxDepth <= 0) return;
        std::string_view id = node->getID();
        if (!id.empty() && id.starts_with("paimon-")) return;

        bool isBackground = false;
        if (geode::cast::typeinfo_cast<CCLayerColor*>(node) != nullptr) isBackground = true;
        else if (geode::cast::typeinfo_cast<CCScale9Sprite*>(node) != nullptr) isBackground = true;

        if (isBackground) {
            if (node->getZOrder() > -20) {
                if (auto parent = node->getParent()) parent->reorderChild(node, -20);
                else node->setZOrder(-20);
            }
        }
        auto children = CCArrayExt<CCNode*>(node->getChildren());
        for (auto* ch : children) pushGameColorLayersBehind(ch, maxDepth - 1);
    }

public:
    // Icon-color gradient background built from this score's own icon colors
    // (GJUserScore::m_color1 / m_color2). Clipped to the cell, sits behind
    // everything. Idempotent: removes any previous gradient first.
    void addIconGradientBackground(CCSize cs) {
        auto f = m_fields.self();
        if (!f) return;

        if (auto old = this->getChildByID("paimon-icon-gradient-clip"_spr)) {
            old->removeFromParent();
        }
        f->m_iconGradient = nullptr;

        auto* gm = GameManager::sharedState();
        ccColor3B a{255, 255, 255};
        ccColor3B b{255, 255, 255};
        if (this->m_score && gm) {
            a = gm->colorForIdx(this->m_score->m_color1);
            b = gm->colorForIdx(this->m_score->m_color2);
        } else if (gm) {
            a = gm->colorForIdx(gm->getPlayerColor());
            b = gm->colorForIdx(gm->getPlayerColor2());
        }

        auto grad = paimon::profilebg::AnimatedGradientLayer::create(a, b);
        if (!grad) return;
        grad->setContentSize(cs);
        grad->setAnchorPoint({0.5f, 0.5f});
        grad->ignoreAnchorPointForPosition(false);
        grad->setPosition({cs.width / 2.f, cs.height / 2.f});
        grad->setOpacity(static_cast<GLubyte>(paimon::scorecell::gradientOpacity()));
        grad->setEffect(paimon::scorecell::gradientEffect(), paimon::scorecell::gradientSpeed());

        auto stencil = paimon::SpriteHelper::createRectStencil(cs.width, cs.height);
        auto clip = paimon::ScissorClipNode::create(stencil);
        if (!clip) return;
        clip->setContentSize(cs);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setPosition({0.f, 0.f});
        clip->setZOrder(-15); // above the game's flat bg (-20), behind content
        clip->setID("paimon-icon-gradient-clip"_spr);
        clip->addChild(grad);
        this->addChild(clip);
        f->m_iconGradient = clip;

        pushGameColorLayersBehind(this);
    }

    // Applies the score-cell visual FX (icon gradient + hover) according to the
    // current saved settings. Safe to call repeatedly (used both on load and on
    // a live settings refresh).
    void paimonApplyFx() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto f = m_fields.self();
        if (!f || f->m_isBeingDestroyed) return;
        if (!this->getParent()) return;

        auto cs = this->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) {
            cs.width = this->m_width;
            cs.height = this->m_height;
        }
        if (cs.width <= 1.f || cs.height <= 1.f) return;

        // Gradient background
        if (paimon::scorecell::gradientEnabled()) {
            addIconGradientBackground(cs);
        } else {
            if (auto old = this->getChildByID("paimon-icon-gradient-clip"_spr)) {
                old->removeFromParent();
            }
            f->m_iconGradient = nullptr;
        }

        // Hover: rebuild the watcher + clear any leftover overlays.
        if (auto w = this->getChildByID("paimon-hover-watcher")) w->removeFromParent();
        if (auto g = this->getChildByID("paimon-hover-glow")) g->removeFromParent();
        if (auto s = this->getChildByID("paimon-hover-shine")) s->removeFromParent();
        f->m_hoverWatcher = nullptr;

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        if (paimon::scorecell::hoverEnabled()) {
            auto watcher = paimon::scorecell::ScoreCellHoverWatcher::create(
                paimon::scorecell::normalizeHoverType(paimon::scorecell::hoverType()),
                paimon::scorecell::hoverIntensity());
            if (watcher) {
                this->addChild(watcher);
                f->m_hoverWatcher = watcher;
                // If the profile banner already exists, wire the transform target.
                if (auto clip = this->getChildByID("paimon-profile-clip"_spr)) {
                    watcher->setTransformTarget(clip, 1.f, 1.f, clip->getPosition(), 0.f);
                }
            }
        }
#endif
    }

    void addOrUpdateProfileThumb(CCTexture2D* texture) {
        // texture may be null when gifKey is set; the GIF covers it
        
        // critical: the cell must have a parent
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
            
            if (f->m_isBeingDestroyed) {
                log::debug("[GJScoreCell] Cell marked as destroyed, skipping thumbnail update");
                return;
            }
            
            log::debug("[GJScoreCell] Starting profile thumbnail update");
            
            // Collect the mod's nodes and remove them in a second pass to avoid
            // mutating the children array during iteration.
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

            auto cs = this->getContentSize();
            if (cs.width <= 0 || cs.height <= 0) {
                log::error("[GJScoreCell] Invalid cell content size: {}x{}", cs.width, cs.height);
                return;
            }
            if (cs.width <= 1.f || cs.height <= 1.f) {
                cs.width = this->m_width;
                cs.height = this->m_height;
            }

            // background logic (gradient vs blurred thumbnail)
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

            // gifKey always from config (injected from cache)
            gifKey = config.gifKey;

            if (isCurrentUser) {
                // Current user: use local config
                bgType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
                blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
                darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
            } else {
                // Other users: try config from cache
                if (config.hasConfig) {
                    bgType = config.backgroundType;
                    blurIntensity = config.blurIntensity;
                    darkness = config.darkness;
                    useGradient = config.useGradient;
                    colorA = config.colorA;
                    colorB = config.colorB;
                } else {
                    // No config: use defaults
                    bgType = "thumbnail"; // default: blurred thumbnail
                    // keep other defaults
                }
            }
            
            // Basic validation
            if (!texture && gifKey.empty()) {
                log::error("[GJScoreCell] No texture and no GIF key available for account {}", accountID);
                return;
            }

            // Force thumbnail mode when a texture/gif exists
            if (bgType == "gradient" && (texture || !gifKey.empty())) {
                bgType = "thumbnail";
            }

            // When the icon-color gradient FX is enabled, it owns the cell
            // background; skip the blurred-thumbnail background entirely.
            if (paimon::scorecell::gradientEnabled()) {
                bgType = "none";
            }

            if (bgType == "none") {
                // Keep the game background
            }
            else if (bgType == "thumbnail") {
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);

                CCNode* bgNode = nullptr;

                // Try GIF or video first
                if (!gifKey.empty()) {
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

                    // Fall back to GIF
                    if (!bgNode) {
                    auto bgGif = AnimatedGIFSprite::createFromCache(gifKey);
                    if (bgGif) {
                        // Scale to cover the area
                        float scaleX = targetSize.width / bgGif->getContentSize().width;
                        float scaleY = targetSize.height / bgGif->getContentSize().height;
                        float scale = std::max(scaleX, scaleY);

                        bgGif->setScale(scale);
                        bgGif->setAnchorPoint({0.5f, 0.5f});
                        bgGif->setPosition(targetSize * 0.5f);

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
                
                // Fall back to texture
                if (!bgNode && texture) {
                    // Static image: multi-pass blur
                    CCSize blurTargetSize = cs;
                    blurTargetSize.width = std::max(blurTargetSize.width, 512.f);
                    blurTargetSize.height = std::max(blurTargetSize.height, 256.f);

                    float stronger = std::min(10.0f, blurIntensity + 3.0f);
                    auto blurredBg = BlurSystem::getInstance()->createBlurredSprite(texture, blurTargetSize, stronger);
                    if (blurredBg) {
                        blurredBg->setPosition(blurTargetSize * 0.5f);
                        bgNode = blurredBg;
                    } else {
                        // Last resort: textured sprite with shader
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
                    auto stencil = paimon::SpriteHelper::createRectStencil(cs.width, cs.height);
                    
                    auto clipper = paimon::ScissorClipNode::create(stencil);
                    clipper->setContentSize(cs);
                    clipper->setPosition({0,0});
                    clipper->setZOrder(-2);
                    clipper->setID("paimon-score-bg-clipper"_spr);

                    // Scale bgNode to cell size
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

                    // Dark overlay on top
                    if (darkness > 0.0f) {
                        auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(darkness * 255)});
                        overlay->setContentSize(cs);
                        overlay->setPosition({0, 0});
                        overlay->setZOrder(-1); 
                        this->addChild(overlay);
                        f->m_darkOverlay = overlay;
                    }

                    pushGameColorLayersBehind(this);
                }
            }

            // Main sprite logic
            CCNode* mainNode = nullptr;
            float contentW = 0, contentH = 0;

            // Try video first
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

            // then try GIF
            if (!mainNode && !gifKey.empty()) {
                log::debug("[GJScoreCell] Trying to create GIF sprite from cache key: {}", gifKey);

                if (AnimatedGIFSprite::isCached(gifKey)) {
                    log::debug("[GJScoreCell] GIF is cached, creating sprite...");
                    auto gifSprite = AnimatedGIFSprite::createFromCache(gifKey);
                    if (gifSprite) {
                        mainNode = gifSprite;
                        contentW = gifSprite->getContentSize().width;
                        contentH = gifSprite->getContentSize().height;

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
            
            // No GIF: use texture
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

        // Make sure the cell still exists
        if (!this->getParent()) {
            log::warn("[GJScoreCell] Cell was destroyed before thumbnail could be added");
            return;
        }

        
        log::debug("[GJScoreCell] Cell size: {}x{}", cs.width, cs.height);

            float factor = 0.80f;
            
            if (isCurrentUser) {
                factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f);
            } else {
                int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
                auto config = ProfileThumbs::get().getProfileConfig(accountID);
                if (config.hasConfig) {
                    factor = config.widthFactor;
                } else {
                    factor = 0.60f; 
                }
            }
            
            factor = std::max(0.30f, std::min(0.95f, factor));
            float desiredWidth = cs.width * factor;

            float scaleY = cs.height / contentH;
            float scaleX = desiredWidth / contentW;

            mainNode->setScaleY(scaleY);
            mainNode->setScaleX(scaleX);

        // Angled cut on the right side
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
        // Pinned to the right edge with offset
        clip->setPosition({ cs.width, 0.3f });
        clip->setID("paimon-profile-clip"_spr);
    // Behind text/icons
    clip->setZOrder(-1);

        mainNode->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(mainNode);
        
        this->addChild(clip);
        f->m_profileClip = clip;

        // FX: one-shot entrance animation + register the hover transform target
        // so scale/lift/tilt hovers act on the banner.
        {
            CCPoint clipPos = clip->getPosition();
            paimon::scorecell::applyEntrance(
                clip, paimon::scorecell::entranceType(), clipPos, 1.f, 1.f);
            if (f->m_hoverWatcher) {
                f->m_hoverWatcher->setTransformTarget(clip, 1.f, 1.f, clipPos, 0.f);
            }
        }
        
        bool isPremiumUser = false;

        // Borders around the thumbnail
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

    // separator behind the image (fixed style)
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

        // background already added above
        log::debug("[GJScoreCell] Profile thumbnail added successfully");
    }

    $override void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        
        // push the game's color layers behind so the gradient shows
        pushGameColorLayersBehind(this);

        if (!score) return;
        log::info("[GJScoreCell] loadFromScore: accountID={} user={}", score->m_accountID, std::string(score->m_userName));

            // Apply the score-cell visual FX (icon gradient + hover) based on settings.
            paimonApplyFx();

            int accountID = score->m_accountID;
            if (accountID <= 0) return;

            {
                std::string username = score->m_userName;
                if (username.empty()) {
                    log::warn("[GJScoreCell] Username empty for account {}", accountID);
                    return;
                }
                
                // check cache first
                auto cachedProfile = ProfileThumbs::get().getCachedProfile(accountID);
                bool wantsGifProfile = cachedProfile.has_value() && !cachedProfile->gifKey.empty();
                bool hasReadyCachedGif = wantsGifProfile && AnimatedGIFSprite::isCached(cachedProfile->gifKey);
                bool hasReadyCachedProfile = cachedProfile.has_value() && (hasReadyCachedGif || (!wantsGifProfile && cachedProfile->texture));
                if (hasReadyCachedProfile) {
                    log::debug("[GJScoreCell] Found cached profile for account {}", accountID);
                    // load from cache asynchronously
                    WeakRef<PaimonGJScoreCell> safeThis = this;
                    Loader::get()->queueInMainThread([safeThis, accountID]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto selfRef = safeThis.lock();
                        auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                        if (!self || !self->getParent()) return;

                        auto cached = ProfileThumbs::get().getCachedProfile(accountID);
                        if (cached.has_value()) {
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
                
                // not cached: download from the server
                log::debug("[GJScoreCell] Profile not in cache for user: {} - Downloading...", username);
                
                bool enableSpinners = true;
                
                if (enableSpinners) {
                    showLoadingSpinner();
                }
                
                WeakRef<PaimonGJScoreCell> safeRef = this;

                // use queueLoad instead of downloading directly
                ProfileThumbs::get().queueLoad(accountID, username, [safeRef, accountID, enableSpinners](bool success, CCTexture2D* texture) {
                    auto selfRef = safeRef.lock();
                    auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                    if (!self) return;

                    if (!success) {
                        if (enableSpinners) self->hideLoadingSpinner();
                        log::warn("[GJScoreCell] Failed to download profile for account {}", accountID);
                        return;
                    }

                    // GIF profiles: texture may be null but the GIF is already
                    // cached via cacheProfileGIF; let it continue so
                    // addOrUpdateProfileThumb detects it via gifKey.
                    if (!texture) {
                        auto cachedEntry = ProfileThumbs::get().getCachedProfile(accountID);
                        if (!cachedEntry.has_value() || cachedEntry->gifKey.empty()) {
                            if (enableSpinners) self->hideLoadingSpinner();
                            log::warn("[GJScoreCell] No texture and no GIF for account {}", accountID);
                            return;
                        }
                        // GIF-only profile: continue without texture
                    }

                    // Ref<> so it isn't autoreleased before the next async call
                    Ref<CCTexture2D> safeTex = texture;

                    ThumbnailAPI::get().downloadProfileConfig(accountID, [safeRef, accountID, safeTex, enableSpinners](bool success2, ProfileConfig const& config) {
                        auto selfRef = safeRef.lock();
                        auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                        if (!self) return;
                        if (enableSpinners) self->hideLoadingSpinner();

                        if (safeTex) {
                            ProfileThumbs::get().cacheProfile(accountID, safeTex, {255,255,255}, {255,255,255}, 0.5f);
                        }
                        if (success2) {
                            ProfileThumbs::get().cacheProfileConfig(accountID, config);
                        }

                        self->addOrUpdateProfileThumb(safeTex);
                    });
                });
            }

        // move the player's profile button (using that cache)
        auto f = m_fields.self();
        if (!f->m_buttonsMoved) {
            f->m_buttonsMoved = true;
            
                if (!g_buttonCache.initialized) {
                        g_buttonCache.buttonOffset = 0.0f;
                    g_buttonCache.initialized = true;
                    log::debug("[GJScoreCell] Button cache initialized with offset: {}", g_buttonCache.buttonOffset);
                }
                
                // offset 0: nothing to do
                if (g_buttonCache.buttonOffset <= 0.01f) {
                    return;
                }
                
                // search and move buttons among direct children only
                auto children = this->getChildren();
                if (!children) return;
                
                bool foundButton = false;
                
                // limit search to the first 10 nodes (menus are usually near the start)
                int searchCount = 0;

                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if (foundButton || searchCount >= 10) break;
                    searchCount++;

                    auto menu = typeinfo_cast<CCMenu*>(child);
                    if (!menu) continue;

                    auto menuChildren = menu->getChildren();
                    if (!menuChildren) continue;
                    
                    // only check the first 5 menu items
                    int menuSearchCount = 0;

                    for (auto* menuChild : CCArrayExt<CCNode*>(menuChildren)) {
                        if (foundButton || menuSearchCount >= 5) break;
                        menuSearchCount++;

                        auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(menuChild);
                        if (!btn) continue;
                        
                        auto btnID = btn->getID();
                        
                        // skip the mod's own buttons (prefix check)
                        std::string btnIDStr = btnID;
                        if (btnIDStr.empty() || btnIDStr.compare(0, 7, "paimon-") != 0) {
                            auto currentPos = btn->getPosition();
                            
                            // move only if it's at a reasonable position
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

        // ModeratorsLayer: hide rank-label in moderator-list cells (unified here to avoid a second $modify on GJScoreCell)
        if (ModeratorsLayer::s_instance && ModeratorsLayer::s_instance->isScoreInList(score)) {
            WeakRef<PaimonGJScoreCell> self = this;
            Loader::get()->queueInMainThread([self]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto cellRef = self.lock();
                auto* cell = static_cast<PaimonGJScoreCell*>(cellRef.data());
                if (!cell) return;
                if (auto rankLabel = cell->getChildByID("rank-label")) {
                    rankLabel->setVisible(false);
                }
            });
        }
    }

    // draw hook removed to avoid crashes
};

namespace paimon::scorecell {

namespace {
    void applyFxRecursive(cocos2d::CCNode* node) {
        if (!node) return;
        if (auto cell = geode::cast::typeinfo_cast<GJScoreCell*>(node)) {
            static_cast<PaimonGJScoreCell*>(cell)->paimonApplyFx();
            return; // no score cells nested inside a score cell
        }
        if (auto children = node->getChildren()) {
            for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(children)) {
                applyFxRecursive(child);
            }
        }
    }
}

void refreshAllCells() {
    if (paimon::isRuntimeShuttingDown()) return;
    auto scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    applyFxRecursive(scene);
}

} // namespace paimon::scorecell
