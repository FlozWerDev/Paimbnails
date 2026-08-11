#include <Geode/Geode.hpp>
#include "../../../framework/HookConventions.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "../services/ProgressBarManager.hpp"
#include "../ui/ProgressBarConfigPopup.hpp"
#include "../../gameplay-performance/GameplayPerformance.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// Registered with the global scheduler so it keeps ticking even while gameplay is paused.
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
        // Returns nullptr outside gameplay.
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kModVisualsModuleId)) return;
        if (auto* pl = PlayLayer::get()) {
            ProgressBarManager::get().applyToPlayLayer(pl);
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

// Drag overlay: full-screen touch layer that lets the user grab and move
// the progress bar. Added to PauseLayer when free-drag is active.
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
        // Priority just above PauseLayer (typically -128).
        CCDirector::get()->getTouchDispatcher()
            ->addTargetedDelegate(this, -129, true);
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        auto& mgr = ProgressBarManager::get();
        if (!mgr.isFreeDragActive()) return false;

        auto* pl = PlayLayer::get();
        if (!pl) return false;

        CCNode* bar = pl->getChildByIDRecursive("progress-bar");
        if (!bar || !bar->getParent()) return false;

        auto worldPos = touch->getLocation();

        // Build an AABB for the bar in world space with a 10px grab margin.
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

// PlayLayer hook: install ticker, invalidate baseline per level.

class $modify(PaimonProgressBarPlayLayer, PlayLayer) {
    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        ensureProgressBarTicker();
        // Reset baseline so it's re-sampled on first enable.
        ProgressBarManager::get().invalidateBaseline();
        return true;
    }

    $override
    void onQuit() {
        // Baseline and custom textures from this level are no longer valid.
        auto& mgr = ProgressBarManager::get();
        mgr.invalidateBaseline();
        mgr.releaseCustomTextures();
        PlayLayer::onQuit();
    }
};

// PauseLayer hook: adds a config button and attaches the drag overlay when free-drag is active.
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
        // Avoid duplicates if customSetup runs multiple times.
        if (this->getChildByID("paimon-progressbar-config-button"_spr)) return;
        auto winSize = CCDirector::get()->getWinSize();

        // Find a suitable menu (prefer geode.node-ids id, fall back to side-detection).
        auto pickMenu = [&](char const* id, bool leftSide) -> CCMenu* {
            if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID(id))) return byId;
            CCMenu* best = nullptr;
            float bestScore = -1.f;
            if (auto* children = this->getChildren()) {
                for (auto* obj : CCArrayExt<CCNode*>(children)) {
                    auto menu = typeinfo_cast<CCMenu*>(obj);
                    if (!menu) continue;
                    float x = menu->getPositionX();
                    bool sideMatch = leftSide
                        ? (x < winSize.width * 0.5f)
                        : (x > winSize.width * 0.5f);
                    if (!sideMatch) continue;
                    float score = static_cast<float>(menu->getChildrenCount());
                    if (!best || score > bestScore) {
                        best = menu;
                        bestScore = score;
                    }
                }
            }
            return best;
        };

        auto* menu = pickMenu("left-button-menu", true);
        if (!menu) menu = pickMenu("right-button-menu", false);
        if (!menu) {
            log::warn("[ProgressBar] No suitable menu found in PauseLayer");
            return;
        }

        // Icon: try GD bar/settings frames, fall back to a generic button.
        CCSprite* iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_percentagesBtn_001.png");
        if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
        if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
        if (!iconSprite) {
            log::warn("[ProgressBar] No fallback icon available for config button");
            return;
        }

        const float kTargetSize = 30.f;
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
