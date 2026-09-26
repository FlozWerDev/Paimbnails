#pragma once

#include "../data/VersusModes.hpp"
#include "../data/VersusTypes.hpp"
#include "../data/VersusCards.hpp"
#include "VersusClient.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

class PlayLayer;

namespace paimon::versus {

class VersusSession {
public:
    static VersusSession& get();

    Phase phase() const { return m_phase; }
    bool idle() const { return m_phase == Phase::Idle; }
    bool inLevel() const { return m_inLevel; }

    MatchInfo const& match() const { return m_match; }
    FormatDef const& format() const { return formatAt(m_match.format); }
    SideState const& own() const { return m_own; }
    SideState const& rival() const { return m_rival; }
    Outcome outcome() const { return m_outcome; }
    int eloDelta() const { return m_eloDelta; }

    // The hub and the duel modal are both on screen at once, so listeners are
    // keyed by owner instead of being a single slot one of them would clobber.
    void addListener(void const* owner, std::function<void()> listener);
    void removeListener(void const* owner);

    void beginQueue(Mode mode, Format format);
    QueueTicket const& ticket() const { return m_ticket; }
    void cancelQueue();

    // Slow polling catches incoming friendlies only while no other request is active.
    void beginWatch();
    void endWatch();

    void accept(bool yes);
    void ban(int levelId);
    // loads the level and pushes PlayLayer; false voids the match instead of
    // hanging on a download.
    bool enterLevel();

    void onLevelStarted(PlayLayer* layer);
    void onLevelTick(float dt, float percent, int attempt, bool practice);
    void onDeath();
    void onComplete();
    void onLevelLeft();

    // Roulette: the milestone list comes from the server seed, so both clients
    // build the same one and a card lands with no round trip.
    std::vector<CardId> const& hand() const { return m_hand; }
    // What the rival is holding, as of their last tick. Only the Eye card is
    // allowed to draw it.
    std::vector<CardId> const& rivalHand() const { return m_rivalHand; }
    std::vector<float> const& milestones() const { return m_milestones; }
    bool playCard(int slot);
    bool dealsCards() const;
    // Only the two the duel actually enforces are ever sent; listing one the
    // client ignores would be a rule that is not a rule.
    bool hasMutator(std::string const& id) const;

    void forfeit();
    void reset();

    float countdownLeft() const;
    bool countingDown() const { return m_phase == Phase::Countdown; }
    // Seconds left on the format's clock, 0 when it has none or it ran out.
    float timeLeft() const;
    // Tug of war, -1 on the rival's side and 1 on ours.
    float rope() const { return m_rope; }
    std::string statusLine() const;

private:
    VersusSession() = default;

    void setPhase(Phase phase);
    void schedulePoll(float delay);
    void poll();
    void scheduleWatch(float delay);
    void watchTick();
    void applyLobby(MatchInfo const& info);
    void wireNet();
    void pushTick(bool force);
    // Ladder and Relay: takes the segment for us if neither side holds it yet.
    void claimSegment(int segment);
    void evaluate();
    // Relay is the one format the finish line does not settle, so it gets its
    // own reading of the four segments.
    void evaluateRelay();
    // Closes a duel nobody won outright: the better run takes it.
    void finishOnPercent();
    // Positive is our side. Under a hair the two runs are called a dead heat.
    void finishOnGap(float gap);
    int attemptLimit() const;
    void finish(Outcome outcome);
    void notifyListeners();
    void buildMilestones();
    void checkMilestones();
    void drawCard();
    void receiveCard(CardId card, bool alreadyReflected);

    Phase m_phase = Phase::Idle;
    MatchInfo m_match;
    QueueTicket m_ticket;
    SideState m_own;
    SideState m_rival;
    Outcome m_outcome = Outcome::Pending;
    int m_eloDelta = 0;

    bool m_inLevel = false;
    bool m_submitted = false;
    bool m_rivalSeen = false;
    bool m_practice = false;
    float m_levelTime = 0.f;
    float m_sinceTick = 0.f;
    float m_hillHeld = 0.f;
    float m_rope = 0.f;
    float m_startsIn = 0.f;
    float m_rivalSilence = 0.f;

    std::vector<CardId> m_hand;
    std::vector<CardId> m_rivalHand;
    std::vector<float> m_milestones;
    size_t m_nextMilestone = 0;
    float m_milestoneShift = 0.f;
    float m_hourglass = 0.f;
    int m_extraAttempts = 0;

    uint64_t m_pollGeneration = 0;
    uint64_t m_watchGeneration = 0;
    bool m_watching = false;
    std::vector<std::pair<void const*, std::function<void()>>> m_listeners;
};

} // namespace paimon::versus
