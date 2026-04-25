#include <Geode/binding/InfoLayer.hpp>
#include <Geode/modify/InfoLayer.hpp>
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
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include <algorithm>

using namespace geode::prelude;

// Declarado en ProfilePage.cpp: acceso al cache de texturas
extern CCTexture2D* getProfileImgCachedTexture(int accountID);

class $modify(PaimonInfoLayer, InfoLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("InfoLayer::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCClippingNode> m_bgClip = nullptr;
        bool m_hasCaveEffect = false;
        int m_levelID = 0;
        paimon::SubscriptionHandle m_bgEventHandle = 0;
    };

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
                        Loader::get()->queueInMainThread([safeRef, accountID, texture]() {
                            auto* self = static_cast<PaimonInfoLayer*>(safeRef.data());
                            if (self && self->getParent()) {
                                auto liveGifKey = ProfileImageService::get().getProfileImgGifKey(accountID);
                                if (!liveGifKey.empty() && AnimatedGIFSprite::isCached(liveGifKey)) {
                                    self->applyBlurredBackgroundGif(liveGifKey);
                                } else {
                                    self->applyBlurredBackground(texture);
                                }
                            }
                        });
                    }
                }, false);
                if (needsAnimatedFetch || !tex) {
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
                paimon::ThumbnailBackgroundChangedEvent::s_lastTexture) {
                applyBlurredBackground(paimon::ThumbnailBackgroundChangedEvent::s_lastTexture);
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
                    self->applyBlurredBackground(e.texture);
                });
        }

        return true;
    }

    void applyBlurredBackground(CCTexture2D* tex) {
        if (!tex) return;

        auto layer = this->m_mainLayer;
        if (!layer) return;
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

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        auto blurredSprite = BlurSystem::getInstance()->createPaimonBlurSprite(tex, imgArea, 4.0f);
        if (!blurredSprite) return;

        blurredSprite->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));

        auto stencil = paimon::SpriteHelper::createRectStencil(imgArea.width, imgArea.height);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);
        clip->setID("paimon-infolayer-bg-clip"_spr);

        clip->addChild(blurredSprite);

        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 120));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-infolayer-dark-overlay"_spr);
        clip->addChild(dark);

        if (m_fields->m_bgClip && m_fields->m_bgClip->getParent()) {
            m_fields->m_bgClip->setID("paimon-infolayer-bg-clip-old"_spr);
            this->unschedule(schedule_selector(PaimonInfoLayer::cleanupOldBgClip));
            this->scheduleOnce(schedule_selector(PaimonInfoLayer::cleanupOldBgClip), 0.35f);
        }
        m_fields->m_bgClip = nullptr;

        blurredSprite->setOpacity(0);
        blurredSprite->runAction(CCFadeTo::create(0.3f, 255));

        layer->addChild(clip, -1);
        m_fields->m_bgClip = clip;

        styleInfoLayerBgs(layer);
        addInfoAreaPanel();
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
        this->schedule(schedule_selector(PaimonInfoLayer::tickStyleBgs), 0.0f);
    }

    void applyBlurredBackgroundGif(std::string const& gifKey) {
        auto gif = AnimatedGIFSprite::createFromCache(gifKey);
        if (!gif) return;
        if (m_fields->m_bgClip) {
            m_fields->m_bgClip->removeFromParent();
            m_fields->m_bgClip = nullptr;
        }

        auto layer = this->m_mainLayer;
        if (!layer) return;
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

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        float scaleX = imgArea.width / std::max(1.0f, gif->getContentWidth());
        float scaleY = imgArea.height / std::max(1.0f, gif->getContentHeight());
        gif->setScale(std::max(scaleX, scaleY));
        gif->setAnchorPoint(ccp(0.5f, 0.5f));
        gif->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));
        gif->m_intensity = std::clamp((7.0f - 1.0f) / 9.0f, 0.0f, 1.0f);
        if (gif->getTexture()) {
            gif->m_texSize = gif->getTexture()->getContentSizeInPixels();
        }
        if (auto shader = Shaders::getBlurSinglePassShader()) {
            gif->setShaderProgram(shader);
        }
        gif->play();

        auto stencil = paimon::SpriteHelper::createRectStencil(imgArea.width, imgArea.height);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);
        clip->setID("paimon-infolayer-bg-clip"_spr);

        clip->addChild(gif);

        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 120));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-infolayer-dark-overlay"_spr);
        clip->addChild(dark);

        layer->addChild(clip, -1);
        m_fields->m_bgClip = clip;

        styleInfoLayerBgs(layer);
        addInfoAreaPanel();
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
        this->schedule(schedule_selector(PaimonInfoLayer::tickStyleBgs), 0.0f);
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
                            if (id.empty()) {
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
        if (!listNode) return;

        auto findCells = [&](auto const& self, CCNode* node) -> void {
            if (!node) return;
            auto* children = node->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                if (typeinfo_cast<CommentCell*>(child)) {
                    if (!child->getChildByIDRecursive("paimon-comment-bg-panel"_spr)) {
                        self(self, child);
                        continue;
                    }

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
    }

    $override
    void keyBackClicked() {
        restoreMusicEffect();
        InfoLayer::keyBackClicked();
    }

    $override
    void onExit() {
        restoreMusicEffect();
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
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
