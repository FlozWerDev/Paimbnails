#include <Geode/modify/LevelLeaderboard.hpp>
#include <Geode/binding/LevelLeaderboard.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/utils/cocos.hpp>
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelLeaderboard, LevelLeaderboard) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("LevelLeaderboard::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCClippingNode> m_bgClip = nullptr;
        int m_levelID = 0;
        paimon::SubscriptionHandle m_bgEventHandle = 0;
    };

    $override
    bool init(GJGameLevel* level, LevelLeaderboardType type, LevelLeaderboardMode mode) {
        if (!LevelLeaderboard::init(level, type, mode)) return false;
        if (!level || level->m_levelID <= 0) return true;

        int levelID = level->m_levelID.value();
        m_fields->m_levelID = levelID;

        // Usa thumbnail activo de LevelInfoLayer si esta disponible
        if (paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID == levelID &&
            paimon::ThumbnailBackgroundChangedEvent::s_lastTexture) {
            applyBlurredBackground(paimon::ThumbnailBackgroundChangedEvent::s_lastTexture);
        } else {
            WeakRef<PaimonLevelLeaderboard> safeRef = this;
            ThumbnailLoader::get().requestLoad(
                levelID,
                fmt::format("{}.png", levelID),
                [safeRef, levelID](CCTexture2D* tex, bool ok) {
                    if (!ok || !tex) return;
                    auto ref = safeRef.lock();
                    auto* self = static_cast<PaimonLevelLeaderboard*>(ref.data());
                    if (!self || !self->getParent()) return;
                    self->applyBlurredBackground(tex);
                },
                12, false
            );
        }

        // Se suscribe a eventos de cambio de thumbnail
        WeakRef<PaimonLevelLeaderboard> weakSelf = this;
        m_fields->m_bgEventHandle = paimon::EventBus::get().subscribe<paimon::ThumbnailBackgroundChangedEvent>(
            [weakSelf](paimon::ThumbnailBackgroundChangedEvent const& e) {
                auto ref = weakSelf.lock();
                auto* self = static_cast<PaimonLevelLeaderboard*>(ref.data());
                if (!self || !self->getParent()) return;
                if (self->m_fields->m_levelID != e.levelID || !e.texture) return;
                self->applyBlurredBackground(e.texture);
            }
        );

        // Estiliza la lista del leaderboard
        if (m_list) {
            styleLeaderboardList(m_list);
        }

        return true;
    }

    void applyBlurredBackground(CCTexture2D* tex) {
        if (!tex) return;
        auto* layer = this->m_mainLayer;
        if (!layer) return;

        CCSize popupSize  = {440.f, 290.f};
        CCPoint popupCenter = {layer->getContentSize().width * 0.5f,
                               layer->getContentSize().height * 0.5f};

        // Busca el nodo de fondo del popup
        CCNode* bgNode = layer->getChildByID("background");
        if (!bgNode) {
            for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
                if (typeinfo_cast<CCScale9Sprite*>(child)) { bgNode = child; break; }
            }
        }
        if (bgNode) {
            popupSize   = bgNode->getScaledContentSize();
            popupCenter = bgNode->getPosition();
            // Efecto frosted glass detras del popup
        }

        float padding   = 3.f;
        CCSize imgArea  = {popupSize.width - padding * 2.f, popupSize.height - padding * 2.f};
        if (imgArea.width <= 0 || imgArea.height <= 0) return;

        auto blurredSprite = BlurSystem::getInstance()->createPaimonBlurSprite(tex, imgArea, 4.0f);
        if (!blurredSprite) return;
        blurredSprite->setPosition({imgArea.width * 0.5f, imgArea.height * 0.5f});

        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(imgArea.width, imgArea.height, 8.f);
        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint({0.5f, 0.5f});
        clip->setPosition(popupCenter);
        clip->setID("paimon-leaderboard-bg-clip"_spr);
        clip->addChild(blurredSprite);

        // Oscurece para mejor legibilidad
        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 130));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint({0.f, 0.f});
        dark->setPosition({0.f, 0.f});
        clip->addChild(dark);

        // Reemplaza clip anterior
        if (m_fields->m_bgClip && m_fields->m_bgClip->getParent()) {
            m_fields->m_bgClip->removeFromParent();
        }
        m_fields->m_bgClip = nullptr;

        blurredSprite->setOpacity(0);
        blurredSprite->runAction(CCFadeTo::create(0.3f, 255));

        layer->addChild(clip, -1);
        m_fields->m_bgClip = clip;
    }

    void styleLeaderboardList(GJCommentListLayer* list) {
        if (!list) return;
        list->setOpacity(0);
        auto* children = list->getChildren();
        if (!children) return;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child) continue;
            auto id = child->getID();
            if (id == "left-border" || id == "right-border" ||
                id == "top-border"  || id == "bottom-border" || id.empty()) {
                child->setVisible(false);
            }
        }
    }

    $override
    void setupLeaderboard(cocos2d::CCArray* scores) {
        LevelLeaderboard::setupLeaderboard(scores);
        // Re-estiliza la lista despues de cargar scores
        if (m_list) {
            styleLeaderboardList(m_list);
        }
    }

    $override
    void keyBackClicked() {
        if (m_fields->m_bgEventHandle != 0) {
            paimon::EventBus::get().unsubscribe(m_fields->m_bgEventHandle);
            m_fields->m_bgEventHandle = 0;
        }
        LevelLeaderboard::keyBackClicked();
    }
};