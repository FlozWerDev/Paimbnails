#include <Geode/modify/LevelLeaderboard.hpp>
#include "../framework/HookConventions.hpp"
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
#include <vector>

using namespace geode::prelude;

namespace {

// Walks GJCommentListLayer -> BoomListView -> TableView -> CCContentLayer
// (the node holding the cells). Returns nullptr if the hierarchy isn't built yet.
CCNode* findLeaderboardContentLayer(GJCommentListLayer* list) {
    if (!list) return nullptr;

    BoomListView* listView = nullptr;
    if (auto* ch = list->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(ch)) {
            if (auto* blv = typeinfo_cast<BoomListView*>(child)) { listView = blv; break; }
        }
    }
    if (!listView) return nullptr;

    TableView* tableView = nullptr;
    if (auto* ch = listView->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(ch)) {
            if (auto* tv = typeinfo_cast<TableView*>(child)) { tableView = tv; break; }
        }
    }
    if (!tableView) return nullptr;

    if (auto* ch = tableView->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(ch)) {
            if (child && child->getChildrenCount() > 0) return child;
        }
    }
    return nullptr;
}

} // namespace

class $modify(PaimonLevelLeaderboard, LevelLeaderboard) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelLeaderboard::init");
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

        // Use LevelInfoLayer's active thumbnail if available
        if (paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID == levelID &&
            paimon::ThumbnailBackgroundChangedEvent::getLastTexture()) {
            applyBlurredBackground(paimon::ThumbnailBackgroundChangedEvent::getLastTexture());
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

        // Subscribe to thumbnail-change events
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

        if (m_list) {
            styleLeaderboardList(m_list);
            normalizeCellBackgrounds(m_list);
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

        CCNode* bgNode = layer->getChildByID("background");
        if (!bgNode) {
            for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
                if (typeinfo_cast<CCScale9Sprite*>(child)) { bgNode = child; break; }
            }
        }
        if (bgNode) {
            popupSize   = bgNode->getScaledContentSize();
            popupCenter = bgNode->getPosition();
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

        // Darken for legibility
        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 130));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint({0.f, 0.f});
        dark->setPosition({0.f, 0.f});
        clip->addChild(dark);

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

    // Normalize all cell opacity so vanilla's alternating pattern doesn't show
    void normalizeCellBackgrounds(GJCommentListLayer* list) {
        auto* contentLayer = findLeaderboardContentLayer(list);
        if (!contentLayer) return;

        auto* cellChildren = contentLayer->getChildren();
        if (!cellChildren) return;
        for (auto* child : CCArrayExt<CCNode*>(cellChildren)) {
            auto* cell = typeinfo_cast<TableViewCell*>(child);
            if (!cell) continue;

            if (auto* bg = cell->m_backgroundLayer) {
                bg->setOpacity(0);
            }
        }
    }

    // Animate cells with a staggered slide + fade-in
    void animateCellsEntrance(GJCommentListLayer* list) {
        auto* contentLayer = findLeaderboardContentLayer(list);
        if (!contentLayer) return;

        auto* cellChildren = contentLayer->getChildren();
        if (!cellChildren) return;

        std::vector<CCNode*> cells;
        for (auto* child : CCArrayExt<CCNode*>(cellChildren)) {
            if (typeinfo_cast<TableViewCell*>(child)) {
                cells.push_back(child);
            }
        }

        for (size_t i = 0; i < cells.size(); i++) {
            auto* cell = cells[i];
            float originalX = cell->getPositionX();
            float originalY = cell->getPositionY();

            // Initial state: shifted right and transparent
            cell->setPositionX(originalX + 30.f);

            if (auto* ch = cell->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(ch)) {
                    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(child)) {
                        rgba->setOpacity(0);
                    }
                }
            }

            float delay = i * 0.04f;
            float duration = 0.3f;

            cell->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCEaseOut::create(CCMoveTo::create(duration, {originalX, originalY}), 2.0f),
                nullptr
            ));

            if (auto* ch = cell->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(ch)) {
                    auto* tableCell = typeinfo_cast<TableViewCell*>(cell);
                    // Don't restore m_backgroundLayer opacity (already hidden)
                    if (tableCell && child == static_cast<CCNode*>(tableCell->m_backgroundLayer)) continue;
                    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(child)) {
                        child->runAction(CCSequence::create(
                            CCDelayTime::create(delay),
                            CCFadeTo::create(duration, 255),
                            nullptr
                        ));
                    }
                }
            }
        }
    }

    $override
    void setupLeaderboard(cocos2d::CCArray* scores) {
        LevelLeaderboard::setupLeaderboard(scores);
        // Re-style the list after scores load
        if (m_list) {
            styleLeaderboardList(m_list);
            normalizeCellBackgrounds(m_list);
            animateCellsEntrance(m_list);
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

    // Safety net: ensure unsubscription if the layer is destroyed via
    // scene-replace, popScene, or any path that skips keyBackClicked; otherwise
    // the EventBus listener leaks as a zombie and the WeakRef lambda keeps firing.
    $override
    void onExit() {
        if (m_fields->m_bgEventHandle != 0) {
            paimon::EventBus::get().unsubscribe(m_fields->m_bgEventHandle);
            m_fields->m_bgEventHandle = 0;
        }
        LevelLeaderboard::onExit();
    }
};
