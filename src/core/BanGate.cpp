#include "../utils/FilesystemPath.hpp"
#include "BanGate.hpp"
#include "BanPolicy.hpp"
#include "RuntimeLifecycle.hpp"
#include "ModAuthFlow.hpp"
#include "../utils/AtomicFileWrite.hpp"
#include "../utils/HmacSha256.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/SafeText.hpp"
#include "../utils/WebSocketClient.hpp"
#include "../features/collab-editor/CollabManager.hpp"
#include "../features/thumb-alerts/services/ThumbFeedSocket.hpp"
#include "../features/versus/services/VersusNet.hpp"
#include "../features/onboarding/WelcomeFlow.hpp"
#include "../features/moderation/ui/BannedPopup.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace geode::prelude;

namespace {

std::atomic<bool> g_banned{false};
std::mutex g_sessionMutex;
paimon::ban::Session g_session;

// monitor bookkeeping is main-thread-only, like webhelper's callbacks.
bool g_monitorStarted = false;
bool g_checkInFlight = false;
bool g_reportedFailure = false;
bool g_onlineSessionsActive = false;
bool g_loggedIdentityMismatch = false;
bool g_popupScheduled = false;
std::string g_banReason;
std::uint64_t g_checkGeneration = 0;
paimon::ban::AccountIdentity g_monitoredAccount;
paimon::ban::Session::Clock::time_point g_nextCheck{};
paimon::ban::Session::Clock::time_point g_checkStarted{};
paimon::ban::Session::Clock::time_point g_nextPopupAt{};

int64_t nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::filesystem::path banCacheFile() {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / ".paimon";
}

// sealed binary cache: magic + version + account + timestamp + verdict +
// reason + hmac-sha256. authenticated, not secret: editing any byte or
// copying a record from another account invalidates it and forces a server
// revalidation.
constexpr std::size_t kMaxBanCacheBytes = 4096;
constexpr std::size_t kMacBytes = 32;
constexpr std::size_t kMaxReasonBytes = 512;
constexpr std::uint32_t kBanCacheVersion = 1;
constexpr char kSavedCopyKey[] = "ban-cache-sealed-v1";
constexpr std::uint8_t kBanMagic[8] = {'P', 'A', 'I', 'M', 'O', 'N', 'B', 'G'};

enum class CacheState {
    Missing,
    Valid,
    Stale,
    Tampered,
};

struct BanRecord {
    std::int32_t accountID = 0;
    std::int64_t checkedAt = 0;
    bool banned = false;
    std::string reason;
};

struct CacheRead {
    CacheState state = CacheState::Missing;
    BanRecord record;
};

std::int32_t currentAccountID() {
    if (auto* manager = GJAccountManager::get()) {
        return static_cast<std::int32_t>(manager->m_accountID);
    }
    return 0;
}

paimon::ban::AccountIdentity currentIdentity() {
    auto* account = GJAccountManager::get();
    auto* game = GameManager::get();

    auto const reportMismatch = [] {
        if (g_loggedIdentityMismatch) return;
        g_loggedIdentityMismatch = true;
        log::warn("[BanGate] GD account identity is incomplete or inconsistent; online access stays locked.");
    };

    if (!account || !game) {
        reportMismatch();
        return {};
    }

    std::string accountName = account->m_username;
    std::string playerName = game->m_playerName;
    if (account->m_accountID <= 0 || accountName.empty() || playerName.empty() ||
        !paimon::text::isSafeAccountName(accountName) ||
        !paimon::text::isSafeAccountName(playerName) ||
        !paimon::ban::sameAccountName(accountName, playerName)) {
        reportMismatch();
        return {};
    }

    // gd may report zero before the profile finishes loading. the account id
    // and matching names remain authoritative; the user id still participates
    // in equality when available.
    auto const userID = static_cast<std::int32_t>(game->m_playerUserID);

    g_loggedIdentityMismatch = false;
    std::transform(accountName.begin(), accountName.end(), accountName.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
    });
    return {static_cast<std::int32_t>(account->m_accountID), std::move(accountName), userID};
}

std::array<std::uint8_t, 32> const& sealKey() {
    static auto const key = [] {
        // tamper evidence, not secrecy: the mod is open source and a
        // determined user can rebuild it. this stops hand edits, truncated
        // files and records copied from another account.
        constexpr std::uint8_t secret[32] = {
            0x9f, 0x2c, 0x71, 0x4a, 0xd3, 0x08, 0xb6, 0xe5,
            0x53, 0xc1, 0x8e, 0x27, 0xfa, 0x64, 0x1d, 0x9b,
            0x42, 0xd7, 0x0c, 0x85, 0x3e, 0xa9, 0x60, 0xf4,
            0x17, 0xbe, 0x59, 0x22, 0xc8, 0x3b, 0x94, 0x6d,
        };
        constexpr char context[] = "paimbnails.bangate.v1";
        std::array<std::uint8_t, 32> out{};
        paimon::crypto::hmacSha256(
            std::span<std::uint8_t const>(secret, sizeof(secret)),
            std::span<std::uint8_t const>(
                reinterpret_cast<std::uint8_t const*>(context), sizeof(context) - 1),
            out.data());
        return out;
    }();
    return key;
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void appendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
}

bool takeU8(std::span<std::uint8_t const> in, std::size_t limit,
            std::size_t& offset, std::uint8_t& value) {
    if (offset >= limit) return false;
    value = in[offset++];
    return true;
}

bool takeU32(std::span<std::uint8_t const> in, std::size_t limit,
             std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > limit) return false;
    value = (static_cast<std::uint32_t>(in[offset]) << 24)
          | (static_cast<std::uint32_t>(in[offset + 1]) << 16)
          | (static_cast<std::uint32_t>(in[offset + 2]) << 8)
          |  static_cast<std::uint32_t>(in[offset + 3]);
    offset += 4;
    return true;
}

bool takeU64(std::span<std::uint8_t const> in, std::size_t limit,
             std::size_t& offset, std::uint64_t& value) {
    if (offset + 8 > limit) return false;
    value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | in[offset + static_cast<std::size_t>(i)];
    }
    offset += 8;
    return true;
}

std::vector<std::uint8_t> sealRecord(BanRecord const& record) {
    // never split a multi-byte sequence or persist terminal control bytes.
    std::string const reason =
        paimon::text::sanitizeDisplayText(record.reason, kMaxReasonBytes);
    std::size_t const reasonLen = reason.size();

    std::vector<std::uint8_t> blob;
    blob.reserve(sizeof(kBanMagic) + 4 + 4 + 8 + 1 + 4 + reasonLen + kMacBytes);
    blob.insert(blob.end(), kBanMagic, kBanMagic + sizeof(kBanMagic));
    appendU32(blob, kBanCacheVersion);
    appendU32(blob, static_cast<std::uint32_t>(record.accountID));
    appendU64(blob, static_cast<std::uint64_t>(record.checkedAt));
    blob.push_back(record.banned ? 1 : 0);
    appendU32(blob, static_cast<std::uint32_t>(reasonLen));
    blob.insert(blob.end(), reason.begin(), reason.end());

    std::array<std::uint8_t, 32> mac{};
    paimon::crypto::hmacSha256(
        std::span<std::uint8_t const>(sealKey().data(), sealKey().size()),
        std::span<std::uint8_t const>(blob.data(), blob.size()),
        mac.data());
    blob.insert(blob.end(), mac.begin(), mac.end());
    return blob;
}

CacheRead openRecord(std::span<std::uint8_t const> bytes) {
    CacheRead result;
    if (bytes.empty()) return result;

    // plain json from older builds: revalidate silently instead of warning.
    if (bytes[0] == '{') return result;

    if (bytes.size() > kMaxBanCacheBytes) {
        result.state = CacheState::Tampered;
        return result;
    }
    if (bytes.size() < sizeof(kBanMagic) + 4 + 4 + 8 + 1 + 4 + kMacBytes) {
        result.state = CacheState::Tampered;
        return result;
    }
    if (std::memcmp(bytes.data(), kBanMagic, sizeof(kBanMagic)) != 0) {
        result.state = CacheState::Tampered;
        return result;
    }

    std::size_t const signedSize = bytes.size() - kMacBytes;
    std::array<std::uint8_t, 32> mac{};
    paimon::crypto::hmacSha256(
        std::span<std::uint8_t const>(sealKey().data(), sealKey().size()),
        std::span<std::uint8_t const>(bytes.data(), signedSize),
        mac.data());
    if (!paimon::crypto::constantTimeEquals(
            std::span<std::uint8_t const>(mac.data(), mac.size()),
            std::span<std::uint8_t const>(bytes.data() + signedSize, kMacBytes))) {
        result.state = CacheState::Tampered;
        return result;
    }

    std::size_t offset = sizeof(kBanMagic);
    std::uint32_t version = 0;
    std::uint32_t account = 0;
    std::uint64_t checkedAt = 0;
    std::uint8_t banned = 0;
    std::uint32_t reasonLen = 0;
    if (!takeU32(bytes, signedSize, offset, version) || version != kBanCacheVersion ||
        !takeU32(bytes, signedSize, offset, account) ||
        !takeU64(bytes, signedSize, offset, checkedAt) ||
        !takeU8(bytes, signedSize, offset, banned) ||
        !takeU32(bytes, signedSize, offset, reasonLen) ||
        account > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        banned > 1 || reasonLen > kMaxReasonBytes || offset + reasonLen != signedSize) {
        result.state = CacheState::Tampered;
        return result;
    }

    BanRecord record;
    record.accountID = static_cast<std::int32_t>(account);
    record.checkedAt = static_cast<std::int64_t>(checkedAt);
    record.banned = banned != 0;
    record.reason = paimon::text::sanitizeDisplayText(
        std::string_view(reinterpret_cast<char const*>(bytes.data() + offset), reasonLen),
        kMaxReasonBytes);

    std::int64_t const now = nowEpochSeconds();
    if (record.accountID != currentAccountID()) {
        result.state = CacheState::Stale;
        return result;
    }
    // clock edits must never discard an authenticated ban. allow records
    // still undergo clock validation, but never grant online access themselves.
    if (!record.banned && (record.checkedAt <= 0 || now < record.checkedAt)) {
        result.state = CacheState::Tampered;
        return result;
    }

    result.state = CacheState::Valid;
    result.record = std::move(record);
    return result;
}

std::string encodeHex(std::span<std::uint8_t const> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(bytes.size() * 2, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }
    return out;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decodeHex(std::string_view text, std::vector<std::uint8_t>& out) {
    if (text.size() % 2 != 0 || text.size() > kMaxBanCacheBytes * 2) return false;
    out.resize(text.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        int const high = hexNibble(text[i * 2]);
        int const low = hexNibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

CacheRead readBanCache() {
    auto path = banCacheFile();
    std::error_code ec;
    bool const exists = std::filesystem::exists(path, ec);
    if (ec) return {CacheState::Tampered, {}};
    if (!exists) return {};
    if (!std::filesystem::is_regular_file(path, ec)) return {CacheState::Tampered, {}};

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {CacheState::Tampered, {}};
    std::streamoff const size = f.tellg();
    if (size <= 0) return {CacheState::Tampered, {}};
    if (size > static_cast<std::streamoff>(kMaxBanCacheBytes)) {
        return {CacheState::Tampered, {}};
    }
    f.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!f) return {CacheState::Tampered, {}};

    return openRecord(std::span<std::uint8_t const>(bytes.data(), bytes.size()));
}

CacheRead readSavedBanCache() {
    auto const encoded = Mod::get()->getSavedValue<std::string>(kSavedCopyKey, "");
    if (encoded.empty()) return {};

    std::vector<std::uint8_t> bytes;
    if (!decodeHex(encoded, bytes)) return {CacheState::Tampered, {}};
    return openRecord(std::span<std::uint8_t const>(bytes.data(), bytes.size()));
}

void writeSavedCopy(BanRecord const& record) {
    auto const sealed = sealRecord(record);
    Mod::get()->setSavedValue<std::string>(
        kSavedCopyKey,
        encodeHex(std::span<std::uint8_t const>(sealed.data(), sealed.size())));
}

bool sameRecord(BanRecord const& first, BanRecord const& second) {
    return first.accountID == second.accountID && first.checkedAt == second.checkedAt &&
        first.banned == second.banned && first.reason == second.reason;
}

// writes both mirrors. a known ban must survive deleting either copy, so the
// two records are always repaired together.
void persistRecord(BanRecord const& record) {
    auto const sealed = sealRecord(record);
    auto const path = banCacheFile();

    // tmp+rename: a crash mid-write must not leave a truncated .paimon.
    if (!paimon::file::writeAtomically(
            path, std::span<std::uint8_t const>(sealed.data(), sealed.size()))) {
        log::warn("[BanGate] Failed to write the sealed ban cache");
    }

#ifdef _WIN32
    // cosmetic: keep it out of file browsers that hide hidden entries.
    auto const attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(path.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
    }
#endif

    // redundant sealed copy: deleting the file alone must not clear a known ban.
    writeSavedCopy(record);
    // persist the mirror shortly after the verdict instead of waiting for exit.
    paimon::requestDeferredModSave();
}

void writeBanCache(std::int32_t accountID, bool banned, std::string const& reason) {
    BanRecord record;
    record.accountID = accountID;
    record.checkedAt = nowEpochSeconds();
    record.banned = banned;
    record.reason = paimon::text::sanitizeDisplayText(reason, kMaxReasonBytes);

    // Avoid rewriting disk mirrors and forcing Mod::saveData() every 60s when
    // the verdict is identical to what we already persisted recently.
    constexpr std::int64_t kPersistRefreshSeconds = 1800;
    static std::optional<BanRecord> s_lastPersisted;
    if (s_lastPersisted &&
        sameRecord(*s_lastPersisted, record) &&
        record.checkedAt >= s_lastPersisted->checkedAt &&
        (record.checkedAt - s_lastPersisted->checkedAt) < kPersistRefreshSeconds) {
        return;
    }

    persistRecord(record);
    s_lastPersisted = record;
}

// mainthreaddelay drops callbacks during shutdown, so the ban popup schedules
// its own task.
void showBannedPopupForced(std::string const& reason, float delay) {
    if (g_popupScheduled || paimon::ban::isBannedPopupActive()) return;

    auto* director = CCDirector::get();
    if (!director) return;
    auto* sched = director->getScheduler();
    if (!sched) return;

    struct Task final : CCObject {
        std::string reason;
        void fire(float) {
            g_popupScheduled = false;
            if (auto* dir = CCDirector::get())
                if (auto* s = dir->getScheduler())
                    s->unscheduleSelector(schedule_selector(Task::fire), this);
            // never touch cocos during teardown: the scene graph may be gone.
            if (paimon::isRuntimeShuttingDown()) {
                this->release();
                return;
            }
            paimon::ban::showBannedPopup(reason);
            this->release();
        }
    };

    auto* t = new Task();
    t->reason = paimon::text::sanitizeDisplayText(reason, kMaxReasonBytes);
    g_popupScheduled = true;
    sched->scheduleSelector(schedule_selector(Task::fire), t, 0.f, 0, std::max(0.f, delay), false);
}

void stopOnlineSessions() {
    g_onlineSessionsActive = false;
    paimon::thumbalerts::ThumbFeedSocket::get().stop();
    paimon::collab::CollabManager::get().disconnect();
    paimon::versus::net::setRival(0);
    paimon::versus::net::stopListening();
    paimon::net::WebSocketClient::shutdownAll();
}

// never touch the runtime shutdown latch or threadtracker here: both are
// one-way with no reset, and tripping them while the game keeps running would
// make schedulemainthreaddelay/spawn no-op forever.
void enforceBan(std::string const& reason) {
    auto const cleanReason = paimon::text::sanitizeDisplayText(reason, kMaxReasonBytes);
    if (!cleanReason.empty()) g_banReason = cleanReason;

    {
        std::lock_guard lock(g_sessionMutex);
        g_session.markBanned();
    }

    // a known ban is latched, but a live verdict can enrich its reason. always
    // revoke credentials and close online sessions, even if the flag came from
    // a cached record before the server answered.
    HttpClient::get().clearAllAuthTokens();

    if (!g_banned.exchange(true, std::memory_order_acq_rel)) {
        stopOnlineSessions();
        HttpClient::get().cleanTasks(false);
    }

    if (g_banReason.empty()) g_banReason = "Banned by the Paimbnails server.";
    g_nextPopupAt = paimon::ban::Session::Clock::now() + std::chrono::seconds(5);
    showBannedPopupForced(g_banReason, 1.5f);
}

void applyCachedBan() {
    CacheRead const fileRead = readBanCache();
    CacheRead const savedRead = readSavedBanCache();

    bool const tampered =
        fileRead.state == CacheState::Tampered || savedRead.state == CacheState::Tampered;
    if (tampered) {
        log::warn("[BanGate] Ban cache failed its integrity check; forcing revalidation");
    }

    // a known ban wins over any allow mirror, regardless of its timestamp.
    // only a successful live server response may replace both with an unban.
    std::optional<BanRecord> cache;
    auto consider = [&cache](CacheRead const& read) {
        if (read.state != CacheState::Valid) return;
        if (!cache || paimon::ban::preferCachedVerdict(
                read.record.banned, read.record.checkedAt, cache->banned, cache->checkedAt)) {
            cache = read.record;
        }
    };
    consider(fileRead);
    consider(savedRead);

    if (cache && cache->banned) {
        // repair both mirrors, or deleting .paimon reveals an older allow
        // record in save.json, and deleting that value reveals the file.
        bool const needsRepair =
            fileRead.state != CacheState::Valid || savedRead.state != CacheState::Valid ||
            !sameRecord(fileRead.record, *cache) || !sameRecord(savedRead.record, *cache);
        if (needsRepair) {
            log::warn("[BanGate] Repairing ban cache mirrors from the strongest record.");
            persistRecord(*cache);
        }

        log::warn("[BanGate] Applying cached ban immediately; awaiting server revalidation.");
        enforceBan(cache->reason);
    }
}

void monitorBanStatus() {
    if (paimon::isRuntimeShuttingDown()) return;
    paimon::scheduleMainThreadDelay(1.f, monitorBanStatus);

    auto const account = currentIdentity();
    auto const now = paimon::ban::Session::Clock::now();
    if (account != g_monitoredAccount) {
        g_monitoredAccount = account;
        g_checkInFlight = false;
        ++g_checkGeneration;
        g_nextCheck = {};
        {
            std::lock_guard lock(g_sessionMutex);
            g_session.invalidate();
        }
        if (g_onlineSessionsActive) stopOnlineSessions();
        applyCachedBan();
    }

    if (g_onlineSessionsActive && !paimon::ban::networkAuthorization()) {
        stopOnlineSessions();
    }

    // a paused/closed popup must not become an escape hatch: re-show it while
    // the ban is active and keep keybinds/ui locked.
    if (paimon::ban::isBanned() && !paimon::ban::isBannedPopupActive() && now >= g_nextPopupAt) {
        g_nextPopupAt = now + std::chrono::seconds(10);
        showBannedPopupForced(g_banReason, 0.f);
    }

    if (!paimon::onboarding::isAccepted() || !account.isValid()) return;
    // a canceled httpclient operation must not strand the monitor permanently.
    if (g_checkInFlight && now - g_checkStarted >= std::chrono::seconds(40)) {
        g_checkInFlight = false;
        ++g_checkGeneration;
        {
            std::lock_guard lock(g_sessionMutex);
            g_session.invalidate();
        }
        if (g_onlineSessionsActive) stopOnlineSessions();
    }
    if (g_checkInFlight || now < g_nextCheck) return;

    g_checkInFlight = true;
    g_checkStarted = now;
    g_nextCheck = now + std::chrono::seconds(60);
    auto const generation = ++g_checkGeneration;
    HttpClient::get().checkBanned([account, generation](bool ok, bool banned, std::string const& reason) {
        if (paimon::isRuntimeShuttingDown() || generation != g_checkGeneration) return;
        g_checkInFlight = false;
        auto const current = currentIdentity();
        if (current != account) {
            g_nextCheck = {};
            return;
        }
        {
            std::lock_guard lock(g_sessionMutex);
            g_session.recordVerdict(account, current,
                ok ? std::optional<bool>(banned) : std::nullopt,
                paimon::ban::Session::Clock::now());
        }
        if (!ok) {
            if (g_onlineSessionsActive) stopOnlineSessions();
            if (!g_reportedFailure) {
                log::warn("[BanGate] Verification unavailable; online access remains locked.");
                if (!g_banned.load(std::memory_order_acquire)) {
                    Notification::create("Ban verification unavailable. Online features are locked.",
                        NotificationIcon::Warning, 6.f)->show();
                }
                g_reportedFailure = true;
            }
            return;
        }
        g_reportedFailure = false;
        writeBanCache(account.accountID, banned, reason);
        if (banned) {
            log::warn("[BanGate] Applying server ban.");
            enforceBan(reason);
        } else if (!g_banned.load(std::memory_order_acquire) && !g_onlineSessionsActive) {
            g_onlineSessionsActive = true;
            HttpClient::get().cleanTasks();
            paimon::thumbalerts::ThumbFeedSocket::get().resumeAfterConsent();
            if (HttpClient::get().hasSessionToken() || !HttpClient::get().getModCode().empty()) {
                paimon::modauth::refreshSession();
            }
        }
    });
}

} // namespace

namespace paimon::ban {

bool isBanned() {
    return g_banned.load(std::memory_order_acquire);
}

std::uint64_t authorizeWebRequest(std::string_view method, std::string_view url) {
    if (paimon::isRuntimeShuttingDown()) return 0;
    if (isVerificationRequest(method, url)) return std::numeric_limits<std::uint64_t>::max();
    return networkAuthorization();
}

std::uint64_t networkAuthorization() {
    if (paimon::isRuntimeShuttingDown() || isBanned()) return 0;
    auto const account = currentIdentity();
    std::lock_guard lock(g_sessionMutex);
    return g_session.authorization(account, Session::Clock::now());
}

bool runStartupBanGate() {
    if (!g_monitorStarted) {
        g_monitorStarted = true;
        g_monitoredAccount = currentIdentity();
        applyCachedBan();
        paimon::scheduleMainThreadDelay(0.1f, monitorBanStatus);
    }
    return isBanned();
}

} // namespace paimon::ban
