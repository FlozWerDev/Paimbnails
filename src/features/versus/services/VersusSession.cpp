#include "VersusSession.hpp"
#include "VersusGlobed.hpp"
#include "VersusEffects.hpp"
#include "VersusNet.hpp"
#include "VersusRng.hpp"
#include "VersusStore.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace geode::prelude;

namespace paimon::versus {

namespace {

// four a second while running, once every two while waiting in the lobby.
constexpr float kTickInterval = 0.25f;
constexpr float kLobbyPoll = 2.0f;
constexpr float kFoundPoll = 1.0f;
// watching for an incoming friendly, which is nobody's hurry.
constexpr float kWatchPoll = 4.0f;

// a rival that stops ticking for this long is treated as gone; the server still
// decides what that costs them.
constexpr float kRivalTimeout = 30.f;

// closer than this and both runs are a dead heat, in seconds or points.
constexpr float kDeadHeat = 0.05f;

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

VersusSession& VersusSession::get() {
    static VersusSession instance;
    return instance;
}

void VersusSession::addListener(void const* owner, std::function<void()> listener) {
    removeListener(owner);
    m_listeners.emplace_back(owner, std::move(listener));
}

void VersusSession::removeListener(void const* owner) {
    std::erase_if(m_listeners, [owner](auto const& entry) { return entry.first == owner; });
}

void VersusSession::notifyListeners() {
    // a listener can close a popup, which unregisters it mid-walk, so the list
    // is copied before firing.
    auto const snapshot = m_listeners;
    for (auto const& [owner, listener] : snapshot) {
        if (listener) listener();
    }
}

void VersusSession::setPhase(Phase phase) {
    if (m_phase == phase) return;
    m_phase = phase;
    notifyListeners();
}

void VersusSession::reset() {
    m_pollGeneration++;
    m_phase = Phase::Idle;
    m_match = {};
    m_ticket = {};
    m_own = {};
    m_rival = {};
    m_outcome = Outcome::Pending;
    m_eloDelta = 0;
    m_inLevel = false;
    m_submitted = false;
    m_rivalSeen = false;
    m_practice = false;
    m_levelTime = 0.f;
    m_sinceTick = 0.f;
    m_hillHeld = 0.f;
    m_rope = 0.f;
    m_startsIn = 0.f;
    m_rivalSilence = 0.f;
    m_hand.clear();
    m_rivalHand.clear();
    m_milestones.clear();
    m_nextMilestone = 0;
    m_milestoneShift = 0.f;
    m_hourglass = 0.f;
    m_extraAttempts = 0;
    net::setRival(0);
    net::stopListening();
    gl::restoreVisibility();
    gl::clearShield();
}

void VersusSession::beginQueue(Mode mode, Format format) {
    reset();
    m_match.mode = mode;
    m_match.format = format;
    setPhase(Phase::Queued);

    VersusClient::get().joinQueue(mode, format, [this](bool ok, QueueTicket const& ticket) {
        if (!ok) {
            reset();
            notifyListeners();
            return;
        }
        m_ticket = ticket;
        notifyListeners();
        schedulePoll(kFoundPoll);
    });
}

void VersusSession::cancelQueue() {
    if (m_phase != Phase::Queued) return;
    m_pollGeneration++;
    VersusClient::get().leaveQueue([](bool, std::string const&) {});
    reset();
    notifyListeners();
}

void VersusSession::beginWatch() {
    if (m_watching) return;
    m_watching = true;
    scheduleWatch(kWatchPoll);
}

void VersusSession::endWatch() {
    m_watching = false;
    m_watchGeneration++;
}

void VersusSession::scheduleWatch(float delay) {
    uint64_t const generation = ++m_watchGeneration;
    paimon::scheduleMainThreadDelay(delay, [this, generation]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!m_watching || generation != m_watchGeneration) return;
        watchTick();
    });
}

void VersusSession::watchTick() {
    // the queue and the duel run their own poll; doubling it here would only
    // spend requests, so the watch idles until they are done.
    if (!idle() || !VersusClient::get().authenticated()) {
        scheduleWatch(kWatchPoll);
        return;
    }

    VersusClient::get().pollLobby([this](bool ok, MatchInfo const& info) {
        // past the veto it is a duel we already walked out of; adopting it would
        // reopen the lobby on top of a match that is over for us.
        bool const joinable = info.serverPhase == "found" || info.serverPhase == "banning";
        if (ok && !info.id.empty() && joinable && idle()) {
            applyLobby(info);
            // from here the duel owns the lobby: the watch stands down and only
            // the fast poll moves the phase on.
            schedulePoll(kFoundPoll);
        }
        scheduleWatch(kWatchPoll);
    });
}

void VersusSession::schedulePoll(float delay) {
    uint64_t const generation = ++m_pollGeneration;
    paimon::scheduleMainThreadDelay(delay, [this, generation]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (generation != m_pollGeneration) return;
        poll();
    });
}

void VersusSession::poll() {
    if (m_phase == Phase::Idle || m_phase == Phase::Finished) return;

    // once the level is running the fast channel carries everything; polling
    // the lobby there would only spend requests.
    if (m_phase == Phase::Running) {
        schedulePoll(kLobbyPoll * 3.f);
        return;
    }

    VersusClient::get().pollLobby([this](bool ok, MatchInfo const& info) {
        float const next = m_phase == Phase::Queued ? kLobbyPoll : kFoundPoll;
        if (!ok || info.id.empty()) {
            schedulePoll(next);
            return;
        }
        applyLobby(info);
        schedulePoll(next);
    });
}

void VersusSession::applyLobby(MatchInfo const& info) {
    bool const isNew = m_match.id != info.id;

    // the server closed it without us: a dodge, a void, or a rival that walked
    // before the level opened. nothing to submit, just drop back to the hub.
    if (info.serverPhase == "done" && m_phase != Phase::Finished) {
        reset();
        notifyListeners();
        return;
    }

    m_match = info;

    if (isNew) {
        net::setRival(info.rival.accountId);
        wireNet();
        // a friendly opened with a code is already a yes from both sides, so it
        // arrives past the accept step and there is nothing to agree to.
        setPhase(info.serverPhase == "banning" ? Phase::Banning
                 : info.levelId != 0           ? Phase::Loading
                                               : Phase::Found);
        PaimonNotify::show(Localization::get().getString("versus.match-found").c_str(),
                           NotificationIcon::Info);
        return;
    }

    if (info.serverPhase == "banning" && m_phase == Phase::Found) {
        setPhase(Phase::Banning);
        return;
    }
    if (info.levelId != 0 && (m_phase == Phase::Banning || m_phase == Phase::Found)) {
        setPhase(Phase::Loading);
        return;
    }
    // the lobby countdown is the only clock out here; once the level is up
    // onleveltick owns it, so a late answer would rewind it.
    if (info.countdownMs > 0 && !m_inLevel && m_phase != Phase::Running) {
        m_startsIn = info.countdownMs / 1000.f;
        setPhase(Phase::Countdown);
    }
    notifyListeners();
}

void VersusSession::accept(bool yes) {
    if (m_match.id.empty()) return;

    VersusClient::get().acceptMatch(m_match.id, yes, [this, yes](bool ok, std::string const& message) {
        if (!yes || !ok) {
            if (!message.empty()) PaimonNotify::show(message.c_str(), NotificationIcon::Warning);
            reset();
            notifyListeners();
            return;
        }
        setPhase(Phase::Banning);
    });
}

void VersusSession::ban(int levelId) {
    if (m_match.id.empty()) return;

    VersusClient::get().banLevel(m_match.id, levelId, [this](bool ok, MatchInfo const& info) {
        if (!ok) return;
        applyLobby(info);
    });
}

bool VersusSession::enterLevel() {
    if (m_match.levelId == 0) return false;

    auto* glm = GameLevelManager::get();
    if (!glm) return false;

    // the starting pool is main levels, so nothing has to be downloaded before
    // the countdown. online levels come with the curated rotation.
    auto* level = glm->getMainLevel(m_match.levelId, false);
    if (!level) level = glm->getSavedLevel(m_match.levelId);
    if (!level) {
        PaimonNotify::show(Localization::get().getString("versus.level-missing").c_str(),
                           NotificationIcon::Error);
        return false;
    }

    VersusClient::get().reportReady(m_match.id, [this](bool ok, MatchInfo const& info) {
        if (ok) applyLobby(info);
    });

    auto* scene = PlayLayer::scene(level, false, false);
    if (!scene) return false;
    CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    return true;
}

void VersusSession::wireNet() {
    net::Handlers handlers;

    handlers.onTick = [this](int, net::Tick const& tick) {
        m_rivalSeen = true;
        m_rivalSilence = 0.f;
        m_rival.percent = tick.percent;
        m_rival.bestPercent = std::max(m_rival.bestPercent, tick.percent);
        m_rival.attempt = tick.attempt;
        m_rival.alive = tick.alive;
        m_rival.practice = tick.practice;
        m_rival.shielded = tick.shielded;

        m_rivalHand.clear();
        for (auto const card : tick.hand) {
            if (card != CardId::Count) m_rivalHand.push_back(card);
        }
        notifyListeners();
    };

    handlers.onState = [this](int, net::StateMsg const& state) {
        switch (state.kind) {
            case net::StateKind::Death:
                m_rival.alive = false;
                m_rival.deaths = state.detail;
                break;
            case net::StateKind::Segment:
                m_rival.segments |= static_cast<uint8_t>(1u << std::min<uint8_t>(state.value, 3));
                break;
            case net::StateKind::Finish:
                m_rival.finished = true;
                m_rival.percent = 100.f;
                m_rival.finishTime = state.levelTime;
                break;
            case net::StateKind::Forfeit:
                m_rival.forfeited = true;
                m_rival.alive = false;
                m_rival.percent = 0.f;
                break;
            case net::StateKind::Spent:
                m_rival.spent = true;
                m_rival.alive = false;
                break;
            case net::StateKind::Revive:
                m_rival.spent = false;
                break;
            default:
                break;
        }
        evaluate();
        notifyListeners();
    };

    handlers.onCard = [this](int, net::CardMsg const& msg) {
        receiveCard(msg.card, msg.reflected);
        notifyListeners();
    };

    handlers.onTaunt = [](int, uint8_t) {};

    net::listen(std::move(handlers));
}

void VersusSession::onLevelStarted(PlayLayer* layer) {
    m_inLevel = true;
    VersusEffects::get().attach(layer);
    m_levelTime = 0.f;
    m_sinceTick = 0.f;
    m_hillHeld = 0.f;
    m_rope = 0.f;
    m_rivalSilence = 0.f;
    m_practice = false;
    m_own = {};
    m_submitted = false;
    m_hand.clear();
    m_nextMilestone = 0;
    m_milestoneShift = 0.f;
    m_hourglass = 0.f;
    m_extraAttempts = 0;
    buildMilestones();

    if (m_match.rival.accountId != 0) {
        // a duel in the global room still has to look like a duel.
        gl::isolateRival(m_match.rival.accountId);
    }
    setPhase(m_startsIn > 0.f ? Phase::Countdown : Phase::Running);
}

void VersusSession::onLevelTick(float dt, float percent, int attempt, bool practice) {
    if (!m_inLevel) return;

    if (m_phase == Phase::Countdown) {
        m_startsIn -= dt;
        if (m_startsIn <= 0.f) {
            m_startsIn = 0.f;
            setPhase(Phase::Running);
        }
        return;
    }
    if (m_phase != Phase::Running) return;

    VersusEffects::get().update(dt);

    m_levelTime += dt;

    if (m_rivalSeen) {
        m_rivalSilence += dt;
        if (m_rivalSilence > kRivalTimeout && !m_rival.finished) {
            m_rival.alive = false;
        }
    }

    // practice runs and spent attempts park the percent, but the shared state keeps
    // moving: frozen numbers would let one side call the duel on its own.
    m_practice = practice;
    bool const counts = !practice && !m_own.spent;
    if (counts) {
        m_own.percent = percent;
        m_own.bestPercent = std::max(m_own.bestPercent, percent);
        m_own.attempt = attempt;
        m_own.alive = true;
    }

    auto const& def = format();
    if (def.id == Format::Ladder || def.id == Format::Relay) {
        if (counts) claimSegment(segmentForPercent(m_own.percent));
    } else if (def.id == Format::KingOfTheHill) {
        m_hillHeld = m_own.percent > m_rival.percent ? m_hillHeld + dt : 0.f;
    } else if (def.id == Format::TugOfWar) {
        // both clients run this off the same two percentages, so the rope lands
        // on the same side without anything having to be sent.
        float const pull = (m_own.percent - m_rival.percent) / kRopeLead / kRopeSeconds;
        m_rope = std::clamp(m_rope + pull * dt, -1.f, 1.f);
    }

    if (counts) {
        if (m_hourglass > 0.f) m_hourglass = std::max(0.f, m_hourglass - dt);
        checkMilestones();
    }

    m_sinceTick += dt;
    if (m_sinceTick >= kTickInterval) pushTick(false);

    evaluate();
}

bool VersusSession::hasMutator(std::string const& id) const {
    return std::find(m_match.mutators.begin(), m_match.mutators.end(), id)
        != m_match.mutators.end();
}

bool VersusSession::dealsCards() const {
    return format().cards && paimon::modules::isEnabled("paimbnails.versus.cards");
}

void VersusSession::buildMilestones() {
    m_milestones.clear();
    if (!dealsCards() || m_match.seed == 0) return;
    m_milestones = rollMilestones(m_match.seed);
}

void VersusSession::checkMilestones() {
    if (m_milestones.empty() || m_nextMilestone >= m_milestones.size()) return;

    // spark pulls the next one closer, the hourglass halves what is left to
    // every one of them; both only ever move our own copy of the list.
    float threshold = m_milestones[m_nextMilestone] - m_milestoneShift;
    if (m_hourglass > 0.f) {
        threshold = m_own.percent + (threshold - m_own.percent) * 0.5f;
    }
    if (m_own.percent < threshold) return;

    m_nextMilestone++;
    m_milestoneShift = 0.f;
    drawCard();
}

void VersusSession::drawCard() {
    uint8_t const modeMask = m_match.mode == Mode::Platformer ? ModePlatformer : ModeClassic;
    float const deficit = std::max(0.f, m_rival.percent - m_own.percent);

    auto const card = rollCard(m_match.seed, static_cast<int>(m_nextMilestone),
                               modeMask, deficit, m_match.catchUp);

    m_hand.push_back(card);
    // a third card pushes the oldest out, which is what makes holding a
    // legendary through a hard section an actual decision.
    if (static_cast<int>(m_hand.size()) > kHandSize) m_hand.erase(m_hand.begin());
    notifyListeners();
}

bool VersusSession::playCard(int slot) {
    if (m_phase != Phase::Running || !m_inLevel) return false;
    if (slot < 0 || slot >= static_cast<int>(m_hand.size())) return false;
    if (VersusEffects::get().cardsLocked()) return false;

    auto const card = m_hand[slot];
    m_hand.erase(m_hand.begin() + slot);

    auto const& def = cardAt(card);
    switch (def.target) {
        case CardTarget::Self:
            // heart is bookkeeping, not an effect: the attempt limit is the session's
            // to move, and played on the spending death it hands the run back.
            if (card == CardId::Heart) {
                m_extraAttempts++;
                if (m_own.spent && m_own.attempt < attemptLimit()) {
                    m_own.spent = false;
                    net::sendState({net::StateKind::Revive, 0, 0, m_levelTime});
                }
            }
            VersusEffects::get().apply(card, false);
            break;
        case CardTarget::Rival:
            break;
        case CardTarget::Both:
            VersusEffects::get().apply(card, false);
            if (card == CardId::Hourglass) m_hourglass = def.duration;
            if (card == CardId::Dice) drawCard();
            if (card == CardId::Swap) m_hand.clear();
            break;
    }
    if (card == CardId::Bolt) m_milestoneShift = 4.f;

    net::sendCard({card, static_cast<uint8_t>(m_nextMilestone), false, m_levelTime});
    notifyListeners();
    return true;
}

void VersusSession::receiveCard(CardId card, bool alreadyReflected) {
    auto const& def = cardAt(card);

    if (def.target == CardTarget::Rival) {
        // rebound sends it straight back instead of eating it, and a card that
        // has already bounced once cannot bounce again.
        if (!alreadyReflected && VersusEffects::get().consumeReflect()) {
            net::sendCard({card, 0, true, m_levelTime});
            return;
        }
        VersusEffects::get().apply(card, true);
        return;
    }

    if (def.target == CardTarget::Both) {
        if (card == CardId::Hourglass) m_hourglass = def.duration;
        if (card == CardId::Dice) drawCard();
        if (card == CardId::Swap) m_hand.clear();
        VersusEffects::get().apply(card, true);
        return;
    }

    // wraith is cast on themselves but acted on here: we are the ones who have
    // to stop drawing them.
    if (card == CardId::Ghost) VersusEffects::get().apply(card, true);
}

void VersusSession::pushTick(bool force) {
    if (!force && m_sinceTick < kTickInterval) return;
    m_sinceTick = 0.f;

    net::Tick tick;
    tick.percent = m_own.percent;
    tick.levelTime = m_levelTime;
    tick.attempt = m_own.attempt;
    tick.alive = m_own.alive;
    tick.practice = m_practice;
    tick.shielded = gl::shieldActive();
    for (size_t i = 0; i < m_hand.size() && i < std::size(tick.hand); i++) {
        tick.hand[i] = m_hand[i];
    }
    net::sendTick(tick);
}

int VersusSession::attemptLimit() const {
    auto const& def = format();
    if (def.attemptLimit <= 0) return 0;
    return def.attemptLimit + m_extraAttempts;
}

void VersusSession::onDeath() {
    if (!m_inLevel || m_phase != Phase::Running) return;
    // past the limit the run is already over; what follows is the player
    // restarting, and announcing it again would only flood the rival.
    if (m_own.spent) return;

    m_own.deaths++;
    m_own.alive = false;
    net::sendState({net::StateKind::Death, 0, static_cast<uint16_t>(m_own.deaths), m_levelTime});

    // one life each, so the death is the whole result. it goes before the
    // attempt limit or the limit of one would swallow it.
    if (format().id == Format::SuddenDeath) {
        finish(m_rival.alive ? Outcome::Loss : Outcome::Draw);
        return;
    }

    int const limit = attemptLimit();
    if (limit > 0 && m_own.attempt >= limit) {
        m_own.spent = true;
        net::sendState({net::StateKind::Spent, 0, 0, m_levelTime});
        evaluate();
    }
}

void VersusSession::claimSegment(int segment) {
    if (segment < 0 || segment >= kLadderSegments) return;

    uint8_t const bit = static_cast<uint8_t>(1u << segment);
    if ((m_own.segments & bit) || (m_rival.segments & bit)) return;

    m_own.segments |= bit;
    net::sendState({net::StateKind::Segment, static_cast<uint8_t>(segment), 0, m_levelTime});
}

void VersusSession::onComplete() {
    if (!m_inLevel) return;

    // the last segment is the finish line itself, and the tick does not always
    // report a clean 100 before the level ends.
    auto const& def = format();
    if (def.id == Format::Ladder || def.id == Format::Relay) claimSegment(kLadderSegments - 1);

    m_own.finished = true;
    m_own.percent = 100.f;
    m_own.bestPercent = 100.f;
    m_own.finishTime = m_levelTime;
    net::sendState({net::StateKind::Finish, 0, 0, m_levelTime});
    evaluate();
}

void VersusSession::evaluate() {
    if (m_phase != Phase::Running || m_submitted) return;

    auto const& def = format();

    // walking out hands the duel over whatever else is on the board.
    if (m_rival.forfeited) {
        finish(Outcome::Win);
        return;
    }

    if (def.id == Format::Relay) {
        evaluateRelay();
        return;
    }

    if (m_own.finished && !m_rival.finished) {
        finish(Outcome::Win);
        return;
    }
    if (m_rival.finished && !m_own.finished) {
        // time attack is the one format where finishing second can still win.
        if (def.id != Format::TimeAttack) finish(Outcome::Loss);
        return;
    }
    if (m_own.finished && m_rival.finished) {
        if (def.id == Format::TimeAttack || def.id == Format::Race) {
            finishOnGap(m_rival.finishTime - m_own.finishTime);
        } else {
            finish(Outcome::Draw);
        }
        return;
    }

    if (def.id == Format::Ladder) {
        int own = 0, rival = 0;
        for (int i = 0; i < kLadderSegments; i++) {
            if (m_own.segments & (1u << i)) own++;
            if (m_rival.segments & (1u << i)) rival++;
        }
        if (own >= kLadderToWin) { finish(Outcome::Win); return; }
        if (rival >= kLadderToWin) { finish(Outcome::Loss); return; }
    } else if (def.id == Format::KingOfTheHill && m_hillHeld >= kHillSeconds) {
        finish(Outcome::Win);
        return;
    } else if (def.id == Format::TugOfWar && std::fabs(m_rope) >= 1.f) {
        finish(m_rope > 0.f ? Outcome::Win : Outcome::Loss);
        return;
    } else if (def.id == Format::SuddenDeath && !m_rival.alive && m_own.alive) {
        finish(Outcome::Win);
        return;
    }

    // both sides out of attempts, or the clock ran out: whoever got further
    // takes it. nothing else can move now.
    if (m_own.spent && m_rival.spent) {
        finishOnPercent();
        return;
    }
    if (def.timeLimit > 0 && m_levelTime >= static_cast<float>(def.timeLimit)) {
        finishOnPercent();
    }
}

// segments are claimed once and never handed over, so a 90% run keeps what
// it took: closing the last one doesn't always win.
void VersusSession::evaluateRelay() {
    int own = 0, rival = 0;
    for (int i = 0; i < kLadderSegments; i++) {
        if (m_own.segments & (1u << i)) own++;
        if (m_rival.segments & (1u << i)) rival++;
    }

    auto const& def = format();
    bool const closed = own + rival >= kLadderSegments;
    bool const outOfTime = def.timeLimit > 0 && m_levelTime >= static_cast<float>(def.timeLimit);
    if (!closed && !outOfTime) return;

    if (own != rival) {
        finish(own > rival ? Outcome::Win : Outcome::Loss);
        return;
    }
    finishOnPercent();
}

void VersusSession::finishOnPercent() {
    finishOnGap(m_own.bestPercent - m_rival.bestPercent);
}

void VersusSession::finishOnGap(float gap) {
    if (std::fabs(gap) < kDeadHeat) finish(Outcome::Draw);
    else finish(gap > 0.f ? Outcome::Win : Outcome::Loss);
}

void VersusSession::finish(Outcome outcome) {
    if (m_submitted) return;
    m_submitted = true;
    m_outcome = outcome;
    setPhase(Phase::Finished);

    int const before = VersusStore::get().profile(m_match.mode).elo;

    // read everything the record needs here: reset() may run mid-request and
    // orphan the callback's row.
    MatchRecord record;
    record.id = m_match.id;
    record.rival = m_match.rival.name;
    record.levelId = m_match.levelId;
    record.mode = m_match.mode;
    record.format = m_match.format;
    record.outcome = outcome;
    record.ownPercent = m_own.bestPercent;
    record.rivalPercent = m_rival.bestPercent;
    record.playedAt = nowSeconds();

    VersusClient::get().submitResult(m_match.id, m_own, m_rival, outcome,
        [this, before, record](bool ok, std::string const& message) mutable {
            if (!ok) {
                log::warn("[Versus][Session] Result rejected: {}", message);
                return;
            }
            record.eloDelta = VersusStore::get().profile(record.mode).elo - before;
            VersusStore::get().pushRecord(record);

            // the duel is still on screen only if nobody has left it yet.
            if (m_match.id != record.id) return;
            m_eloDelta = record.eloDelta;
            notifyListeners();
        });
}

void VersusSession::forfeit() {
    if (m_match.id.empty() || m_submitted) return;

    net::sendState({net::StateKind::Forfeit, 0, 0, m_levelTime});
    m_own.forfeited = true;
    m_submitted = true;
    m_outcome = Outcome::Loss;
    setPhase(Phase::Finished);

    VersusClient::get().forfeit(m_match.id, [this](bool, std::string const&) {
        notifyListeners();
    });
}

void VersusSession::onLevelLeft() {
    if (!m_inLevel) return;
    m_inLevel = false;

    VersusEffects::get().detach();
    gl::restoreVisibility();
    gl::clearShield();

    // leaving a paired duel forfeits; the server would rule it one once the rival
    // submits, and the countdown counts or the next entry inherits a claimed match.
    if ((m_phase == Phase::Running || m_phase == Phase::Countdown) && !m_submitted) forfeit();
}

float VersusSession::countdownLeft() const {
    return std::max(0.f, m_startsIn);
}

float VersusSession::timeLeft() const {
    auto const& def = format();
    if (def.timeLimit <= 0) return 0.f;
    return std::max(0.f, static_cast<float>(def.timeLimit) - m_levelTime);
}

std::string VersusSession::statusLine() const {
    auto& loc = Localization::get();
    switch (m_phase) {
        case Phase::Idle:      return {};
        case Phase::Queued:    return loc.getString("versus.status.queued");
        case Phase::Found:     return loc.getString("versus.status.found");
        case Phase::Banning:   return loc.getString("versus.status.banning");
        case Phase::Loading:   return loc.getString("versus.status.loading");
        case Phase::Countdown: return loc.getString("versus.status.countdown");
        case Phase::Running:   return loc.getString("versus.status.running");
        case Phase::Finished:  return loc.getString("versus.status.finished");
    }
    return {};
}

} // namespace paimon::versus
