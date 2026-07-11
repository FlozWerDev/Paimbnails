#include <Geode/modify/LevelPage.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/utils/cocos.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Assets.hpp"
#include "../utils/SpriteHelper.hpp"
#include <algorithm>
#include <vector>
#include <cmath>
#include "../framework/HookConventions.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelPage, LevelPage) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelPage::updateDynamicPage");
    }

    struct Fields {
        Ref<CCNode> m_thumbClipper = nullptr;
        Ref<CCSprite> m_thumbSprite = nullptr;
        int m_levelID = 0;
        std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
        int m_currentThumbnailIndex = 0;
        float m_cycleTimer = 0.f;
        int m_cycleToken = 0;
        int m_invalidationListenerId = 0;
    };

    $override
    void updateDynamicPage(GJGameLevel* level) {
        LevelPage::updateDynamicPage(level);
        
        if (!level) return;
        
        if (m_fields->m_levelID > 0 && m_fields->m_levelID != level->m_levelID) {
            ThumbnailLoader::get().cancelLoad(m_fields->m_levelID);
        }
        
        m_fields->m_levelID = level->m_levelID;
        m_fields->m_cycleTimer = 0.f;
        m_fields->m_currentThumbnailIndex = 0;
        this->unschedule(schedule_selector(PaimonLevelPage::updateGalleryCycle));

        if (m_fields->m_invalidationListenerId == 0) {
            WeakRef<PaimonLevelPage> safeRef = this;
            m_fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener([safeRef](int invalidLevelID) {
                auto ref = safeRef.lock();
                auto* self = static_cast<PaimonLevelPage*>(ref.data());
                if (!self || !self->getParent()) return;
                if (!self->m_level || self->m_level->m_levelID != invalidLevelID) return;
                self->updateDynamicPage(self->m_level);
            });
        }
        
        if (level->m_levelID <= 0) return;
        
        if (this->m_levelDisplay) {
            // Reset to the ORIGINAL preview every time the page is (re)populated:
            // drop any previous thumbnail clipper (removeFromParent also cancels
            // its pending "hide vanilla" action) and make the vanilla content
            // visible again. The thumbnail, once its assets finish downloading,
            // fades in on top via applyThumbnail and only then hides the vanilla
            // underneath. This keeps the cell 100% original until there is
            // something ready to transition to (no premature/placeholder image,
            // no empty cell) and correctly handles recycled pages on swipe.
            if (m_fields->m_thumbClipper) {
                m_fields->m_thumbClipper->removeFromParent();
                m_fields->m_thumbClipper = nullptr;
                m_fields->m_thumbSprite = nullptr;
            }
            if (auto* children = m_levelDisplay->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if (child && child->getID() != "paimbnails-clipper"_spr) {
                        child->setVisible(true);
                    }
                }
            }

            int capturedLevelID = level->m_levelID;
            int token = ++m_fields->m_cycleToken;
            Ref<LevelPage> safeRef = this;
            ThumbnailAPI::get().getThumbnails(capturedLevelID, [safeRef, capturedLevelID, token](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                auto* self = static_cast<PaimonLevelPage*>(safeRef.data());
                if (!self->getParent() || self->m_fields->m_cycleToken != token || self->m_fields->m_levelID != capturedLevelID) return;

                if (success && !thumbs.empty()) {
                    self->m_fields->m_thumbnails = thumbs;
                    self->loadThumbnailAt(0);
                    if (thumbs.size() >= 2 && Mod::get()->getSavedValue<bool>("levelcell-gallery-autocycle", true)) {
                        self->schedule(schedule_selector(PaimonLevelPage::updateGalleryCycle), 0.f);
                    }
                    return;
                }

                std::string fileName = fmt::format("{}.png", capturedLevelID);
                ThumbnailLoader::get().requestLoad(capturedLevelID, fileName, [safeRef, capturedLevelID](CCTexture2D* tex, bool loadSuccess) {
                    auto* self = static_cast<PaimonLevelPage*>(safeRef.data());
                    if (!self->getParent() || self->m_fields->m_levelID != capturedLevelID) return;
                    if (loadSuccess && tex) self->applyThumbnail(tex);
                }, ThumbnailLoader::PriorityHero, false, ThumbnailLoader::Quality::High);
            });
        }
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLevelPage::updateGalleryCycle));
        if (m_fields->m_invalidationListenerId != 0) {
            ThumbnailLoader::get().removeInvalidationListener(m_fields->m_invalidationListenerId);
            m_fields->m_invalidationListenerId = 0;
        }
        m_fields->m_cycleToken++;
        LevelPage::onExit();
    }

    void updateGalleryCycle(float dt) {
        if (m_fields->m_thumbnails.size() < 2) return;
        m_fields->m_cycleTimer += dt;
        if (m_fields->m_cycleTimer < 3.0f) return;
        m_fields->m_cycleTimer = 0.f;
        int next = (m_fields->m_currentThumbnailIndex + 1) % static_cast<int>(m_fields->m_thumbnails.size());
        loadThumbnailAt(next);
    }

    void loadThumbnailAt(int index) {
        if (index < 0 || index >= static_cast<int>(m_fields->m_thumbnails.size())) return;
        m_fields->m_currentThumbnailIndex = index;
        int capturedLevelID = m_fields->m_levelID;
        int token = ++m_fields->m_cycleToken;
        std::string url = m_fields->m_thumbnails[index].url;
        if (!m_fields->m_thumbnails[index].id.empty()) {
            auto sep = (url.find('?') == std::string::npos) ? "?" : "&";
            url += fmt::format("{}_pv={}", sep, m_fields->m_thumbnails[index].id);
        }
        int attemptIndex = index;
        Ref<LevelPage> safeRef = this;
        ThumbnailLoader::get().requestUrlLoad(url, [safeRef, capturedLevelID, token, attemptIndex](CCTexture2D* tex, bool success) {
            auto* self = static_cast<PaimonLevelPage*>(safeRef.data());
            if (!self->getParent() || self->m_fields->m_cycleToken != token || self->m_fields->m_levelID != capturedLevelID) return;
            if (success && tex) {
                self->applyThumbnail(tex);
            } else if (self->m_fields->m_thumbnails.size() > 1) {
                int next = (attemptIndex + 1) % static_cast<int>(self->m_fields->m_thumbnails.size());
                if (next != attemptIndex) self->loadThumbnailAt(next);
            }
        });
    }
    
    void applyThumbnail(CCTexture2D* tex) {
        if (!tex || !m_levelDisplay) return;

        // Keep the previous clipper alive until the new one has fully faded in,
        // so gallery cycling cross-fades smoothly instead of flashing.
        Ref<CCNode> oldClipper = m_fields->m_thumbClipper;

        auto sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;

        CCSize boxSize = m_levelDisplay->getContentSize();

            // Rounded clipping for official levels
        float cornerRadius = std::clamp(boxSize.height * 0.11f, 6.f, 14.f);
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(boxSize.width, boxSize.height, cornerRadius);
        if (!stencil) return;

        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(boxSize);
        clipper->setAnchorPoint({0.5f, 0.5f});
        clipper->setPosition(boxSize / 2);

        // Fit sprite to the container
        float scaleX = boxSize.width / sprite->getContentSize().width;
        float scaleY = boxSize.height / sprite->getContentSize().height;
        float scale = std::max(scaleX, scaleY);

        sprite->setScaleX(scale);
        sprite->setScaleY(scale * 0.985f); // shrink height slightly so it doesn't overflow a pixel top/bottom
        sprite->setPosition(boxSize / 2);
        sprite->setColor({255, 255, 255});
        sprite->setOpacity(0); // fade in over the original preview

        clipper->addChild(sprite);

        // Transparent dark overlay
        auto darkOverlay = CCSprite::create();
        darkOverlay->setTextureRect(CCRect(0, 0, boxSize.width, boxSize.height + 2.f)); // 1px taller top and bottom
        darkOverlay->setColor({0, 0, 0});
        darkOverlay->setOpacity(0); // fade in to 45
        darkOverlay->setPosition(boxSize / 2);
        clipper->addChild(darkOverlay, 2);

        // Add the thumbnail as the cell BACKGROUND (z = -1, behind the vanilla
        // labels/difficulty/coins — exactly like the original) and fade it in,
        // so the level's text stays visible on top while the preview cross-fades
        // from the original render to the thumbnail.
        clipper->setID("paimbnails-clipper"_spr);
        m_levelDisplay->addChild(clipper, -1);

        constexpr float kFadeDur = 0.35f;
        sprite->runAction(CCFadeIn::create(kFadeDur));
        darkOverlay->runAction(CCFadeTo::create(kFadeDur, 45));

        m_fields->m_thumbClipper = clipper;
        m_fields->m_thumbSprite = sprite;

        // Once the new thumbnail has faded in, drop the previous one so gallery
        // cycling cross-fades instead of stacking clippers.
        if (oldClipper) {
            clipper->runAction(CCSequence::create(
                CCDelayTime::create(kFadeDur + 0.02f),
                CallFuncExt::create([oldClipper]() {
                    if (oldClipper && oldClipper->getParent()) {
                        oldClipper->removeFromParent();
                    }
                }),
                nullptr
            ));
        }
    }
};
