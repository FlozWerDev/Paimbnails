#pragma once

// four binary events scoped to the rival: ~60 bytes/s each way at peak, under
// what one moving player costs, inside Globed's fair use.

#include "../data/VersusTypes.hpp"

#include <cstdint>
#include <functional>

namespace paimon::versus::net {

struct Tick {
    float percent = 0.f;
    float levelTime = 0.f;
    int attempt = 1;
    bool alive = true;
    bool practice = false;
    bool shielded = false;
    // the two slots, Count for an empty one; rides every tick, but only the
    // Eye card ever shows it.
    CardId hand[2] = {CardId::Count, CardId::Count};
};

enum class StateKind : uint8_t {
    Ready,
    Countdown,
    Death,
    Segment,
    Finish,
    Forfeit,
    Rematch,
    Spent,      // out of attempts; the format decides once both sides are
    Revive,     // a Heart moved the limit; the run is back on
};

struct StateMsg {
    StateKind kind = StateKind::Ready;
    uint8_t value = 0;
    uint16_t detail = 0;
    float levelTime = 0.f;
};

struct CardMsg {
    CardId card = CardId::Fog;
    uint8_t milestone = 0;
    bool reflected = false;
    float levelTime = 0.f;
};

using TickHandler  = std::function<void(int from, Tick const&)>;
using CardHandler  = std::function<void(int from, CardMsg const&)>;
using StateHandler = std::function<void(int from, StateMsg const&)>;
using TauntHandler = std::function<void(int from, uint8_t emote)>;

struct Handlers {
    TickHandler onTick;
    CardHandler onCard;
    StateHandler onState;
    TauntHandler onTaunt;
};

// called once on load; registration itself waits for Globed internally.
void registerEvents();

// only this account's events are forwarded, and everything sent goes only
// to them. Zero tears the duel down.
void setRival(int accountId);
int rival();

void listen(Handlers handlers);
void stopListening();

void sendTick(Tick const& tick);
void sendCard(CardMsg const& card);
void sendState(StateMsg const& state);

} // namespace paimon::versus::net
