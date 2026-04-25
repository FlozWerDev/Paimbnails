#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include "../services/ProgressBarManager.hpp"
#include "../ui/ProgressBarConfigPopup.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// ─────────────────────────────────────────────────────────────
// Ticker: a dedicated CCNode registered with the GLOBAL scheduler
// so it keeps ticking even while gameplay is paused (otherwise
// changes from the config popup would not apply live while the
// pause menu is open).
// ─────────────────────────────────────────────────────────────

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
        auto* pl = PlayLayer::get();
        if (!pl) return;
        ProgressBarManager::get().applyToPlayLayer(pl);
    }
};

static geode::Ref<ProgressBarTicker> s_progressBarTicker = nullptr;

static void ensureProgressBarTicker() {
    if (s_progressBarTicker) return;
    s_progressBarTicker = ProgressBarTicker::create();
    if (!s_progressBarTicker) return;
    CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(
        s_progressBarTicker.data(), 0, false
    );
}

static void shutdownProgressBarTicker() {
    if (!s_progressBarTicker) return;
    if (auto* dir = CCDirector::sharedDirector()) {
        if (auto* sch = dir->getScheduler()) {
            sch->unscheduleUpdateForTarget(s_progressBarTicker.data());
        }
    }
    (void)s_progressBarTicker.take();
}

$on_game(Exiting) {
    shutdownProgressBarTicker();
}

// ─────────────────────────────────────────────────────────────
// Drag overlay: a full-screen touch-receiving CCLayer that
// allows the user to grab the progress bar and drag it around.
// Added to the PauseLayer when free-drag is active so that it
// only intercepts touches while the menu is open.
// ─────────────────────────────────────────────────────────────

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
        // Priority just above the PauseLayer (which is typically -128)
        CCDirector::sharedDirector()->getTouchDispatcher()
            ->addTargetedDelegate(this, -129, true);
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        auto& mgr = ProgressBarManager::get();
        if (!mgr.isFreeDragActive()) return false;

        auto* pl = PlayLayer::get();
        if (!pl) return false;

        // Find the bar and test if the touch intersects its AABB.
        CCNode* bar = pl->getChildByIDRecursive("progress-bar");
        if (!bar) return false;

        auto worldPos = touch->getLocation();

        // Build an AABB for the bar in world space. Use getBoundingBox
        // and convert to world coordinates.
        auto bb = bar->boundingBox();
        auto parent = bar->getParent();
        if (!parent) return false;
        CCPoint bl = parent->convertToWorldSpace(ccp(bb.getMinX(), bb.getMinY()));
        CCPoint tr = parent->convertToWorldSpace(ccp(bb.getMaxX(), bb.getMaxY()));
        float minX = std::min(bl.x, tr.x) - 10.f;
        float maxX = std::max(bl.x, tr.x) + 10.f;
        float minY = std::min(bl.y, tr.y) - 10.f;
        float maxY = std::max(bl.y, tr.y) + 10.f;
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

// ─────────────────────────────────────────────────────────────
// PlayLayer hook: install / remove ticker node.
// ─────────────────────────────────────────────────────────────

class $modify(PaimonProgressBarPlayLayer, PlayLayer) {
    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        // Make sure the global ticker exists — it keeps ticking while
        // paused so popup edits apply instantly.
        ensureProgressBarTicker();
        // Fresh level → re-sample vanilla baseline if user re-enables.
        ProgressBarManager::get().invalidateBaseline();
        return true;
    }

    $override
    void onQuit() {
        // Baseline & custom texture hosts from this level are no longer valid.
        ProgressBarManager::get().invalidateBaseline();
        ProgressBarManager::get().releaseCustomTextures();
        PlayLayer::onQuit();
    }
};

// ─────────────────────────────────────────────────────────────
// PauseLayer hook: adds a "config" button to the pause menu and
// attaches the drag overlay when free-drag is enabled.
// ─────────────────────────────────────────────────────────────

class $modify(PaimonProgressBarPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("PauseLayer::customSetup", "geode.node-ids");
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();
        this->addProgressBarConfigButton();

        if (!ProgressBarManager::get().isFreeDragActive()) return;
        if (this->getChildByID("paimon-progressbar-drag-layer"_spr)) return;

        auto* dragLayer = ProgressBarDragLayer::create();
        if (!dragLayer) return;
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        dragLayer->setContentSize(winSize);
        dragLayer->setPosition({0.f, 0.f});
        this->addChild(dragLayer, -1);
    }

    void addProgressBarConfigButton() {
        // Avoid duplicates if customSetup runs multiple times.
        if (this->getChildByID("paimon-progressbar-config-button"_spr)) return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // Find a suitable menu (prefer geode.node-ids "left-button-menu").
        auto pickMenu = [&](char const* id, bool leftSide) -> CCMenu* {
            if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID(id))) return byId;
            CCMenu* best = nullptr;
            float bestScore = 0.f;
            for (auto* obj : CCArrayExt<CCNode*>(this->getChildren())) {
                auto menu = typeinfo_cast<CCMenu*>(obj);
                if (!menu) continue;
                float x = menu->getPositionX();
                bool sideMatch = leftSide ? (x < winSize.width * 0.5f) : (x > winSize.width * 0.5f);
                if (!sideMatch) continue;
                float score = menu->getChildrenCount();
                if (!best || score > bestScore) {
                    best = menu;
                    bestScore = score;
                }
            }
            return best;
        };

        auto* menu = pickMenu("left-button-menu", true);
        if (!menu) menu = pickMenu("right-button-menu", false);
        if (!menu) return;

        // Build icon. Try a few GD sprite frames that resemble a bar /
        // settings icon; fall back to the generic button background.
        CCSprite* iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_percentagesBtn_001.png");
        if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
        if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
        if (!iconSprite) return;

        float target = 30.f;
        float cur = std::max(iconSprite->getContentSize().width, iconSprite->getContentSize().height);
        if (cur > 0.f) iconSprite->setScale(target / cur);

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
