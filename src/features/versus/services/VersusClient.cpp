#include "VersusClient.hpp"
#include "../data/VersusModes.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <ctime>

using namespace geode::prelude;

namespace paimon::versus {

namespace {

constexpr char const* kDefaultBase = "https://vs-paimbnails.vercel.app";
constexpr char const* kLegacyBase = "https://paimon-versus.vercel.app";
constexpr auto kTimeout = std::chrono::seconds(12);

std::string trimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

PlayerRef parsePlayer(matjson::Value const& v) {
    PlayerRef ref;
    ref.accountId = static_cast<int>(intField(v, "accountId"));
    ref.userId = static_cast<int>(intField(v, "userId"));
    ref.name = stringField(v, "name");
    ref.elo = static_cast<int>(intField(v, "elo", kStartElo));
    ref.wins = static_cast<int>(intField(v, "wins"));
    ref.losses = static_cast<int>(intField(v, "losses"));
    ref.placementsLeft = static_cast<int>(intField(v, "placements"));
    return ref;
}

ModeProfile parseProfile(matjson::Value const& v) {
    ModeProfile p;
    p.elo = static_cast<int>(intField(v, "elo", kStartElo));
    p.best = static_cast<int>(intField(v, "best", p.elo));
    p.wins = static_cast<int>(intField(v, "wins"));
    p.losses = static_cast<int>(intField(v, "losses"));
    p.streak = static_cast<int>(intField(v, "streak"));
    p.placementsLeft = static_cast<int>(intField(v, "placements", kPlacementMatches));
    p.xpTotal = intField(v, "xpTotal");
    p.paimon = boolField(v, "paimon");
    p.lastPlayed = intField(v, "lastPlayed");
    return p;
}

LevelOffer parseOffer(matjson::Value const& v) {
    LevelOffer offer;
    offer.levelId = static_cast<int>(intField(v, "levelId"));
    offer.name = stringField(v, "name");
    offer.author = stringField(v, "author");
    offer.difficulty = static_cast<int>(intField(v, "difficulty"));
    offer.length = static_cast<int>(intField(v, "length"));
    offer.banned = boolField(v, "banned");
    return offer;
}

matjson::Value sideToJson(SideState const& side) {
    return matjson::makeObject({
        {"percent", side.percent},
        {"best", side.bestPercent},
        {"time", side.finishTime},
        {"attempt", side.attempt},
        {"deaths", side.deaths},
        {"finished", side.finished},
        {"forfeited", side.forfeited},
        {"segments", static_cast<int>(side.segments)},
    });
}

} // namespace

int64_t intField(matjson::Value const& v, char const* key, int64_t fallback) {
    if (!v.contains(key)) return fallback;
    return v[key].asInt().unwrapOr(fallback);
}

std::string stringField(matjson::Value const& v, char const* key) {
    if (!v.contains(key)) return {};
    return v[key].asString().unwrapOr("");
}

bool boolField(matjson::Value const& v, char const* key, bool fallback) {
    if (!v.contains(key)) return fallback;
    return v[key].asBool().unwrapOr(fallback);
}

VersusClient& VersusClient::get() {
    static VersusClient instance;
    return instance;
}

std::string VersusClient::baseUrl() const {
    auto configured = trimSlash(
        Mod::get()->getSavedValue<std::string>("versus-server-url", "")
    );
    // Migrate the old hidden override as well as fresh installs. Otherwise a
    // player who connected before the rename would keep using the dead host.
    if (configured.empty() || configured == kLegacyBase) return kDefaultBase;
    return configured;
}

bool VersusClient::authenticated() const {
    return m_authenticated && !m_token.empty();
}

void VersusClient::send(std::string const& method, std::string const& path,
                        matjson::Value const& body,
                        geode::CopyableFunction<void(bool, matjson::Value const&,
                                                     std::string const&)> cb,
                        bool allowRetry) {
    auto req = web::WebRequest();
    req.timeout(kTimeout);
    req.header("Content-Type", "application/json");

    auto const modCode = Mod::get()->getSavedValue<std::string>("mod-code", "");
    if (!modCode.empty()) req.header("X-Mod-Code", modCode);
    if (!m_token.empty()) req.header("Authorization", "Bearer " + m_token);

    if (method != "GET") req.bodyString(body.dump(matjson::NO_INDENTATION));

    WebHelper::dispatch(std::move(req), method, baseUrl() + path,
        [method, path, body, allowRetry, cb = std::move(cb)](web::WebResponse res) mutable {
            if (paimon::isRuntimeShuttingDown()) return;

            auto const text = res.string().unwrapOr("");
            auto parsed = matjson::parse(text);
            matjson::Value json = parsed.isOk() ? parsed.unwrap() : matjson::Value();

            if (!res.ok()) {
                auto message = json.contains("error")
                    ? json["error"].asString().unwrapOr("")
                    : fmt::format("HTTP {}", res.code());

                auto& self = VersusClient::get();
                if (res.code() == 401 && allowRetry && !self.m_token.empty()) {
                    log::warn("[Versus][Client] Session refused on {}, signing in again", path);
                    self.m_token.clear();
                    self.m_authenticated = false;
                    self.authenticate([method, path, body, cb = std::move(cb)](
                                          bool ok, std::string const& authMessage) mutable {
                        if (!ok) {
                            cb(false, matjson::Value(), authMessage);
                            return;
                        }
                        VersusClient::get().send(method, path, body, std::move(cb), false);
                    });
                    return;
                }

                cb(false, json, message);
                return;
            }
            cb(true, json, stringField(json, "message"));
        });
}

MatchInfo VersusClient::parseMatch(matjson::Value const& v) {
    MatchInfo match;
    match.id = stringField(v, "id");
    match.serverPhase = stringField(v, "phase");
    match.mode = modeFromId(stringField(v, "mode"));
    match.format = formatFromId(stringField(v, "format"));
    match.levelId = static_cast<int>(intField(v, "levelId"));
    match.seed = static_cast<uint64_t>(intField(v, "seed"));
    match.countdownMs = static_cast<int>(intField(v, "countdownMs"));
    match.catchUp = boolField(v, "catchUp", true);
    match.ranked = boolField(v, "ranked", true);
    if (v.contains("mutators") && v["mutators"].isArray()) {
        for (auto const& entry : v["mutators"]) {
            match.mutators.push_back(entry.asString().unwrapOr(""));
        }
    }
    if (v.contains("rival")) match.rival = parsePlayer(v["rival"]);
    if (v.contains("offers") && v["offers"].isArray()) {
        for (auto const& offer : v["offers"]) match.offers.push_back(parseOffer(offer));
    }
    return match;
}

void VersusClient::authenticate(AuthCallback cb) {
    auto* gm = GameManager::sharedState();
    auto* am = GJAccountManager::sharedState();
    auto const body = matjson::makeObject({
        {"accountId", am ? am->m_accountID : 0},
        {"name", gm ? std::string(gm->m_playerName) : std::string()},
    });

    send("POST", "/api/auth", body, [cb = std::move(cb)](bool ok, matjson::Value const& json,
                                                         std::string const& message) mutable {
        if (!ok) {
            log::warn("[Versus][Client] Auth failed: {}", message);
            cb(false, message);
            return;
        }

        auto& self = VersusClient::get();
        self.m_token = stringField(json, "token");
        self.m_authenticated = !self.m_token.empty();
        VersusStore::get().setSessionToken(self.m_token);

        self.m_season.number = static_cast<int>(intField(json, "season"));
        self.m_season.daysLeft = static_cast<int>(intField(json, "seasonDaysLeft"));
        self.m_season.mutators.clear();
        if (json.contains("mutators") && json["mutators"].isArray()) {
            for (auto const& entry : json["mutators"]) {
                self.m_season.mutators.push_back(entry.asString().unwrapOr(""));
            }
        }

        if (json.contains("classic")) {
            VersusStore::get().setProfile(Mode::Classic, parseProfile(json["classic"]));
        }
        if (json.contains("platformer")) {
            VersusStore::get().setProfile(Mode::Platformer, parseProfile(json["platformer"]));
        }
        cb(true, message);
    });
}

void VersusClient::joinQueue(Mode mode, Format format, QueueCallback cb) {
    auto const body = matjson::makeObject({
        {"mode", modeId(mode)},
        {"format", formatId(format)},
        {"friendsOnly", VersusStore::get().friendsOnly()},
        {"globed", true},
    });

    send("POST", "/api/queue", body, [cb = std::move(cb)](bool ok, matjson::Value const& json,
                                                          std::string const&) mutable {
        QueueTicket ticket;
        if (ok) {
            ticket.id = stringField(json, "ticket");
            ticket.waiting = static_cast<int>(intField(json, "waiting"));
            ticket.estimateSeconds = static_cast<int>(intField(json, "estimate"));
        }
        cb(ok, ticket);
    });
}

void VersusClient::leaveQueue(OkCallback cb) {
    send("DELETE", "/api/queue", matjson::Value(),
        [cb = std::move(cb)](bool ok, matjson::Value const&, std::string const& message) mutable {
            cb(ok, message);
        });
}

void VersusClient::pollLobby(MatchCallback cb) {
    send("GET", "/api/lobby", matjson::Value(),
        [cb = std::move(cb)](bool ok, matjson::Value const& json, std::string const&) mutable {
            cb(ok, ok ? parseMatch(json) : MatchInfo{});
        });
}

void VersusClient::acceptMatch(std::string const& matchId, bool accept, OkCallback cb) {
    auto const body = matjson::makeObject({{"accept", accept}});
    send("POST", "/api/match/" + matchId + "/accept", body,
        [cb = std::move(cb)](bool ok, matjson::Value const&, std::string const& message) mutable {
            cb(ok, message);
        });
}

void VersusClient::banLevel(std::string const& matchId, int levelId, MatchCallback cb) {
    auto const body = matjson::makeObject({{"levelId", levelId}});
    send("POST", "/api/match/" + matchId + "/ban", body,
        [cb = std::move(cb)](bool ok, matjson::Value const& json, std::string const&) mutable {
            cb(ok, ok ? parseMatch(json) : MatchInfo{});
        });
}

void VersusClient::reportReady(std::string const& matchId, MatchCallback cb) {
    send("POST", "/api/match/" + matchId + "/ready", matjson::Value(),
        [cb = std::move(cb)](bool ok, matjson::Value const& json, std::string const&) mutable {
            cb(ok, ok ? parseMatch(json) : MatchInfo{});
        });
}

void VersusClient::submitResult(std::string const& matchId, SideState const& own,
                                SideState const& rival, Outcome outcome, OkCallback cb) {
    auto const body = matjson::makeObject({
        {"own", sideToJson(own)},
        {"rival", sideToJson(rival)},
        {"outcome", static_cast<int>(outcome)},
    });
    send("POST", "/api/match/" + matchId + "/result", body,
        [cb = std::move(cb)](bool ok, matjson::Value const& json, std::string const& message) mutable {
            if (ok) {
                auto& store = VersusStore::get();
                if (json.contains("classic")) {
                    store.setProfile(Mode::Classic, parseProfile(json["classic"]));
                }
                if (json.contains("platformer")) {
                    store.setProfile(Mode::Platformer, parseProfile(json["platformer"]));
                }
            }
            cb(ok, message);
        });
}

void VersusClient::forfeit(std::string const& matchId, OkCallback cb) {
    send("POST", "/api/match/" + matchId + "/forfeit", matjson::Value(),
        [cb = std::move(cb)](bool ok, matjson::Value const&, std::string const& message) mutable {
            cb(ok, message);
        });
}

void VersusClient::challenge(std::string const& target, Mode mode, Format format,
                             ChallengeCallback cb) {
    auto const body = matjson::makeObject({
        {"target", target},
        {"mode", modeId(mode)},
        {"format", formatId(format)},
    });
    send("POST", "/api/challenge", body,
        [cb = std::move(cb)](bool ok, matjson::Value const& json,
                             std::string const& message) mutable {
            ChallengeResult result;
            if (ok) {
                result.code = stringField(json, "code");
                result.matchId = stringField(json, "id");
            }
            cb(ok, result, message);
        });
}

void VersusClient::fetchProfile(int accountId, ProfileCallback cb) {
    if (accountId <= 0) {
        cb(false, ModeProfile{}, ModeProfile{});
        return;
    }

    auto const now = static_cast<int64_t>(std::time(nullptr));
    if (auto it = m_profileCache.find(accountId); it != m_profileCache.end()) {
        if (now - it->second.fetchedAt < kProfileTtlSeconds) {
            cb(true, it->second.classic, it->second.platformer);
            return;
        }
    }

    auto waiting = m_profileWaiters.find(accountId);
    if (waiting != m_profileWaiters.end()) {
        waiting->second.push_back(std::move(cb));
        return;
    }
    m_profileWaiters[accountId].push_back(std::move(cb));

    send("GET", fmt::format("/api/profile/{}", accountId), matjson::Value(),
        [accountId](bool ok, matjson::Value const& json, std::string const&) {
            ModeProfile classic;
            ModeProfile platformer;
            if (ok) {
                if (json.contains("classic")) classic = parseProfile(json["classic"]);
                if (json.contains("platformer")) platformer = parseProfile(json["platformer"]);
            }

            auto& self = VersusClient::get();
            if (ok) {
                self.m_profileCache[accountId] = {
                    classic, platformer, static_cast<int64_t>(std::time(nullptr))};
            }

            auto node = self.m_profileWaiters.extract(accountId);
            if (!node) return;
            for (auto& waiter : node.mapped()) {
                if (waiter) waiter(ok, classic, platformer);
            }
        });
}

void VersusClient::fetchLeaderboard(Mode mode, std::string const& scope, BoardCallback cb) {
    send("GET", fmt::format("/api/leaderboard?mode={}&scope={}", modeId(mode), scope),
        matjson::Value(),
        [cb = std::move(cb)](bool ok, matjson::Value const& json, std::string const&) mutable {
            std::vector<LeaderboardRow> rows;
            if (ok && json.contains("rows") && json["rows"].isArray()) {
                int position = 1;
                for (auto const& entry : json["rows"]) {
                    LeaderboardRow row;
                    row.rank = static_cast<int>(intField(entry, "rank", position));
                    row.accountId = static_cast<int>(intField(entry, "accountId"));
                    row.name = stringField(entry, "name");
                    row.elo = static_cast<int>(intField(entry, "elo"));
                    row.wins = static_cast<int>(intField(entry, "wins"));
                    row.losses = static_cast<int>(intField(entry, "losses"));
                    rows.push_back(std::move(row));
                    position++;
                }
            }
            cb(ok, rows);
        });
}

void VersusClient::reportPlayer(std::string const& matchId, std::string const& note, OkCallback cb) {
    auto const body = matjson::makeObject({
        {"matchId", matchId},
        {"note", note},
    });
    send("POST", "/api/report", body,
        [cb = std::move(cb)](bool ok, matjson::Value const&, std::string const& message) mutable {
            cb(ok, message);
        });
}

} // namespace paimon::versus
