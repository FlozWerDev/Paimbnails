#include <Geode/binding/InfoLayer.hpp>
#include <Geode/modify/InfoLayer.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJLevelList.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/CommentCell.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/CommentBgHider.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <algorithm>
#include <tuple>

using namespace geode::prelude;

// Profile image cache access kept at global scope for compatibility.
#include "../features/profiles/services/ProfileImageCache.hpp"

class $modify(PaimonInfoLayer, InfoLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "InfoLayer::init");
    }

    struct Fields {
        Ref<CCClippingNode> m_bgClip = nullptr;
        bool m_hasCaveEffect = false;
        int m_levelID = 0;
        paimon::SubscriptionHandle m_bgEventHandle = 0;
        int m_bgRequestToken = 0;
        int m_styleTickCount = 0;
        bool m_bgSettled = false;
        uintptr_t m_lastBgTex = 0;

        ~Fields() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (m_bgEventHandle != 0) {
                paimon::EventBus::get().unsubscribe(m_bgEventHandle);
                m_bgEventHandle = 0;
            }
        }
    };

    std::tuple<CCNode*, CCSize, CCPoint> resolvePopupBackgroundLayout() {
        auto layer = this->m_mainLayer;
        if (!layer) {
            return {nullptr, CCSizeZero, CCPointZero};
        }

        auto layerSize = layer->getContentSize();
        CCSize popupSize = CCSize(440.f, 290.f);
        CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        if (auto bg = layer->getChildByID("background")) {
            popupSize = bg->getScaledContentSize();
            popupCenter = bg->getPosition();
        } else {
            for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
                if (typeinfo_cast<CCScale9Sprite*>(child)) {
                    popupSize = child->getScaledContentSize();
                    popupCenter = child->getPosition();
                    break;
                }
            }
        }

        return {layer, popupSize, popupCenter};
    }

    CCClippingNode* buildBlurBackgroundClip(CCSprite* backgroundSprite, CCSize const& imgArea, CCPoint popupCenter) {
        if (!backgroundSprite || imgArea.width <= 0.f || imgArea.height <= 0.f) {
            return nullptr;
        }

        backgroundSprite->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));

        auto stencil = paimon::SpriteHelper::createRectStencil(imgArea.width, imgArea.height);
        auto clip = CCClippingNode::create();
        if (!stencil || !clip) {
            return nullptr;
        }

        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);
        clip->setID("paimon-infolayer-bg-clip"_spr);
        clip->addChild(backgroundSprite);

        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 120));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-infolayer-dark-overlay"_spr);
        clip->addChild(dark);
        return clip;
    }

    void removeAllInfoLayerBgClips() {
        auto layer = this->m_mainLayer;
        if (!layer) return;
// Remove every background clip; delayed single-ID cleanup could leak duplicates.
        auto* children = layer->getChildren();
        if (!children) return;
        std::vector<CCNode*> toRemove;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child) continue;
            auto id = child->getID();
            if (id == "paimon-infolayer-bg-clip"_spr ||
                id == "paimon-infolayer-bg-clip-old"_spr) {
                toRemove.push_back(child);
            }
        }
        for (auto* n : toRemove) {
            n->stopAllActions();
            n->removeFromParent();
        }
        m_fields->m_bgClip = nullptr;
    }

    void installBackgroundClip(CCClippingNode* clip, bool fadeIn) {
        auto layer = this->m_mainLayer;
        if (!layer || !clip) {
            return;
        }

        this->unschedule(schedule_selector(PaimonInfoLayer::cleanupOldBgClip));
        removeAllInfoLayerBgClips();

        if (fadeIn) {
            if (auto bgSprite = clip->getChildByType<CCSprite>(0)) {
                bgSprite->setOpacity(0);
                bgSprite->runAction(CCFadeTo::create(0.3f, 255));
            }
        }

        layer->addChild(clip, -1);
        m_fields->m_bgClip = clip;

        styleInfoLayerBgs(layer);
        addInfoAreaPanel();
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
// Run a short catch-up for cells that finish loading after clip setup.
        m_fields->m_styleTickCount = 0;
        this->schedule(schedule_selector(PaimonInfoLayer::tickStyleBgs), 0.8f);
    }

    $override
    bool init(GJGameLevel* level, GJUserScore* score, GJLevelList* list) {
        if (!InfoLayer::init(level, score, list)) return false;

        if (score) {
            int accountID = score->m_accountID;
            if (accountID <= 0) return true;

            auto gifKey = ProfileImageService::get().getProfileImgGifKey(accountID);
            if (!gifKey.empty() && AnimatedGIFSprite::isCached(gifKey)) {
                applyBlurredBackgroundGif(gifKey);
            }

            CCTexture2D* tex = getProfileImgCachedTexture(accountID);
            bool needsAnimatedFetch = !gifKey.empty() && !AnimatedGIFSprite::isCached(gifKey);
            if (needsAnimatedFetch || !tex) {
                Ref<InfoLayer> safeRef = this;
                ThumbnailAPI::get().downloadProfileImg(accountID, [safeRef, accountID](bool success, CCTexture2D* texture) {
                    if (success && texture) {
// Retain the autoreleased texture across the second main-thread hop.
                        Ref<CCTexture2D> texRef = texture;
                        Loader::get()->queueInMainThread([safeRef, accountID, texRef]() {
                            if (paimon::isRuntimeShuttingDown()) return;
                            auto* self = static_cast<PaimonInfoLayer*>(safeRef.data());
                            if (self && self->getParent()) {
                                auto liveGifKey = ProfileImageService::get().getProfileImgGifKey(accountID);
                                if (!liveGifKey.empty() && AnimatedGIFSprite::isCached(liveGifKey)) {
                                    self->applyBlurredBackgroundGif(liveGifKey);
                                } else {
                                    self->applyBlurredBackground(texRef);
                                }
                            }
                        });
                    }
                }, false);
                if (needsAnimatedFetch || !tex) {
                    if (tex && m_fields->m_bgClip == nullptr) {
                        applyBlurredBackground(tex);
                    }
                    return true;
                }
            }

            if (!needsAnimatedFetch && m_fields->m_bgClip == nullptr && tex) {
                applyBlurredBackground(tex);
            }

            if (ProfileMusicManager::get().isPlaying()) {
                ProfileMusicManager::get().applyCaveEffect();
                m_fields->m_hasCaveEffect = true;
            }
        } else if (level && level->m_levelID.value() > 0) {
            int levelID = level->m_levelID.value();
            m_fields->m_levelID = levelID;

            if (paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID == levelID &&
                paimon::ThumbnailBackgroundChangedEvent::getLastTexture()) {
                applyBlurredBackground(paimon::ThumbnailBackgroundChangedEvent::getLastTexture());
            } else {
                std::string fileName = fmt::format("{}.png", levelID);
                Ref<InfoLayer> safeRef = this;
                ThumbnailLoader::get().requestLoad(levelID, fileName,
                    [safeRef](CCTexture2D* tex, bool success) {
                        auto* self = static_cast<PaimonInfoLayer*>(safeRef.data());
                        if (self->getParent() && tex) {
                            self->applyBlurredBackground(tex);
                        }
                    }, 12, false, ThumbnailLoader::Quality::High);
            }

            WeakRef<PaimonInfoLayer> weakSelf = this;
            m_fields->m_bgEventHandle = paimon::EventBus::get().subscribe<paimon::ThumbnailBackgroundChangedEvent>(
                [weakSelf](paimon::ThumbnailBackgroundChangedEvent const& e) {
                    auto ref = weakSelf.lock();
                    auto* self = static_cast<PaimonInfoLayer*>(ref.data());
                    if (!self) return;
                    if (!self->getParent()) return;
                    if (self->m_fields->m_levelID <= 0 || self->m_fields->m_levelID != e.levelID) return;
                    if (!e.texture) return;
// Follow gallery changes; blur application deduplicates textures.
                    self->applyBlurredBackground(e.texture);
                });
        }

        return true;
    }

    void applyBlurredBackground(CCTexture2D* tex) {
        if (!tex) return;

        auto texKey = reinterpret_cast<uintptr_t>(tex);
        if (m_fields->m_bgSettled && m_fields->m_lastBgTex == texKey && m_fields->m_bgClip) {
            return;
        }
        m_fields->m_lastBgTex = texKey;

        auto [layer, popupSize, popupCenter] = resolvePopupBackgroundLayout();
        if (!layer) return;

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        int requestToken = ++m_fields->m_bgRequestToken;

        if (!m_fields->m_bgClip) {
            if (auto* blurred = Shaders::createPopupPaimonBlurredSprite(tex, imgArea, 4.0f)) {
                if (auto* clip = buildBlurBackgroundClip(blurred, imgArea, popupCenter)) {
                    installBackgroundClip(clip, false);
                    m_fields->m_bgSettled = true;
                    m_fields->m_lastBgTex = texKey;
                    return;
                }
            }
        }

        m_fields->m_bgSettled = false;

        WeakRef<PaimonInfoLayer> safeRef = this;
// Use an empty key so BlurSystem keys by texture pointer; level-based keys
// collided when gallery textures changed.
        BlurSystem::getInstance()->buildPaimonBlurPriority(
            tex,
            imgArea,
            4.0f,
            std::string{},
            [safeRef, requestToken, imgArea, popupCenter, texKey](CCSprite* blurredSprite) {
                auto ref = safeRef.lock();
                auto* self = static_cast<PaimonInfoLayer*>(ref.data());
                if (!self || !self->getParent()) return;
                if (self->m_fields->m_bgRequestToken != requestToken) return;
                if (!blurredSprite) {
                    self->m_fields->m_bgSettled = false;
                    return;
                }

                if (auto clip = self->buildBlurBackgroundClip(blurredSprite, imgArea, popupCenter)) {
                    self->installBackgroundClip(clip, true);
                }
                self->m_fields->m_bgSettled = true;
                self->m_fields->m_lastBgTex = texKey;
            }
        );
    }

    void applyBlurredBackgroundGif(std::string const& gifKey) {
        auto gif = AnimatedGIFSprite::createFromCache(gifKey);
        if (!gif) return;

        auto [layer, popupSize, popupCenter] = resolvePopupBackgroundLayout();
        if (!layer) return;

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        float scaleX = imgArea.width / std::max(1.0f, gif->getContentWidth());
        float scaleY = imgArea.height / std::max(1.0f, gif->getContentHeight());
        gif->setScale(std::max(scaleX, scaleY));
        gif->setAnchorPoint(ccp(0.5f, 0.5f));
        gif->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));
// Use a static first frame for the full popup background.
        gif->stop();

        if (auto clip = buildBlurBackgroundClip(gif, imgArea, popupCenter)) {
            ++m_fields->m_bgRequestToken;
            installBackgroundClip(clip, false);
            m_fields->m_bgSettled = true;
        }
    }

    void addInfoAreaPanel() {
        auto* layer = this->m_mainLayer;
        if (!layer) return;
        if (layer->getChildByID("paimon-info-area-bg"_spr)) return;

        TextArea* descText = nullptr;
        auto findTA = [&](auto const& self, CCNode* node) -> void {
            if (!node || descText) return;
            if (auto* ta = typeinfo_cast<TextArea*>(node)) {
                descText = ta;
                return;
            }
            auto* children = node->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                self(self, child);
            }
        };
        findTA(findTA, layer);
        if (!descText) return;

        GJCommentListLayer* commentList = nullptr;
        auto findCL = [&](auto const& self, CCNode* node) -> void {
            if (!node || commentList) return;
            auto* children = node->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (auto* cl = typeinfo_cast<GJCommentListLayer*>(child)) {
                    commentList = cl;
                    return;
                }
                self(self, child);
            }
        };
        findCL(findCL, layer);
        if (!commentList) return;

        CCSize clSize = commentList->getScaledContentSize();
        CCPoint clBL = commentList->convertToWorldSpace(ccp(0.f, 0.f));
        CCPoint clBLinLayer = layer->convertToNodeSpace(clBL);

        CCSize descSz = descText->getScaledContentSize();
        CCPoint descBL = descText->convertToWorldSpace(ccp(0.f, 0.f));
        CCPoint descTR = descText->convertToWorldSpace(ccp(descSz.width, descSz.height));
        CCPoint blInLayer = layer->convertToNodeSpace(descBL);
        CCPoint trInLayer = layer->convertToNodeSpace(descTR);

        float padding = 6.f;
        float panelW = clSize.width;
        float panelH = (trInLayer.y - blInLayer.y) + padding * 2.f + 30.f;
        float panelCenterX = clBLinLayer.x + clSize.width * 0.5f;
        float panelCenterY = (blInLayer.y + trInLayer.y) * 0.5f;

        if (panelH < 10.f || panelW < 10.f) return;

        auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 150, 6.f);
        panel->setAnchorPoint(ccp(0.5f, 0.5f));
        panel->setPosition(ccp(panelCenterX, panelCenterY));
        panel->setID("paimon-info-area-bg"_spr);
        layer->addChild(panel, 0);
    }

    void styleInfoLayerBgs(CCNode* root) {
        if (!root) return;

        auto walk = [&](auto const& self, CCNode* parent) -> void {
            if (!parent) return;
            auto* children = parent->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                if (child->getID() == "alphalaneous.happy_textures/special-border") {
                    child->setVisible(false);
                }

                if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(child)) {
                    commentList->setOpacity(0);

                    auto* listChildren = commentList->getChildren();
                    if (listChildren) {
                        for (auto* lc : CCArrayExt<CCNode*>(listChildren)) {
                            if (!lc) continue;
                            auto id = lc->getID();
                            if (id == "left-border" || id == "right-border" ||
                                id == "top-border" || id == "bottom-border") {
                                lc->setVisible(false);
                            }
                        }
                    }

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

    void cleanupOldBgClip(float) {
        if (auto* layer = this->m_mainLayer) {
            if (auto* old = layer->getChildByID("paimon-infolayer-bg-clip-old"_spr)) {
                old->removeFromParent();
            }
        }
    }

    void tickStyleBgs(float) {
        if (auto* layer = this->m_mainLayer) {
            styleInfoLayerBgs(layer);
        }
        if (++m_fields->m_styleTickCount >= 5) {
            this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
        }
    }

// Reapply the style as soon as GD rebuilds the list; leave vanilla UI untouched
// when no Paimon background is installed.
    void styleCommentsNow() {
        if (!m_fields->m_bgClip) return;
        if (auto* layer = this->m_mainLayer) {
            styleInfoLayerBgs(layer);
        }
// Re-arm a short catch-up after paging.
        m_fields->m_styleTickCount = 0;
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
        this->schedule(schedule_selector(PaimonInfoLayer::tickStyleBgs), 0.8f);
    }

// Setup, response, and paging can all create new vanilla brown nodes; hide them.
    $override
    void setupCommentsBrowser(cocos2d::CCArray* comments) {
        InfoLayer::setupCommentsBrowser(comments);
        styleCommentsNow();
    }

    $override
    void loadCommentsFinished(cocos2d::CCArray* comments, char const* key) {
        InfoLayer::loadCommentsFinished(comments, key);
        styleCommentsNow();
    }

    $override
    void loadPage(int page, bool noSetup) {
        InfoLayer::loadPage(page, noSetup);
        styleCommentsNow();
    }

    void stopBackgroundUpdates() {
        if (m_fields->m_bgEventHandle != 0) {
            paimon::EventBus::get().unsubscribe(m_fields->m_bgEventHandle);
            m_fields->m_bgEventHandle = 0;
        }
        ++m_fields->m_bgRequestToken;
    }

    $override
    void keyBackClicked() {
        restoreMusicEffect();
        stopBackgroundUpdates();
        InfoLayer::keyBackClicked();
    }

    void onClose(CCObject* sender) {
        restoreMusicEffect();
        stopBackgroundUpdates();
        InfoLayer::onClose(sender);
    }

    $override
    void onExit() {
        restoreMusicEffect();
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
        this->unschedule(schedule_selector(PaimonInfoLayer::cleanupOldBgClip));
        removeAllInfoLayerBgClips();
        stopBackgroundUpdates();
        InfoLayer::onExit();
    }

    void restoreMusicEffect() {
        if (m_fields->m_hasCaveEffect) {
            auto& musicMgr = ProfileMusicManager::get();
            if (musicMgr.isPlaying() || musicMgr.isPaused()) {
                musicMgr.removeCaveEffect();
            } else {
                musicMgr.forceRemoveCaveEffect();
            }
            m_fields->m_hasCaveEffect = false;
        }
    }
};
