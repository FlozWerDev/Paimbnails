#include "VersusNet.hpp"
#include "VersusGlobed.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

#if __has_include(<globed/core/Event.hpp>)
    #include <globed/core/Event.hpp>
    #define PAIMON_VERSUS_GLOBED 1
#endif

using namespace geode::prelude;

namespace paimon::versus::net {

namespace {

int s_rival = 0;
Handlers s_handlers;

// Token bucket. Globed asks not to spam and we promised a hard ceiling, so the
// cap lives here rather than in every caller.
constexpr float kBudgetPerSecond = 8.f;
float s_budget = kBudgetPerSecond;
std::chrono::steady_clock::time_point s_lastRefill;

bool spend() {
    auto const now = std::chrono::steady_clock::now();
    if (s_lastRefill == std::chrono::steady_clock::time_point()) s_lastRefill = now;

    float const elapsed = std::chrono::duration<float>(now - s_lastRefill).count();
    s_lastRefill = now;
    s_budget = std::min(kBudgetPerSecond, s_budget + elapsed * kBudgetPerSecond);

    if (s_budget < 1.f) return false;
    s_budget -= 1.f;
    return true;
}

void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

uint16_t readU16(std::span<uint8_t const> data, size_t at) {
    return static_cast<uint16_t>(data[at]) | static_cast<uint16_t>(data[at + 1] << 8);
}

uint16_t clampU16(float v, float scale) {
    float const scaled = v * scale;
    if (scaled <= 0.f) return 0;
    return static_cast<uint16_t>(std::min(scaled, 65535.f));
}

CardId cardOrNone(uint8_t raw) {
    return raw < static_cast<uint8_t>(CardId::Count) ? static_cast<CardId>(raw) : CardId::Count;
}

#ifdef PAIMON_VERSUS_GLOBED

struct VsTick : globed::ServerEvent<VsTick, globed::EventServer::Game> {
    static constexpr auto Id = "vs-tick"_spr;

    Tick data;

    VsTick() = default;
    explicit VsTick(Tick const& t) : data(t) {}

    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> out;
        out.reserve(9);
        putU16(out, clampU16(data.percent, 100.f));
        putU16(out, clampU16(data.levelTime, 10.f));
        putU16(out, static_cast<uint16_t>(std::min(data.attempt, 65535)));
        uint8_t flags = 0;
        if (data.alive) flags |= 1u << 0;
        if (data.practice) flags |= 1u << 1;
        if (data.shielded) flags |= 1u << 2;
        out.push_back(flags);
        out.push_back(static_cast<uint8_t>(data.hand[0]));
        out.push_back(static_cast<uint8_t>(data.hand[1]));
        return out;
    }

    static Result<VsTick> decode(std::span<uint8_t const> data) {
        if (data.size() < 7) return Err("short vs-tick");
        VsTick out;
        out.data.percent = readU16(data, 0) / 100.f;
        out.data.levelTime = readU16(data, 2) / 10.f;
        out.data.attempt = readU16(data, 4);
        uint8_t const flags = data[6];
        out.data.alive = flags & (1u << 0);
        out.data.practice = flags & (1u << 1);
        out.data.shielded = flags & (1u << 2);
        // The hand was added after the tick shipped, so a client from before it
        // simply never shows one.
        if (data.size() >= 9) {
            out.data.hand[0] = cardOrNone(data[7]);
            out.data.hand[1] = cardOrNone(data[8]);
        }
        return Ok(std::move(out));
    }
};

struct VsCard : globed::ServerEvent<VsCard, globed::EventServer::Game> {
    static constexpr auto Id = "vs-card"_spr;

    CardMsg data;

    VsCard() = default;
    explicit VsCard(CardMsg const& c) : data(c) {}

    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> out;
        out.reserve(5);
        out.push_back(static_cast<uint8_t>(data.card));
        out.push_back(data.milestone);
        putU16(out, clampU16(data.levelTime, 10.f));
        out.push_back(data.reflected ? 1 : 0);
        return out;
    }

    static Result<VsCard> decode(std::span<uint8_t const> data) {
        if (data.size() < 5) return Err("short vs-card");
        if (data[0] >= static_cast<uint8_t>(CardId::Count)) return Err("unknown card id");
        VsCard out;
        out.data.card = static_cast<CardId>(data[0]);
        out.data.milestone = data[1];
        out.data.levelTime = readU16(data, 2) / 10.f;
        out.data.reflected = data[4] != 0;
        return Ok(std::move(out));
    }
};

struct VsState : globed::ServerEvent<VsState, globed::EventServer::Game> {
    static constexpr auto Id = "vs-state"_spr;

    StateMsg data;

    VsState() = default;
    explicit VsState(StateMsg const& s) : data(s) {}

    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> out;
        out.reserve(6);
        out.push_back(static_cast<uint8_t>(data.kind));
        out.push_back(data.value);
        putU16(out, data.detail);
        putU16(out, clampU16(data.levelTime, 10.f));
        return out;
    }

    static Result<VsState> decode(std::span<uint8_t const> data) {
        if (data.size() < 6) return Err("short vs-state");
        if (data[0] > static_cast<uint8_t>(StateKind::Spent)) return Err("unknown state kind");
        VsState out;
        out.data.kind = static_cast<StateKind>(data[0]);
        out.data.value = data[1];
        out.data.detail = readU16(data, 2);
        out.data.levelTime = readU16(data, 4) / 10.f;
        return Ok(std::move(out));
    }
};

struct VsTaunt : globed::ServerEvent<VsTaunt, globed::EventServer::Game> {
    static constexpr auto Id = "vs-taunt"_spr;

    uint8_t emote = 0;

    VsTaunt() = default;
    explicit VsTaunt(uint8_t e) : emote(e) {}

    std::vector<uint8_t> encode() const { return {emote, 0}; }

    static Result<VsTaunt> decode(std::span<uint8_t const> data) {
        if (data.size() < 2) return Err("short vs-taunt");
        return Ok(VsTaunt{data[0]});
    }
};

std::vector<geode::ListenerHandle> s_listeners;

globed::EventOptions optionsFor(bool reliable, bool urgent) {
    globed::EventOptions opts;
    opts.server = globed::EventServer::Game;
    opts.reliable = reliable;
    opts.urgent = urgent;
    if (s_rival != 0) opts.targetPlayers.push_back(s_rival);
    return opts;
}

bool fromRival(globed::EventOptions const& opts) {
    return s_rival != 0 && opts.sender == s_rival;
}

#endif

} // namespace

void registerEvents() {
#ifdef PAIMON_VERSUS_GLOBED
    // Touching each type is enough: ServerEvent registers itself on first use
    // and waits for Globed on its own.
    VsTick::_register();
    VsCard::_register();
    VsState::_register();
    VsTaunt::_register();
    log::info("[Versus][Net] Server events registered");
#else
    log::info("[Versus][Net] Built without Globed headers, running server-relayed");
#endif
}

void setRival(int accountId) {
    s_rival = accountId;
    s_budget = kBudgetPerSecond;
    s_lastRefill = {};
}

int rival() {
    return s_rival;
}

void listen(Handlers handlers) {
    // stopListening clears the handlers too, so it goes first or it would wipe
    // the ones we were just given.
    stopListening();
    s_handlers = std::move(handlers);
#ifdef PAIMON_VERSUS_GLOBED
    s_listeners.push_back(VsTick::listen([](VsTick const& ev, globed::EventOptions const& opts) {
        if (fromRival(opts) && s_handlers.onTick) s_handlers.onTick(opts.sender, ev.data);
    }));
    s_listeners.push_back(VsCard::listen([](VsCard const& ev, globed::EventOptions const& opts) {
        if (fromRival(opts) && s_handlers.onCard) s_handlers.onCard(opts.sender, ev.data);
    }));
    s_listeners.push_back(VsState::listen([](VsState const& ev, globed::EventOptions const& opts) {
        if (fromRival(opts) && s_handlers.onState) s_handlers.onState(opts.sender, ev.data);
    }));
    s_listeners.push_back(VsTaunt::listen([](VsTaunt const& ev, globed::EventOptions const& opts) {
        if (fromRival(opts) && s_handlers.onTaunt) s_handlers.onTaunt(opts.sender, ev.emote);
    }));
#endif
}

void stopListening() {
#ifdef PAIMON_VERSUS_GLOBED
    s_listeners.clear();
#endif
    s_handlers = {};
}

void sendTick(Tick const& tick) {
#ifdef PAIMON_VERSUS_GLOBED
    if (!gl::inSession() || s_rival == 0) return;
    if (!spend()) return;
    VsTick(tick).send(optionsFor(false, true));
#else
    (void) tick;
#endif
}

void sendCard(CardMsg const& card) {
#ifdef PAIMON_VERSUS_GLOBED
    if (!gl::inSession() || s_rival == 0) return;
    VsCard(card).send(optionsFor(true, true));
#else
    (void) card;
#endif
}

void sendState(StateMsg const& state) {
#ifdef PAIMON_VERSUS_GLOBED
    if (!gl::present() || s_rival == 0) return;
    VsState(state).send(optionsFor(true, true));
#else
    (void) state;
#endif
}

} // namespace paimon::versus::net
