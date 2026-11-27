#include <Geode/Geode.hpp>
#include "../../../framework/HookConventions.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/NodeEvent.hpp>

#include "../services/ProgressBarManager.hpp"
#include "../ui/ProgressBarConfigPopup.hpp"
#include "../../gameplay-performance/GameplayPerformance.hpp"
#include "../../../utils/PauseMenuNodes.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// registered with the global scheduler so it keeps ticking even while gameplay is paused.
class ProgressBarTicker : public CCNode {
public:
    static ProgressBarTicker* create() {
        auto* ret = new ProgressBarTicker();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        this->setID("paimon-progressbar-ticker"_spr);
        return true;
    }

    void update(float) override {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kModVisualsModuleId)) return;
        if (auto* pl = PlayLayer::get()) {
            ProgressBarManager::get().applyToPlayLayer(pl, pl->m_isPracticeMode);
        }
    }
};

namespace {
geode::Ref<ProgressBarTicker> s_progressBarTicker = nullptr;

void ensureProgressBarTicker() {
    if (s_progressBarTicker) return;
    s_progressBarTicker = ProgressBarTicker::create();
    if (!s_progressBarTicker) {
        log::error("[ProgressBar] Failed to create ticker");
        return;
    }
    auto* dir = CCDirector::get();
    if (!dir) return;
    auto* sch = dir->getScheduler();
    if (!sch) return;
    sch->scheduleUpdateForTarget(s_progressBarTicker.data(), 0, false);
}

void shutdownProgressBarTicker() {
    if (!s_progressBarTicker) return;
    if (auto* dir = CCDirector::get()) {
        if (auto* sch = dir->getScheduler()) {
            sch->unscheduleUpdateForTarget(s_progressBarTicker.data());
        }
    }
    (void)s_progressBarTicker.take();
}
} // namespace

$on_game(Exiting) {
    shutdownProgressBarTicker();
}

class ProgressBarDragLayer : public CCLayer {
public:
    static ProgressBarDragLayer* create() {
        auto* ret = new ProgressBarDragLayer();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        this->setID("paimon-progressbar-drag-layer"_spr);
        this->setTouchEnabled(true);
        return true;
    }

    void registerWithTouchDispatcher() override {
        // priority just above pauselayer (typically -128).
        CCDirector::get()->getTouchDispatcher()
            ->addTargetedDelegate(this, -129, true);
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        auto& mgr = ProgressBarManager::get();
        if (!mgr.isFreeDragActive()) return false;

        auto* pl = PlayLayer::get();
        if (!pl) return false;

        CCNode* bar = mgr.getActiveBarNode(pl);
        if (!bar || !bar->getParent()) return false;

        auto worldPos = touch->getLocation();

        auto bb = bar->boundingBox();
        auto* parent = bar->getParent();
        CCPoint bl = parent->convertToWorldSpace(ccp(bb.getMinX(), bb.getMinY()));
        CCPoint tr = parent->convertToWorldSpace(ccp(bb.getMaxX(), bb.getMaxY()));
        const float kPad = 10.f;
        float minX = std::min(bl.x, tr.x) - kPad;
        float maxX = std::max(bl.x, tr.x) + kPad;
        float minY = std::min(bl.y, tr.y) - kPad;
        float maxY = std::max(bl.y, tr.y) + kPad;
        if (worldPos.x < minX || worldPos.x > maxX) return false;
        if (worldPos.y < minY || worldPos.y > maxY) return false;

        mgr.beginDrag(worldPos);
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        ProgressBarManager::get().updateDrag(touch->getLocation());
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        ProgressBarManager::get().endDrag();
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        ProgressBarManager::get().endDrag();
    }
};

class $modify(PaimonProgressBarPlayLayer, PlayLayer) {
    $override
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        ensureProgressBarTicker();
        // reset baseline so it's re-sampled on first enable.
        ProgressBarManager::get().invalidateBaseline();
        this->addOnExitCallback([] {
            auto& mgr = ProgressBarManager::get();
            mgr.invalidateBaseline();
            mgr.releaseCustomTextures();
            return true;
        });
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto& mgr = ProgressBarManager::get();
        if (!mgr.config().enabled) return;
        float startPct = this->getCurrentPercent();
        if (!std::isfinite(startPct)) startPct = 0.f;
        float bestPct = this->m_level
            ? static_cast<float>(this->m_isPracticeMode ? this->m_level->m_practicePercent : this->m_level->m_normalPercent.value())
            : 0.f;
        mgr.notifyLevelReset(startPct, bestPct, this->m_isPlatformer, this->m_isPracticeMode);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool const realHit = player && object != this->m_anticheatSpike
            && (player == this->m_player1 || player == this->m_player2);
        PlayLayer::destroyPlayer(player, object);
        if (realHit && ProgressBarManager::get().config().enabled) {
            float pct = this->getCurrentPercent();
            if (std::isfinite(pct) && pct > 0.f) {
                ProgressBarManager::get().notifyDeath(pct);
            }
        }
    }

    void onQuit() {
        // baseline and custom textures from this level are no longer valid.
        auto& mgr = ProgressBarManager::get();
        mgr.invalidateBaseline();
        mgr.releaseCustomTextures();
        PlayLayer::onQuit();
    }
};

class $modify(PaimonProgressBarPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "PauseLayer::customSetup");
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();
        this->addProgressBarConfigButton();
        this->maybeAttachDragOverlay();
    }

    void maybeAttachDragOverlay() {
        if (!ProgressBarManager::get().isFreeDragActive()) return;
        if (this->getChildByID("paimon-progressbar-drag-layer"_spr)) return;

        auto* dragLayer = ProgressBarDragLayer::create();
        if (!dragLayer) return;
        auto winSize = CCDirector::get()->getWinSize();
        dragLayer->setContentSize(winSize);
        dragLayer->setPosition({0.f, 0.f});
        this->addChild(dragLayer, -1);
    }

    void addProgressBarConfigButton() {
        // avoid duplicates if customsetup runs multiple times.
        if (this->getChildByIDRecursive("paimon-progressbar-config-button"_spr)) return;

        auto* menu = paimon::findPauseButtonMenu(this, "left-button-menu");
        if (!menu) menu = paimon::findPauseButtonMenu(this, "right-button-menu");
        if (!menu) {
            log::warn("[ProgressBar] No suitable menu found in PauseLayer");
            return;
        }

        // Use valid GD 2.2091 frames wrapped in CircleButtonSprite so PauseMenuLayout
        // matches all other sidebar icons cleanly.
        CCSprite* iconGlyph = paimon::SpriteHelper::safeCreateWithFrameName("GJ_timeIcon_001.png");
        if (!iconGlyph) iconGlyph = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
        if (!iconGlyph) iconGlyph = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");

        CCSprite* iconSprite = nullptr;
        if (iconGlyph) {
            iconSprite = CircleButtonSprite::create(
                iconGlyph,
                CircleBaseColor::Green,
                CircleBaseSize::Medium
            );
        }
        if (!iconSprite) {
            iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
        }
        if (!iconSprite) {
            log::warn("[ProgressBar] No fallback icon available for config button");
            return;
        }

        const float kTargetSize = 36.f;
        float cur = std::max(iconSprite->getContentSize().width,
                             iconSprite->getContentSize().height);
        if (cur > 0.f) iconSprite->setScale(kTargetSize / cur);

        auto btn = CCMenuItemSpriteExtra::create(
            iconSprite, this,
            menu_selector(PaimonProgressBarPauseLayer::onOpenProgressBarConfig));
        if (!btn) return;
        btn->setID("paimon-progressbar-config-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onOpenProgressBarConfig(CCObject*) {
        if (auto* popup = ProgressBarConfigPopup::create()) {
            popup->show();
        }
    }
};
