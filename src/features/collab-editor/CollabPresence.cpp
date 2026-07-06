#include "CollabPresence.hpp"

#include "../../utils/WebHelper.hpp"
#include "CollabManager.hpp"
#include "CollabPopups.hpp"
#include "CollabTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <chrono>
#include <thread>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

bool invitesEnabled() {
    return Mod::get()->getSettingValue<bool>("collab-invites");
}

std::string baseUrl() {
    return std::string(kServerBaseUrl);
}

} // namespace

CollabPresence& CollabPresence::get() {
    static CollabPresence instance;
    return instance;
}

void CollabPresence::start() {
    if (m_started || !invitesEnabled()) return;
    auto* acc = GJAccountManager::sharedState();
    if (!acc || acc->m_accountID <= 0) return; // not signed in: nothing to reach

    m_accountId = acc->m_accountID;
    m_started = true;
    ++m_gen;
    registerSelf();
}

void CollabPresence::stop() {
    if (!m_started) return;
    int account = m_accountId;
    m_started = false;
    ++m_gen; // invalidate in-flight polls

    if (account > 0) {
        auto body = matjson::makeObject({{"accountID", static_cast<int64_t>(account)}});
        auto req = web::WebRequest();
        req.header("Content-Type", "application/json");
        req.bodyString(body.dump(matjson::NO_INDENTATION));
        WebHelper::dispatch(std::move(req), "POST", baseUrl() + "/api/presence/leave",
            [](web::WebResponse) {});
    }
}

void CollabPresence::registerSelf() {
    if (!m_started || m_accountId <= 0) return;
    uint64_t gen = m_gen;

    std::string name = defaultDisplayName();
    auto body = matjson::makeObject({
        {"accountID", static_cast<int64_t>(m_accountId)},
        {"username", name},
    });

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(15));
    req.header("Content-Type", "application/json");
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    WebHelper::dispatch(std::move(req), "POST", baseUrl() + "/api/presence/register",
        [this, gen](web::WebResponse res) {
            if (!m_started || gen != m_gen) return;
            if (res.ok()) {
                poll();
            } else {
                scheduleRetry(gen, 5000);
            }
        });
}

void CollabPresence::poll() {
    if (!m_started || m_accountId <= 0) return;
    uint64_t gen = m_gen;

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(35));
    std::string url = baseUrl() + fmt::format("/api/presence/poll?account={}", m_accountId);

    WebHelper::dispatch(std::move(req), "GET", url,
        [this, gen](web::WebResponse res) {
            if (!m_started || gen != m_gen) return;

            if (!res.ok()) {
                scheduleRetry(gen, 3000);
                return;
            }

            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (!parsed) {
                poll();
                return;
            }

            bool needsReregister = false;
            auto value = parsed.unwrap();
            if (auto arr = value["messages"].asArray()) {
                for (auto const& msg : arr.unwrap()) {
                    if (!msg.isObject()) continue;
                    std::string t = msg["t"].asString().unwrapOr("");
                    if (t == "invite") {
                        handleInvite(
                            msg["room"].asString().unwrapOr(""),
                            msg["fromName"].asString().unwrapOr("")
                        );
                    } else if (t == "error" && msg["code"].asString().unwrapOr("") == "not_registered") {
                        needsReregister = true;
                    }
                }
            }

            if (needsReregister) {
                registerSelf();
            } else {
                poll();
            }
        });
}

void CollabPresence::scheduleRetry(uint64_t gen, int ms) {
    std::thread([this, gen, ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        Loader::get()->queueInMainThread([this, gen]() {
            if (!m_started || gen != m_gen) return;
            registerSelf();
        });
    }).detach();
}

void CollabPresence::handleInvite(std::string const& room, std::string const& fromName) {
    if (room.empty()) return;

    // Don't interrupt an active session for our own room; ignore self-invites
    // to the room we're already in.
    auto& mgr = CollabManager::get();
    if (mgr.connected() && mgr.roomCode() == room) return;

    if (auto* n = AchievementNotifier::sharedState()) {
        std::string who = fromName.empty() ? "Alguien" : fromName;
        std::string desc = fmt::format("{} te invito a colaborar", who);
        std::string icon = "paim_Paimon.png"_spr;
        n->notifyAchievement("Invitacion de Collab", desc.c_str(), icon.c_str(), true);
    }

    queueInMainThread([room, fromName]() {
        if (auto* popup = CollabInvitePromptPopup::create(room, fromName)) popup->show();
    });
}

} // namespace paimon::collab

$on_game(Loaded) {
    paimon::collab::CollabPresence::get().start();
}
