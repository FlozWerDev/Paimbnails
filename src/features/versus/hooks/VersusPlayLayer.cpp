#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "../services/VersusSession.hpp"
#include "../services/VersusStore.hpp"
#include "../ui/VersusEndPopup.hpp"
#include "../ui/VersusHUDNode.hpp"
#include "../ui/VersusHandNode.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"

using namespace geode::prelude;
using namespace paimon::versus;

namespace {

constexpr char const* kHudId = "versus-hud"_spr;

bool duelRunning() {
    auto const& session = VersusSession::get();
    return session.inLevel() && !session.idle();
}

} // namespace

class $modify(PaimonVersusPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "PlayLayer::init");
    }

    struct Fields {
        VersusHUDNode* m_hud = nullptr;
        VersusHandNode* m_hand = nullptr;
        Outcome m_shown = Outcome::Pending;
    };

    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto& session = VersusSession::get();
        if (session.idle() || session.phase() == Phase::Finished) return true;
        if (!level || level->m_levelID.value() != session.match().levelId) return true;

        session.onLevelStarted(this);

        if (paimon::modules::isEnabled("paimbnails.versus.hud") && VersusStore::get().hudEnabled()) {
            if (auto* hud = VersusHUDNode::create()) {
                hud->setID(kHudId);
                this->addChild(hud, 1000);
                m_fields->m_hud = hud;
                if (session.countingDown()) hud->playCountdown(session.countdownLeft());
            }
        }

        // The hand is its own module: turning the bars off is not a reason to
        // deal cards nobody can see or play.
        if (session.dealsCards()) {
            if (auto* hand = VersusHandNode::create()) {
                hand->setID("versus-hand"_spr);
                this->addChild(hand, 1000);
                m_fields->m_hand = hand;
            }
            bindCardKeys();
        }
        return true;
    }

    // Keys come from the mod's keybind settings so they stay rebindable; the
    // listener holds a plain pointer because a Ref would leak the level per duel.
    void bindCardKeys() {
        for (int slot = 0; slot < 2; slot++) {
            auto const key = slot == 0 ? "versus-card-1-keybind" : "versus-card-2-keybind";
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), key),
                [this, slot](Keybind const&, bool down, bool repeat, double) {
                    if (!down || repeat || !this->isRunning()) return;
                    VersusSession::get().playCard(slot);
                });
        }
    }

    $override
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!duelRunning()) return;

        auto& session = VersusSession::get();
        session.onLevelTick(dt, this->getCurrentPercent(), m_attempts, m_isPracticeMode);

        if (m_fields->m_hud) m_fields->m_hud->refresh();

        if (session.phase() == Phase::Finished && m_fields->m_shown == Outcome::Pending) {
            m_fields->m_shown = session.outcome();
            if (m_fields->m_hud) m_fields->m_hud->showResult(session.outcome());
        }
    }

    $override
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        // The hook also fires for the second player of a dual, and for deaths
        // that safe mode swallowed; only the real one counts.
        if (!duelRunning() || player != m_player1 || m_isPracticeMode) return;
        VersusSession::get().onDeath();
    }

    $override
    void levelComplete() {
        PlayLayer::levelComplete();
        if (!duelRunning() || m_isPracticeMode) return;
        VersusSession::get().onComplete();
    }

    // no-practice: the toggle simply does not take while the duel is running.
    void togglePracticeMode(bool practice) {
        if (practice && duelRunning() && VersusSession::get().hasMutator("no-practice")) {
            PaimonNotify::show(Localization::get().getString("versus.mutator.no-practice").c_str(),
                               NotificationIcon::Warning);
            return;
        }
        PlayLayer::togglePracticeMode(practice);
    }

    $override
    void onQuit() {
        if (duelRunning()) VersusSession::get().onLevelLeft();
        PlayLayer::onQuit();
    }

    $override
    void onExit() {
        auto& session = VersusSession::get();
        bool const finished = session.phase() == Phase::Finished;
        auto const outcome = session.outcome();

        if (session.inLevel()) session.onLevelLeft();
        PlayLayer::onExit();

        // The result popup belongs to whatever scene comes next, so it is
        // queued rather than parented to a layer that is going away.
        if (finished && outcome != Outcome::Pending) {
            paimon::scheduleMainThreadDelay(0.35f, []() {
                if (auto* popup = VersusEndPopup::create()) popup->show();
            });
        }
    }
};
