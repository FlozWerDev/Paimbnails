#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include "../services/PetManager.hpp"

using namespace geode::prelude;

// ────────────────────────────────────────────────────────────
// PetTickerNode: nodo dedicado que ejecuta la logica del pet cada frame
// via scheduleUpdateForTarget. Reemplaza el hook a CCScheduler::update
// (patron explicitamente desaconsejado por las guidelines de Geode).
// ────────────────────────────────────────────────────────────

class PetTickerNode : public CCNode {
    int m_frameCounter = 0;

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

        // la animacion del pet necesita correr cada frame si esta attached
        if (pet.isAttached()) {
            pet.update(dt);
        }

        // los chequeos de config/scene/visibility son costosos a 60fps
        // throttle: solo cada ~6 frames (unos 100ms a 60fps)
        if (++m_frameCounter % 6 != 0) return;

        if (!pet.config().enabled) {
            if (pet.isAttached()) pet.detachFromScene();
            return;
        }

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        if (!pet.shouldShowOnCurrentScene()) {
            if (pet.isAttached()) pet.detachFromScene();
            return;
        }

        if (!pet.isAttached()) {
            pet.attachToScene(scene);
        }
    }
};

// Ref<> retiene el nodo para que el scheduler no lo libere
static Ref<PetTickerNode> s_petTicker = nullptr;

void shutdownPetTicker() {
    if (!s_petTicker) return;

    if (auto* director = CCDirector::sharedDirector()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleUpdateForTarget(s_petTicker.data());
        }
    }

    (void)s_petTicker.take();
}

void initPetTicker() {
    if (s_petTicker) return;
    s_petTicker = PetTickerNode::create();
    // Registrar directamente con el scheduler global (paused=false).
    // No usamos CCNode::scheduleUpdate() porque requiere que el nodo
    // este en un running scene (m_bRunning==true) para no pausarse.
    CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(
        s_petTicker.data(), 0, false
    );
}

$on_game(Exiting) {
    shutdownPetTicker();
}

// ════════════════════════════════════════════════════════════
// Game event hooks — trigger pet reactions
// ════════════════════════════════════════════════════════════

// Level complete (normal mode)
class $modify(PetPlayLayerHook, PlayLayer) {
    void levelComplete() {
        PlayLayer::levelComplete();
        PetManager::get().triggerReaction("level_complete");
    }
};

// Player death
class $modify(PetPlayerObjectHook, PlayerObject) {
    void playerDestroyed(bool p0) {
        PlayerObject::playerDestroyed(p0);
        PetManager::get().triggerReaction("death");
    }
};

// Practice mode exit — hook into GJGameLevel::savePercentage or
// the practice mode toggle. Using PauseLayer hook for practice exit.
class $modify(PetPauseLayerHook, PauseLayer) {
    void onQuit(cocos2d::CCObject* sender) {
        // check if we're in practice mode
        auto* pl = PlayLayer::get();
        if (pl && pl->m_isPracticeMode) {
            PetManager::get().triggerReaction("practice_exit");
        }
        PauseLayer::onQuit(sender);
    }
};
