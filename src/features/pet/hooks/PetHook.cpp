#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include "../services/PetManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

class PetTickerNode : public CCNode {
    int m_frameCounter = 0;
    CCScene* m_lastScene = nullptr;

public:
    static PetTickerNode* create() {
        auto ret = new PetTickerNode();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        this->setID("paimon-pet-ticker"_spr);
        return true;
    }

    void update(float dt) override {
        auto& pet = PetManager::get();

        if (pet.isAttached()) {
            pet.update(dt);
        }

        if (++m_frameCounter % 6 != 0) return;

        if (!pet.config().enabled) {
            if (pet.isAttached()) pet.detachFromScene();
            return;
        }

        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;

        if (!pet.isAttached() || (pet.isAttached() && m_lastScene != scene)) {
            pet.attachToScene(scene);
            m_lastScene = scene;
        }
    }
};

// Ref<> keeps the node alive so the scheduler doesn't free it.
static Ref<PetTickerNode> s_petTicker = nullptr;

void shutdownPetTicker() {
    if (!s_petTicker) return;

    if (auto* director = CCDirector::get()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleUpdateForTarget(s_petTicker.data());
        }
    }

    (void)s_petTicker.take();
}

void initPetTicker() {
    if (s_petTicker) return;
    s_petTicker = PetTickerNode::create();
    // Register with the global scheduler directly (paused=false);
    // CCNode::scheduleUpdate() requires the node to be in a running scene.
    CCDirector::get()->getScheduler()->scheduleUpdateForTarget(
        s_petTicker.data(), 0, false
    );
}

$on_game(Exiting) {
    shutdownPetTicker();
}

// Game event hooks — trigger pet reactions

// Defer the reaction to the next main-thread tick for a clean stack.
static void deferPetReaction(std::string eventType) {
    Loader::get()->queueInMainThread([eventType = std::move(eventType)]() {
        if (paimon::isRuntimeShuttingDown()) return;
        PetManager::get().triggerReaction(eventType);
    });
}

// Level complete (normal mode)
class $modify(PetPlayLayerHook, PlayLayer) {
    static void onModify(auto& self) {
        // Run late, out of the notifyAchievement/AchievementBar stack.
        (void)self.setHookPriorityPost("PlayLayer::levelComplete", geode::Priority::Late);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        deferPetReaction("level_complete");
    }
};

// Player death
class $modify(PetPlayerObjectHook, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("PlayerObject::playerDestroyed", geode::Priority::Late);
    }

    void playerDestroyed(bool p0) {
        PlayerObject::playerDestroyed(p0);
        auto* pl = PlayLayer::get();
        if (!pl) return;

        // Trigger once per death sequence (primary player only in dual mode).
        if (this == pl->m_player1) {
            deferPetReaction("death");
        }
    }
};

// Practice mode exit — hooking PauseLayer::onQuit.
class $modify(PetPauseLayerHook, PauseLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("PauseLayer::onQuit", geode::Priority::Late);
    }

    void onQuit(cocos2d::CCObject* sender) {
        auto* pl = PlayLayer::get();
        if (pl && pl->m_isPracticeMode) {
            deferPetReaction("practice_exit");
        }
        PauseLayer::onQuit(sender);
    }
};
