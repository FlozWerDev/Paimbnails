#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/thumbnails/services/ThumbsRegistry.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/ScissorClipNode.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../framework/HookConventions.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../features/scorecell/ScoreCellSettings.hpp"
#include "../features/scorecell/ScoreCellRefresh.hpp"
#include "../features/scorecell/LeaderboardCellLayout.hpp"
#include "../features/scorecell/fx/ScoreCellHoverWatcher.hpp"
#include "../features/scorecell/fx/ScoreGradientDesign.hpp"
#include "../features/scorecell/fx/ScoreGradientLayer.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include "../features/profiles/services/ProfileGradientEffects.hpp"
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;
using namespace Shaders;

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
        Ref<geode::LoadingSpinner> m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false; // don't touch cells being destroyed
        Ref<CCNode> m_iconGradient = nullptr;
        Ref<paimon::scorecell::ScoreCellHoverWatcher> m_hoverWatcher = nullptr;
    };
    
    void showLoadingSpinner() {
        auto f = m_fields.self();
        
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
        
        auto spinner = geode::LoadingSpinner::create(10.f);
        
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
        // note: match with find(), not starts_with(): ids set via "_spr"
        // expand to "<mod-id>/paimon-...", so a prefix check never matches.
        if (!id.empty() && id.find("paimon-") != std::string_view::npos) return;

        bool isBackground = geode::cast::typeinfo_cast<CCLayerColor*>(node) != nullptr ||
            geode::cast::typeinfo_cast<CCScale9Sprite*>(node) != nullptr;

        if (isBackground && node->getZOrder() > -20) {
            if (auto parent = node->getParent()) parent->reorderChild(node, -20);
            else node->setZOrder(-20);
        }
        auto children = CCArrayExt<CCNode*>(node->getChildren());
        for (auto* ch : children) pushGameColorLayersBehind(ch, maxDepth - 1);
    }

public:
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
        // harmonize once so neon pairs don't burn and lights don't wash out
        {
            auto tuned = paimon::scorecell::detail::harmonizePair(a, b);
            a = tuned.first;
            b = tuned.second;
        }

        auto grad = paimon::profilebg::AnimatedGradientLayer::create(a, b);
        if (!grad) return;
        grad->setContentSize(cs);
        grad->setAnchorPoint({0.5f, 0.5f});
        grad->ignoreAnchorPointForPosition(false);
        grad->setPosition({cs.width / 2.f, cs.height / 2.f});
        grad->setOpacity(static_cast<GLubyte>(paimon::scorecell::gradientOpacity()));
        grad->setEffect(paimon::scorecell::gradientEffect(), paimon::scorecell::gradientSpeed());

        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(cs.width, cs.height, 7.f);
        auto clip = cocos2d::CCClippingNode::create(stencil);
        if (!clip) return;
        clip->setContentSize(cs);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setPosition({0.f, 0.f});
        clip->setAlphaThreshold(0.05f);
        clip->setZOrder(-15); // above the game's flat bg (-20), behind content
        clip->setID("paimon-icon-gradient-clip"_spr);
        clip->addChild(grad);
        // dark scrim keeps text readable over saturated pairs
        paimon::scorecell::attachCellOverlays(clip, cs);
        this->addChild(clip);
        f->m_iconGradient = clip;

        pushGameColorLayersBehind(this);
    }

    // safe on load and live settings refresh
    void paimonApplyFx() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto f = m_fields.self();
        if (!f || f->m_isBeingDestroyed) return;
        auto cs = this->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) {
            cs.width = this->m_width;
            cs.height = this->m_height;
        }
        if (cs.width <= 1.f || cs.height <= 1.f) return;

        if (auto old = getChildByID("paimon-score-gradient"_spr)) old->removeFromParent();
        if (auto oldClip = getChildByID("paimon-score-gradient-clip"_spr)) oldClip->removeFromParent();
        bool scoreGradient = paimon::scorecell::scoreGradientEnabled();
        if (scoreGradient && m_score) {
            auto* gm = GameManager::sharedState();
            if (auto* gradient = paimon::scorecell::ScoreGradientLayer::create(
                    cs, gm->colorForIdx(m_score->m_color1), gm->colorForIdx(m_score->m_color2))) {
                gradient->setAnchorPoint({0.f, 0.f});
                gradient->setPosition({0.f, 0.f});
                gradient->setBaseOpacity(static_cast<GLubyte>(paimon::scorecell::gradientOpacity()));
                gradient->setIdleSpeed(paimon::scorecell::gradientSpeed());
                if (auto* clip = paimon::scorecell::makeScoreGradientClip(gradient, cs)) {
                    clip->setZOrder(-15);
                    clip->setID("paimon-score-gradient-clip"_spr);
                    addChild(clip);
                    // legacy id so refresh logic finds the layer
                    gradient->setID("paimon-score-gradient"_spr);
                } else {
                    addChild(gradient, -15);
                }
                pushGameColorLayersBehind(this);
            }
        }
        if (f->m_profileBg) f->m_profileBg->setVisible(!scoreGradient);
        if (f->m_darkOverlay) f->m_darkOverlay->setVisible(!scoreGradient);

        if (!scoreGradient && paimon::scorecell::gradientEnabled() &&
            paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) {
            addIconGradientBackground(cs);
        } else {
            if (auto old = this->getChildByID("paimon-icon-gradient-clip"_spr)) {
                old->removeFromParent();
            }
            f->m_iconGradient = nullptr;
        }

        if (auto w = this->getChildByID("paimon-hover-watcher"_spr)) w->removeFromParent();
        if (auto g = this->getChildByID("paimon-hover-glow"_spr)) g->removeFromParent();
        if (auto s = this->getChildByID("paimon-hover-shine"_spr)) s->removeFromParent();
        f->m_hoverWatcher = nullptr;

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        if (paimon::scorecell::hoverEnabled()) {
            auto watcher = paimon::scorecell::ScoreCellHoverWatcher::create(
                paimon::scorecell::normalizeHoverType(paimon::scorecell::hoverType()),
                paimon::scorecell::hoverIntensity());
            if (watcher) {
                this->addChild(watcher);
                f->m_hoverWatcher = watcher;
                if (auto clip = this->getChildByID("paimon-profile-clip"_spr)) {
                    watcher->setTransformTarget(clip, 1.f, 1.f, clip->getPosition(), 0.f);
                }
            }
        }
#endif
    }

    void addOrUpdateProfileThumb(CCTexture2D* texture) {
        
            if (!this->getParent()) {
                log::warn("[GJScoreCell] Cell has no parent, skipping addOrUpdateProfileThumb");
                return;
            }
            

            auto f = m_fields.self();
            if (!f) {
                log::error("[GJScoreCell] Fields are null in addOrUpdateProfileThumb");
                return;
            }
            
            if (f->m_isBeingDestroyed) return;
            
            
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

            std::string bgType = "gradient";
            float blurIntensity = 3.0f;
            float darkness = 0.2f;
            ccColor3B colorA = {255,255,255};
            ccColor3B colorB = {255,255,255};
            bool useGradient = false;
            std::string gifKey = "";

            bool isCurrentUser = this->m_score && this->m_score->isCurrentUser();
            
            int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
            auto config = ProfileThumbs::get().getProfileConfig(accountID);

            gifKey = config.gifKey;

            if (isCurrentUser) {
                bgType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
                blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
                darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
            } else {
                if (config.hasConfig) {
                    bgType = config.backgroundType;
                    blurIntensity = config.blurIntensity;
                    darkness = config.darkness;
                    useGradient = config.useGradient;
                    colorA = config.colorA;
                    colorB = config.colorB;
                } else {
                    bgType = "thumbnail";
                }
            }
            
            if (!texture && gifKey.empty()) {
                log::error("[GJScoreCell] No texture and no GIF key available for account {}", accountID);
                return;
            }

            if (bgType == "gradient" && (texture || !gifKey.empty())) {
                bgType = "thumbnail";
            }

            // gradient owns the background; skip the blurred thumbnail
            if (paimon::scorecell::scoreGradientEnabled() ||
                (paimon::scorecell::gradientEnabled() &&
                 paimon::modules::isEnabled("paimbnails.leaderboardcells.browser"))) {
                bgType = "none";
            }

            if (bgType == "thumbnail") {
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);

                CCNode* bgNode = nullptr;

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

                    if (!bgNode) {
                    auto bgGif = AnimatedGIFSprite::createFromCache(gifKey);
                    if (bgGif) {
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
    }
                }
                
                if (!bgNode && texture) {
                    CCSize blurTargetSize = cs;
                    blurTargetSize.width = std::max(blurTargetSize.width, 512.f);
                    blurTargetSize.height = std::max(blurTargetSize.height, 256.f);

                    float stronger = std::min(10.0f, blurIntensity + 3.0f);
                    auto blurredBg = BlurSystem::getInstance()->createBlurredSprite(texture, blurTargetSize, stronger);
                    if (blurredBg) {
                        blurredBg->setPosition(blurTargetSize * 0.5f);
                        bgNode = blurredBg;
                    } else {
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

            CCNode* mainNode = nullptr;
            float contentW = 0, contentH = 0;

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

            if (!mainNode && !gifKey.empty()) {

                if (AnimatedGIFSprite::isCached(gifKey)) {
                    auto gifSprite = AnimatedGIFSprite::createFromCache(gifKey);
                    if (gifSprite) {
                        mainNode = gifSprite;
                        contentW = gifSprite->getContentSize().width;
                        contentH = gifSprite->getContentSize().height;

                        gifSprite->play();

                        gifSprite->setID("paimon-profile-thumb-gif"_spr);

                    } else {
                        log::warn("[GJScoreCell] createFromCache returned null for key: {}", gifKey);
                    }
                } else {
                    log::warn("[GJScoreCell] GIF not in cache for key: {}", gifKey);
                }
            }
            
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

            float factor = 0.60f;
            if (isCurrentUser) {
                factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f);
            } else if (config.hasConfig) {
                factor = config.widthFactor;
            }
            
            factor = std::max(0.30f, std::min(0.95f, factor));
            float desiredWidth = cs.width * factor;

            float scaleY = cs.height / contentH;
            float scaleX = desiredWidth / contentW;

            mainNode->setScaleY(scaleY);
            mainNode->setScaleX(scaleX);

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
        clip->setPosition({ cs.width, 0.3f });
        clip->setID("paimon-profile-clip"_spr);
    clip->setZOrder(-1);

        mainNode->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(mainNode);
        
        this->addChild(clip);
        f->m_profileClip = clip;

        if (f->m_hoverWatcher) {
            f->m_hoverWatcher->setTransformTarget(clip, 1.f, 1.f, clip->getPosition(), 0.f);
        }
        
        constexpr float borderThickness = 2.f;
        ccColor4B borderColor = ccc4(0, 0, 0, 120);

        auto makeBorder = [&](CCSize bSize, CCPoint pos, std::string_view id, float skew) {
            auto b = CCLayerColor::create(borderColor);
            b->setContentSize(bSize);
            b->setAnchorPoint({1, 0});
            b->setSkewX(skew);
            b->setPosition(pos);
            b->setZOrder(-1);
            b->setID(std::string(id).c_str());
            this->addChild(b);
            return b;
        };

        makeBorder({scaledSize.width, borderThickness}, {cs.width, 0.3f + scaledSize.height}, "paimon-profile-border-top"_spr, angle);
        makeBorder({scaledSize.width, borderThickness}, {cs.width, 0.3f - borderThickness}, "paimon-profile-border-bottom"_spr, angle);
        makeBorder({borderThickness, scaledSize.height + borderThickness * 2}, {cs.width, 0.3f - borderThickness}, "paimon-profile-border-right"_spr, 0.f);

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

    }

    $override void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        m_fields->m_isBeingDestroyed = false;
        
        pushGameColorLayersBehind(this);

        if (!score) return;

            paimonApplyFx();
            paimon::scorecell::applyLeaderboardLayout(this);

            int accountID = score->m_accountID;
            if (accountID <= 0) return;

            {
                std::string username = score->m_userName;
                if (username.empty()) {
                    log::warn("[GJScoreCell] Username empty for account {}", accountID);
                    return;
                }
                
                auto cachedProfile = ProfileThumbs::get().getCachedProfile(accountID);
                bool wantsGifProfile = cachedProfile.has_value() && !cachedProfile->gifKey.empty();
                bool hasReadyCachedGif = wantsGifProfile && AnimatedGIFSprite::isCached(cachedProfile->gifKey);
                bool hasReadyCachedProfile = cachedProfile.has_value() && (hasReadyCachedGif || (!wantsGifProfile && cachedProfile->texture));
                if (hasReadyCachedProfile) {
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
                
                
                
                showLoadingSpinner();
                
                WeakRef<PaimonGJScoreCell> safeRef = this;

                ProfileThumbs::get().queueLoad(accountID, username, [safeRef, accountID](bool success, CCTexture2D* texture) {
                    auto selfRef = safeRef.lock();
                    auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                    if (!self) return;

                    if (!success) {
                        self->hideLoadingSpinner();
                        log::warn("[GJScoreCell] Failed to download profile for account {}", accountID);
                        return;
                    }

                    if (!texture) {
                        auto cachedEntry = ProfileThumbs::get().getCachedProfile(accountID);
                        if (!cachedEntry.has_value() || cachedEntry->gifKey.empty()) {
                            self->hideLoadingSpinner();
                            log::warn("[GJScoreCell] No texture and no GIF for account {}", accountID);
                            return;
                        }
                    }

                    Ref<CCTexture2D> safeTex = texture;

                    ThumbnailAPI::get().downloadProfileConfig(accountID, [safeRef, accountID, safeTex](bool success2, ProfileConfig const& config) {
                        auto selfRef = safeRef.lock();
                        auto* self = static_cast<PaimonGJScoreCell*>(selfRef.data());
                        if (!self) return;
                        self->hideLoadingSpinner();

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

    }

};

namespace paimon::scorecell {

namespace {
    void refreshCellRecursive(cocos2d::CCNode* node) {
        if (!node) return;
        if (auto cell = geode::cast::typeinfo_cast<GJScoreCell*>(node)) {
            auto paimonCell = static_cast<PaimonGJScoreCell*>(cell);
            paimonCell->paimonApplyFx();
            applyLeaderboardLayout(cell);
            return;
        }
        if (auto children = node->getChildren()) {
            for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(children)) {
                refreshCellRecursive(child);
            }
        }
    }
}

void refreshAllCells() {
    if (paimon::isRuntimeShuttingDown()) return;
    auto scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    refreshCellRecursive(scene);
}

} // namespace paimon::scorecell
