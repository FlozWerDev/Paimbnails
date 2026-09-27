#include "BanGate.hpp"
#include "RuntimeLifecycle.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/ThreadTracker.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../features/onboarding/WelcomeFlow.hpp"
#include "../features/moderation/ui/BannedPopup.hpp"
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <filesystem>
#include <fstream>
#include <optional>
#include <chrono>

using namespace geode::prelude;

namespace {

constexpr int64_t kCacheTtlSeconds = 7 * 24 * 60 * 60; // revalidate every 7 days

int64_t nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::filesystem::path banCacheFile() {
    return Mod::get()->getSaveDir() / ".paimon";
}

std::optional<matjson::Value> readBanCache() {
    std::error_code ec;
    auto path = banCacheFile();
    if (!std::filesystem::exists(path, ec)) return std::nullopt;

    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto res = matjson::parse(content);
    if (!res.isOk()) return std::nullopt;
    return res.unwrap();
}

void writeBanCache(bool banned, std::string const& reason) {
    auto json = matjson::makeObject({
        {"banned", banned},
        {"reason", reason},
        {"checkedAt", nowEpochSeconds()}
    });
    std::error_code ec;
    std::filesystem::create_directories(banCacheFile().parent_path(), ec);
    std::ofstream f(banCacheFile(), std::ios::binary | std::ios::trunc);
    if (f) f << json.dump();
}

void terminateModProcesses() {
    paimon::markRuntimeShuttingDown();
    paimon::ThreadTracker::get().shutdown();
    HttpClient::get().cleanTasks(false);
}

// MainThreadDelay drops callbacks during shutdown, so the ban popup schedules its own task.
void showBannedPopupForced(std::string const& reason, float delay) {
    auto* director = CCDirector::get();
    if (!director) return;
    auto* sched = director->getScheduler();
    if (!sched) return;

    struct Task final : CCObject {
        std::string reason;
        void fire(float) {
            if (auto* dir = CCDirector::get())
                if (auto* s = dir->getScheduler())
                    s->unscheduleSelector(schedule_selector(Task::fire), this);
            paimon::ban::showBannedPopup(reason);
            this->release();
        }
    };

    auto* t = new Task();
    t->reason = reason;
    sched->scheduleSelector(schedule_selector(Task::fire), t, 0.f, 0, std::max(0.f, delay), false);
}

void enforceBan(std::string const& reason) {
    terminateModProcesses();
    showBannedPopupForced(reason, 1.5f);
}

} // namespace

namespace paimon::ban {

bool runStartupBanGate() {
    bool haveCache = false;
    bool cachedBanned = false;
    std::string cachedReason;
    bool expired = true;

    if (auto cache = readBanCache()) {
        haveCache = true;
        cachedBanned = cache->contains("banned") && (*cache)["banned"].asBool().unwrapOr(false);
        cachedReason = cache->contains("reason") ? (*cache)["reason"].asString().unwrapOr("") : "";
        int64_t checkedAt = cache->contains("checkedAt") ? (*cache)["checkedAt"].asInt().unwrapOr(0) : 0;
        expired = checkedAt <= 0 || (nowEpochSeconds() - checkedAt) >= kCacheTtlSeconds;
    }

    // Fresh cache (< 7 days): trust it, no server request.
    if (haveCache && !expired) {
        if (cachedBanned) {
            log::warn("[BanGate] User is banned (cached). Aborting mod init.");
            enforceBan(cachedReason);
            return true;
        }
        return false;
    }

    if (!paimon::onboarding::isAccepted()) {
        if (haveCache && cachedBanned) {
            enforceBan(cachedReason);
            return true;
        }
        return false;
    }

    // no cache or older than 7 days: revalidate async; a stale ban plus no server fails closed.
    bool staleBanned = haveCache && cachedBanned;
    std::string staleReason = cachedReason;

    paimon::scheduleMainThreadDelay(3.0f, [staleBanned, staleReason]() {
        if (paimon::isRuntimeShuttingDown()) return;

        int accountID = 0;
        std::string username;
        if (auto* am = GJAccountManager::get()) {
            accountID = am->m_accountID;
            username = am->m_username;
        }

        if (username.empty() && accountID <= 0) {
            if (staleBanned) enforceBan(staleReason);
            return;
        }

        HttpClient::get().checkBanned([staleBanned, staleReason](bool ok, bool banned, std::string const& reason) {
            if (!ok) {
                if (!staleBanned) return;
                // unreachable server: a stale ban stays.
                log::warn("[BanGate] Revalidation failed; keeping stale ban.");
                enforceBan(staleReason);
                return;
            }
            writeBanCache(banned, reason);
            if (banned) {
                log::warn("[BanGate] User is banned (server). Tearing down mod.");
                enforceBan(reason);
            }
        });
    });

    return false;
}

} // namespace paimon::ban
