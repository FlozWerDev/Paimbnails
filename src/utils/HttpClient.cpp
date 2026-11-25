#include "FilesystemPath.hpp"
#include "HttpClient.hpp"
#include "Debug.hpp"
#include "WebHelper.hpp"
#include "ThreadTracker.hpp"
#include "MainThreadDelay.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../core/Settings.hpp"
#include "../core/ModAuthFlow.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <random>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <filesystem>
#include "FormatDetect.hpp"
#include "Base64.hpp"
#include "HttpUrl.hpp"
#include "BanVerdict.hpp"
#include "SafeText.hpp"
#include "../core/BanPolicy.hpp"

using namespace geode::prelude;

// forward-declared with the other helpers below; used early by the URL setters.
namespace { void trimTrailingSlash(std::string& url); }

namespace {
GJAccountManager* getSafeAccountManager() {
    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        log::warn("[HttpClient] GJAccountManager unavailable");
    }
    return accountManager;
}

int getSafeAccountID() {
    if (auto* accountManager = getSafeAccountManager()) {
        return accountManager->m_accountID;
    }
    return 0;
}

std::string getSafeAccountUsername() {
    if (auto* accountManager = getSafeAccountManager()) {
        return accountManager->m_username;
    }
    return "";
}

// only these mean "missing" for negative caching; 0 (timeout), 5xx and 429
// are transient and blanked levels on server hiccups when cached as absent.
bool isMissingStatus(int status) {
    return status == 404 || status == 410;
}

// bounded retries for auth/ban verdicts: fail-closed, with backoff, and no
// hammering of an explicit rate limit.
constexpr int kVerdictMaxAttempts = 3;
constexpr float kModCheckRetryDelays[kVerdictMaxAttempts - 1] = {0.5f, 1.5f};
constexpr float kBanCheckRetryDelays[kVerdictMaxAttempts - 1] = {1.0f, 2.0f};

bool isRateLimitFailure(std::string const& response) {
    return response.starts_with("HTTP 429") || response.find("RATE_LIMITED") != std::string::npos;
}

// shared account tail for upload form fields; callers prepend path/id keys.
std::vector<std::pair<std::string, std::string>> accountFieldTail(std::string const& username) {
    auto account = AccountVerifier::get().verify();
    return {
        {"username", username},
        {"accountID", std::to_string(account.accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };
}

std::string extractServerMessage(std::string const& response, std::string const& fallback) {
    auto parsed = matjson::parse(response);
    if (parsed.isOk()) {
        auto msgVal = parsed.unwrap()["message"].asString();
        if (msgVal.isOk()) return msgVal.unwrap();
    }
    return fallback;
}

std::string profileUploadMessage(std::string const& response, std::string const& fallback) {
    std::string result = fallback;
    auto jsonRes = matjson::parse(response);
    if (!jsonRes.isOk()) return result;
    auto json = jsonRes.unwrap();
    if (json.contains("pendingVerification") && json["pendingVerification"].asBool().unwrapOr(false)) {
        result = "pending_verification";
    }
    auto msgVal = json["message"].asString();
    if (msgVal.isOk() && !msgVal.unwrap().empty()) result = msgVal.unwrap();
    return result;
}

}

HttpClient::HttpClient() {
    m_serverURL = "https://api.flozwer.org";
    m_forumServerURL = "https://paimbnailsbot.onrender.com";

    // shipped with the client, never persisted.
    m_apiKey = "074b91c9-6631-4670-a6f08a2ce970-0183-471b";

    m_modCode = Mod::get()->getSavedValue<std::string>("mod-code", "");
    m_viewerToken = Mod::get()->getSavedValue<std::string>("viewer-token", "");
    m_sessionToken = Mod::get()->getSavedValue<std::string>("session-token", "");
    m_sessionUsername = Mod::get()->getSavedValue<std::string>("session-user", "");
    m_sessionAccountID = static_cast<int>(Mod::get()->getSavedValue<int64_t>("session-account", 0));
    m_sessionExpiresAt = Mod::get()->getSavedValue<int64_t>("session-expires-at", 0);
    m_callbackGate = std::make_shared<std::atomic<bool>>(true);

    // role authority is the volatile modauth session, never disk: stale
    // persisted flags from older builds grant nothing.
    paimon::modauth::clearVerifiedSession();

    loadManifestFromDisk();

    PaimonDebug::log("[HttpClient] Initialized with server: {}", m_serverURL);
    PaimonDebug::log("[HttpClient] Forum server: {}", m_forumServerURL);
}

void HttpClient::cleanTasks(bool allowNewRequests) {
    if (m_callbackGate) {
        m_callbackGate->store(false, std::memory_order_release);
    }

    std::vector<std::function<void(bool)>> manifestCallbacks;
    {
        std::lock_guard<std::mutex> lock(m_manifestFetchMutex);
        m_manifestFetchInFlight = false;
        manifestCallbacks = std::move(m_manifestPendingCallbacks);
        m_manifestPendingCallbacks.clear();
    }

    std::vector<std::pair<int, std::vector<DownloadCallback>>> downloads;
    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        downloads.reserve(m_inflightDownloads.size());
        for (auto& [levelId, callbacks] : m_inflightDownloads) {
            downloads.emplace_back(levelId, std::move(callbacks));
        }
        m_inflightDownloads.clear();
    }

    std::vector<std::vector<ModeratorCallback>> modChecks;
    {
        std::lock_guard<std::mutex> lock(m_inflightModMutex);
        modChecks.reserve(m_inflightModChecks.size());
        for (auto& [key, callbacks] : m_inflightModChecks) {
            modChecks.push_back(std::move(callbacks));
        }
        m_inflightModChecks.clear();
    }

    bool const invokeCallbacks = allowNewRequests && !paimon::isRuntimeShuttingDown();
    if (invokeCallbacks) {
        for (auto& cb : manifestCallbacks) {
            if (cb) cb(false);
        }
        std::vector<uint8_t> empty;
        for (auto& [levelId, callbacks] : downloads) {
            for (auto& cb : callbacks) {
                if (cb) cb(false, empty, 0, 0);
            }
        }
        for (auto& callbacks : modChecks) {
            for (auto& cb : callbacks) {
                if (cb) cb(false, false);
            }
        }
    }

    if (allowNewRequests && !paimon::ban::isBanned()) {
        m_callbackGate = std::make_shared<std::atomic<bool>>(true);
        {
            std::lock_guard lock(m_notFoundMutex);
            m_notFoundCache.clear();
        }
        ThumbnailLoader::get().clearFailedCache();
    } else {
        m_callbackGate.reset();
    }
}

void HttpClient::setServerURL(std::string const& url) {
    m_serverURL = url;
    trimTrailingSlash(m_serverURL);
    PaimonDebug::log("[HttpClient] Server URL updated to: {}", m_serverURL);
}

std::string HttpClient::buildAssetURL(std::string const& path, std::string const& defaultFolder) const {
    if (path.empty()) return {};

    if (path.starts_with("http://") || path.starts_with("https://")) {
        return path;
    }

    std::string normalized = path;
    if (auto pos = normalized.find_first_not_of('/'); pos == std::string::npos) {
        normalized.clear();
    } else if (pos > 0) {
        normalized.erase(0, pos);
    }

    if (!defaultFolder.empty() && normalized.find('/') == std::string::npos) {
        auto prefix = defaultFolder + "/";
        normalized = prefix + normalized;
    }

    return m_serverURL + "/" + normalized;
}

void HttpClient::setForumServerURL(std::string const& url) {
    m_forumServerURL = url;
    trimTrailingSlash(m_forumServerURL);
    PaimonDebug::log("[HttpClient] Forum server URL updated to: {}", m_forumServerURL);
}

namespace {
void trimTrailingSlash(std::string& url) {
    if (!url.empty() && url.back() == '/') url.pop_back();
}

std::string encodeQueryParamImpl(std::string const& value, bool spaceAsPlus) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);

    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }
        if (spaceAsPlus && ch == ' ') {
            encoded.push_back('+');
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(HEX[ch >> 4]);
        encoded.push_back(HEX[ch & 0xF]);
    }

    return encoded;
}
}

std::string HttpClient::encodeQueryParam(std::string const& value) {
    return encodeQueryParamImpl(value, false);
}

std::string HttpClient::encodeQueryParamForm(std::string const& value) {
    return encodeQueryParamImpl(value, true);
}

std::string HttpClient::activeSessionToken() const {
    if (m_sessionToken.empty() || m_sessionAccountID <= 0) return {};
    int const currentID = getSafeAccountID();
    if (currentID <= 0 || currentID != m_sessionAccountID) return {};
    if (m_sessionExpiresAt > 0) {
        auto const nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (m_sessionExpiresAt <= nowMs) return {};
    }
    return m_sessionToken;
}

std::string HttpClient::activeModCode() const {
    if (m_modCode.empty()) return {};
    int const storedAccount = static_cast<int>(Mod::get()->getSavedValue<int64_t>("mod-credential-account", 0));
    int const currentID = getSafeAccountID();
    if (storedAccount > 0 && currentID > 0 && storedAccount != currentID) return {};
    return m_modCode;
}

std::string HttpClient::getSessionToken() const {
    return activeSessionToken();
}

bool HttpClient::hasSessionToken() const {
    return !activeSessionToken().empty();
}

void HttpClient::setSessionToken(std::string const& token, std::string const& username, int accountID, int64_t expiresAt) {
    m_sessionToken = token;
    m_sessionUsername = token.empty() ? std::string() : username;
    m_sessionAccountID = token.empty() ? 0 : accountID;
    m_sessionExpiresAt = token.empty() ? 0 : expiresAt;
    auto* mod = Mod::get();
    mod->setSavedValue("session-token", m_sessionToken);
    mod->setSavedValue("session-user", m_sessionUsername);
    mod->setSavedValue<int64_t>("session-account", m_sessionAccountID);
    mod->setSavedValue<int64_t>("session-expires-at", m_sessionExpiresAt);
    PaimonDebug::log("[HttpClient] Unified session {}.", token.empty() ? "cleared" : "updated");
}

void HttpClient::clearAllAuthTokens() {
    paimon::modauth::clearVerifiedSession();
    setSessionToken("", "", 0, 0);
    setModCode("");
    setViewerToken("");
    auto* mod = Mod::get();
    mod->setSavedValue<int64_t>("mod-code-expires-at", 0);
    mod->setSavedValue<int64_t>("mod-credential-account", 0);
    mod->setSavedValue("mod-credential-user", std::string());
}

void HttpClient::startUnifiedAuth(std::string const& username, int accountID, GenericCallback callback) {
    matjson::Value body = matjson::makeObject({
        {"username", username},
        {"accountID", accountID}
    });
    postWithoutModCode("/api/auth/start", body.dump(), std::move(callback));
}

void HttpClient::completeUnifiedAuth(std::string const& challengeToken, GenericCallback callback) {
    matjson::Value body = matjson::makeObject({
        {"challengeToken", challengeToken}
    });
    postWithoutModCode("/api/auth/complete", body.dump(), std::move(callback));
}

void HttpClient::fetchAuthSession(GenericCallback callback) {
    std::string const username = getSafeAccountUsername();
    int const accountID = getSafeAccountID();
    std::string endpoint = "/api/auth/session";
    if (!username.empty() && accountID > 0) {
        endpoint += "?username=" + encodeQueryParam(username) + "&accountID=" + std::to_string(accountID);
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json",
        "Cache-Control: no-cache, no-store"
    };
    performRequest(m_serverURL + endpoint, "GET", "", headers, std::move(callback), true);
}

void HttpClient::logoutAuthSession(GenericCallback callback) {
    std::string const token = activeSessionToken();
    if (token.empty()) {
        clearAllAuthTokens();
        if (callback) callback(true, R"({"success":true,"revoked":false})");
        return;
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Session-Token: " + token,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(m_serverURL + "/api/auth/logout", "POST", "{}", headers,
        [this, callback = std::move(callback)](bool ok, std::string const& response) {
            clearAllAuthTokens();
            if (callback) callback(ok, response);
        }, false);
}

void HttpClient::setModCode(std::string const& code) {
    paimon::modauth::clearVerifiedSession();
    m_modCode = code;
    Mod::get()->setSavedValue("mod-code", code);
    PaimonDebug::log("[HttpClient] Mod code updated.");
}

void HttpClient::setViewerToken(std::string const& token) {
    m_viewerToken = token;
    Mod::get()->setSavedValue("viewer-token", token);
    PaimonDebug::log("[HttpClient] Viewer token {}.", token.empty() ? "cleared" : "updated");
}

void HttpClient::startAccountVerification(std::string const& username, GenericCallback callback) {
    matjson::Value body = matjson::makeObject({ {"username", username} });
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(m_serverURL + "/api/request-verify/start", "POST", body.dump(), headers,
                   std::move(callback), false);
}

void HttpClient::checkAccountVerification(std::string const& username, GenericCallback callback) {
    matjson::Value body = matjson::makeObject({ {"username", username} });
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(m_serverURL + "/api/request-verify/check", "POST", body.dump(), headers,
        [callback = std::move(callback)](bool success, std::string const& response) {
            if (success) {
                auto parsed = matjson::parse(response);
                if (parsed.isOk()) {
                    auto token = parsed.unwrap()["token"].asString().unwrapOr("");
                    if (!token.empty()) HttpClient::get().setViewerToken(token);
                }
            }
            if (callback) callback(success, response);
        }, false);
}

void HttpClient::startModCodeSetup(std::string const& username, int accountID, GenericCallback callback) {
    matjson::Value body = matjson::makeObject({
        {"username", username},
        {"accountID", accountID}
    });
    postWithoutModCode("/api/mod-auth/start", body.dump(), std::move(callback));
}

void HttpClient::completeModCodeSetup(std::string const& challengeToken, GenericCallback callback) {
    matjson::Value body = matjson::makeObject({
        {"challengeToken", challengeToken}
    });
    postWithoutModCode("/api/mod-auth/complete", body.dump(), std::move(callback));
}

// credential headers to strip from untrusted urls. public methods take full
// urls and can be pointed at the cdn/forum, so filter at this chokepoint.
static bool isCredentialHeader(std::string const& key) {
    static constexpr std::string_view kCredentialKeys[] = {
        "x-session-token", "x-mod-code", "x-viewer-token", "x-api-key",
        "authorization", "x-admin-user", "x-bot-service-secret"
    };
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto const& candidate : kCredentialKeys) {
        if (lower == candidate) return true;
    }
    return false;
}

struct ExplicitHeaderFlags {
    bool hasModCode = false;
    bool hasSessionToken = false;
    bool hasViewerToken = false;
};

static bool headerEqualsIgnoreCase(std::string const& a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

// applies headers and detects explicit auth headers so performRequest never duplicates them.
static void applyHeaderList(web::WebRequest& req, std::vector<std::string> const& headers,
                            ExplicitHeaderFlags* outFlags = nullptr, bool allowCredentials = true) {
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos == std::string::npos) continue;
        std::string key = header.substr(0, colonPos);
        std::string value = header.substr(colonPos + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        if (key.empty() || value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
            continue;
        }
        bool validKey = std::all_of(key.begin(), key.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '!' || c == '#' || c == '$' || c == '%'
                || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-'
                || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
        });
        if (!validKey) continue;
        if (!allowCredentials && isCredentialHeader(key)) {
            PaimonDebug::log("[HttpClient] Dropping credential header '{}' for third-party host", key);
            continue;
        }
        if (headerEqualsIgnoreCase(key, "x-mod-code")) {
            if (outFlags) {
                if (outFlags->hasModCode) continue;
                outFlags->hasModCode = true;
            }
        } else if (headerEqualsIgnoreCase(key, "x-session-token")) {
            if (outFlags) {
                if (outFlags->hasSessionToken) continue;
                outFlags->hasSessionToken = true;
            }
        } else if (headerEqualsIgnoreCase(key, "x-viewer-token")) {
            if (outFlags) {
                if (outFlags->hasViewerToken) continue;
                outFlags->hasViewerToken = true;
            }
        }
        req.header(key, value);
    }
}

// credentials are restricted to configured backend origins.
bool HttpClient::isTrustedBackendUrl(std::string const& url) const {
    auto target = paimon::net::parseHttpUrl(url);
    if (!target) return false;
    for (auto const& backend : {m_serverURL, m_forumServerURL, std::string("https://api.flozwer.org")}) {
        auto trusted = paimon::net::parseHttpUrl(backend);
        if (trusted && target->sameOrigin(*trusted)) return true;
    }
    return false;
}

void HttpClient::performRequest(
    std::string const& url,
    std::string const& method,
    std::string const& postData,
    std::vector<std::string> const& headers,
    geode::CopyableFunction<void(bool, std::string const&)> callback,
    bool includeStoredModCode
) {
    auto callbackGate = m_callbackGate;
    auto req = web::WebRequest();
    req.timeout(asp::Duration::fromSecs(10));
    req.acceptEncoding("gzip, deflate");

    ExplicitHeaderFlags headerFlags;
    bool trustedHost = isTrustedBackendUrl(url);
    if (trustedHost) req.followRedirects(false);

    applyHeaderList(req, headers, &headerFlags, trustedHost);

    auto const sessionToken = activeSessionToken();
    auto const modCode = activeModCode();
    if (trustedHost) {
        // Attach each credential header at most once; Geode's WebRequest::header appends duplicates.
        if (includeStoredModCode && !headerFlags.hasSessionToken && !sessionToken.empty()) {
            req.header("X-Session-Token", sessionToken);
        }
        if (includeStoredModCode && !headerFlags.hasModCode && !modCode.empty()) {
            req.header("X-Mod-Code", modCode);
        }
        if (!headerFlags.hasViewerToken && !m_viewerToken.empty()) {
            req.header("X-Viewer-Token", m_viewerToken);
        }
    }

    if (method == "POST" && !postData.empty()) {
        req.bodyString(postData);
    }

    // capture stable state: workers may outlive the singleton.
    auto workerExhaustedRef = &m_workerExhausted;
    auto exhaustedAtRef = &m_exhaustedAt;
    auto consecutiveFailuresRef = &m_consecutiveWorkerFailures;
    WebHelper::dispatch(std::move(req), method, url, [callbackGate, callback, workerExhaustedRef, exhaustedAtRef, consecutiveFailuresRef](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        if (paimon::isRuntimeShuttingDown()) return;
        bool success = res.ok();
        std::string body = res.string().unwrapOr("");
        std::string responseStr = success
            ? body
            : ("HTTP " + std::to_string(res.code()) + ": " + (body.empty() ? std::string("Unknown error") : body));

        if (success) {
            consecutiveFailuresRef->store(0, std::memory_order_release);
            if (workerExhaustedRef->load(std::memory_order_acquire)) {
                workerExhaustedRef->store(false, std::memory_order_release);
                PaimonDebug::log("[HttpClient] Worker recovery: success response, clearing exhaustion flag");
            }
        }

        // count repeated server failures, but keep app rate limits transient.
        if (!success && res.code() == 503) {
            int newCount = consecutiveFailuresRef->fetch_add(1, std::memory_order_acq_rel) + 1;
            if (newCount >= EXHAUSTION_THRESHOLD) {
                workerExhaustedRef->store(true, std::memory_order_release);
                exhaustedAtRef->store(static_cast<int64_t>(std::time(nullptr)), std::memory_order_release);
                PaimonDebug::warn("[HttpClient] Worker marked exhausted after {} consecutive 503s", newCount);
            }
        } else if (!success && res.code() == 429) {
            // app limiter vs platform quota failure.
            bool isAppRateLimit = body.find("RATE_LIMITED") != std::string::npos
                || body.find("Rate limit exceeded") != std::string::npos;
            if (!isAppRateLimit) {
                int newCount = consecutiveFailuresRef->fetch_add(1, std::memory_order_acq_rel) + 1;
                if (newCount >= EXHAUSTION_THRESHOLD) {
                    workerExhaustedRef->store(true, std::memory_order_release);
                    exhaustedAtRef->store(static_cast<int64_t>(std::time(nullptr)), std::memory_order_release);
                    PaimonDebug::warn("[HttpClient] Worker marked exhausted after {} consecutive 429s", newCount);
                }
            }
        }

        if (paimon::isRuntimeShuttingDown()) return;
        if (callback) callback(success, responseStr);
    });
}

// forwards to the status-code variant; downloads need 404 vs transient.
void HttpClient::performBinaryRequest(
    std::string const& url,
    std::vector<std::string> const& headers,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback,
    int timeoutSeconds,
    bool includeModCode
) {
    performBinaryRequestEx(url, headers,
        [callback = std::move(callback)](bool success, std::vector<uint8_t> const& data, int) {
            if (callback) callback(success, data);
        },
        timeoutSeconds, includeModCode);
}

void HttpClient::performBinaryRequestEx(
    std::string const& url,
    std::vector<std::string> const& headers,
    BinaryStatusCallback callback,
    int timeoutSeconds,
    bool includeModCode,
    bool validateImage,
    unsigned redirectsLeft,
    size_t maxBytes
) {
    auto callbackGate = m_callbackGate;
    auto req = web::WebRequest();
    req.timeout(asp::Duration::fromSecs(timeoutSeconds));
    req.userAgent("Paimbnails/2.x (Geode)");
    req.acceptEncoding("gzip, deflate");

    req.header("Accept", validateImage ? "image/webp,image/png,image/gif,*/*" : "*/*");

    bool trustedHost = isTrustedBackendUrl(url);
    req.followRedirects(false);
    ExplicitHeaderFlags headerFlags;
    applyHeaderList(req, headers, &headerFlags, trustedHost);

    if (includeModCode && trustedHost) {
        if (auto const sessionToken = activeSessionToken(); !headerFlags.hasSessionToken && !sessionToken.empty()) {
            req.header("X-Session-Token", sessionToken);
        }
        if (auto const modCode = activeModCode(); !headerFlags.hasModCode && !modCode.empty()) {
            req.header("X-Mod-Code", modCode);
        }
    }

    std::string urlCopy = url;

    WebHelper::dispatch(std::move(req), "GET", url,
        [this, callbackGate, callback, urlCopy, headers, timeoutSeconds, includeModCode,
         validateImage, redirectsLeft, maxBytes](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        if (paimon::isRuntimeShuttingDown()) return;
        int const code = res.code();
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            auto const location = res.header("Location");
            auto next = location ? paimon::net::resolveHttpRedirect(urlCopy, std::string(*location)) : std::nullopt;
            if (redirectsLeft && next && isUrlSafe(*next)) {
                // each hop rechecks the origin before attaching credentials.
                performBinaryRequestEx(*next, headers, callback, timeoutSeconds, includeModCode,
                                       validateImage, redirectsLeft - 1, maxBytes);
            } else if (callback) {
                callback(false, {}, code);
            }
            return;
        }
        bool success = res.ok();
        if (success) {
            // (1) Content-Length pre-check: don't buffer a body the server
            // already declares over maxBytes.
            if (auto const cl = res.header("Content-Length")) {
                std::string clStr(*cl);
                auto const first = clStr.find_first_not_of(" \t");
                auto const last = clStr.find_last_not_of(" \t");
                if (first != std::string::npos) {
                    clStr = clStr.substr(first, last - first + 1);
                    unsigned long long declared = 0;
                    auto const begin = clStr.data();
                    auto const end = begin + clStr.size();
                    auto const [ptr, ec] = std::from_chars(begin, end, declared);
                    if (ec == std::errc{} && ptr == end && declared > maxBytes) {
                        log::debug("[HttpClient] Binary GET {} aborted: Content-Length {} exceeds cap {} (HTTP {})",
                            urlCopy, declared, maxBytes, code);
                        if (callback) callback(false, {}, code);
                        return;
                    }
                }
            }
        }
        std::vector<uint8_t> data = success ? res.data() : std::vector<uint8_t>{};
        // (2) accumulated-size check: covers missing or lying Content-Length.
        if (success && data.size() > maxBytes) {
            log::debug("[HttpClient] Binary GET {} aborted: body size {} exceeds cap {} (HTTP {})",
                urlCopy, data.size(), maxBytes, code);
            if (callback) callback(false, {}, code);
            return;
        }

        int statusCode = res.code();
        PaimonDebug::log("[HttpClient] Binary GET {} -> status={}, size={}", urlCopy, statusCode, data.size());

        if (success && !data.empty()) {
            auto ct = res.header("Content-Type");
            std::string contentType = ct.has_value() ? std::string(ct.value()) : "";
            PaimonDebug::log("[HttpClient] Binary response Content-Type: {}", contentType);

            if (contentType.find("application/json") != std::string::npos ||
                contentType.find("text/html") != std::string::npos) {
                std::string body(data.begin(), data.begin() + std::min(data.size(), (size_t)500));
                PaimonDebug::log("[HttpClient] Binary request got non-image response: {}", body);
                success = false;
                data.clear();
            }

            if (success && validateImage) {
                auto fmt = paimon::format::detect(data.data(), data.size());
                bool validImage = (fmt != paimon::format::ImageFormat::Unknown);

                if (!validImage) {
                    std::string preview(data.begin(), data.begin() + std::min(data.size(), (size_t)200));
                    PaimonDebug::log("[HttpClient] Binary response does not look like an image. First bytes: {}", preview);
                    success = false;
                    data.clear();
                }
            }
        }

        if (paimon::isRuntimeShuttingDown()) return;
        if (callback) callback(success, data, statusCode);
    });
}

void HttpClient::performUpload(
    std::string const& url,
    std::string const& fieldName,
    std::string const& filename,
    std::vector<uint8_t> const& data,
    std::vector<std::pair<std::string, std::string>> const& formFields,
    std::vector<std::string> const& headers,
    geode::CopyableFunction<void(bool, std::string const&)> callback,
    std::string const& fileContentType
) {
    auto callbackGate = m_callbackGate;
    web::MultipartForm form;

    for (auto const& field : formFields) {
        form.param(field.first, field.second);
    }

    form.file(fieldName, std::span<uint8_t const>(data), filename, fileContentType);

    auto req = web::WebRequest();
    req.timeout(asp::Duration::fromSecs(30));
    req.acceptEncoding("gzip, deflate");

    bool trustedHost = isTrustedBackendUrl(url);
    if (trustedHost) req.followRedirects(false);
    ExplicitHeaderFlags headerFlags;
    applyHeaderList(req, headers, &headerFlags, trustedHost);

    if (trustedHost) {
        if (auto const sessionToken = activeSessionToken(); !headerFlags.hasSessionToken && !sessionToken.empty()) {
            req.header("X-Session-Token", sessionToken);
        }
        if (!headerFlags.hasViewerToken && !m_viewerToken.empty()) {
            req.header("X-Viewer-Token", m_viewerToken);
        }
    }

    req.bodyMultipart(form);

    WebHelper::dispatch(std::move(req), "POST", url, [callbackGate, callback](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        bool success = res.ok();
        std::string body = res.string().unwrapOr("");
        std::string responseStr = success
            ? body
            : ("HTTP " + std::to_string(res.code()) + ": " + (body.empty() ? std::string("Unknown error") : body));

        if (callback) callback(success, responseStr);
    });
}

void HttpClient::uploadProfileMedia(int accountID, std::vector<uint8_t> const& data, std::string const& username,
        UploadCallback callback, char const* endpoint, char const* ext, char const* contentType,
        char const* kindLabel, bool checkPendingVerification) {
    PaimonDebug::log("[HttpClient] Uploading profile background {}for account {} ({} bytes)", kindLabel, accountID, data.size());

    std::string url = m_serverURL + endpoint;
    std::string filename = std::to_string(accountID) + "." + ext;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"levelId", std::to_string(accountID)},
    };
    auto tail = accountFieldTail(username);
    formFields.insert(formFields.end(), tail.begin(), tail.end());

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    std::string successFallback = fmt::format("Profile {}upload successful", kindLabel);
    std::string failurePrefix = fmt::format("Profile {}upload failed: ", kindLabel);
    performUpload(
        url,
        "image",
        filename,
        data,
        formFields,
        headers,
        [callback = std::move(callback), accountID, kindLabel = std::string(kindLabel),
         successFallback, failurePrefix, checkPendingVerification](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile {}upload successful for account {}", kindLabel, accountID);
                auto msg = checkPendingVerification
                    ? profileUploadMessage(response, successFallback)
                    : extractServerMessage(response, successFallback);
                callback(true, msg);
            } else {
                log::error("[HttpClient] Profile {}upload failed for account {}: {}", kindLabel, accountID, response);
                callback(false, failurePrefix + response);
            }
        },
        contentType
    );
}

void HttpClient::uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    uploadProfileMedia(accountID, pngData, username, std::move(callback),
        "/api/backgrounds/upload", "png", "image/png", "", true);
}

void HttpClient::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    uploadProfileMedia(accountID, gifData, username, std::move(callback),
        "/api/backgrounds/upload-gif", "gif", "image/gif", "GIF ", true);
}

void HttpClient::uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback) {
    uploadProfileMedia(accountID, mp4Data, username, std::move(callback),
        "/api/backgrounds/upload-video", "mp4", "video/mp4", "video ", false);
}

void HttpClient::uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile image for account {} ({} bytes, type: {})", accountID, imgData.size(), contentType);

    std::string url = m_serverURL + "/api/profileimgs/upload";

    std::string ext = "png";
    if (contentType == "image/gif") ext = "gif";
    else if (contentType == "image/jpeg") ext = "jpg";
    else if (contentType == "image/webp") ext = "webp";
    else if (contentType == "image/bmp") ext = "bmp";
    else if (contentType == "image/tiff") ext = "tiff";

    std::string filename = "profileimg" + std::to_string(accountID) + "." + ext;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/profileimgs"},
        {"levelId", std::to_string(accountID)},
    };
    auto tail = accountFieldTail(username);
    formFields.insert(formFields.end(), tail.begin(), tail.end());

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        imgData,
        formFields,
        headers,
        [callback = std::move(callback), accountID](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile image upload successful for account {}", accountID);
                callback(true, profileUploadMessage(response, "Profile image upload successful"));
            } else {
                log::error("[HttpClient] Profile image upload failed for account {}: {}", accountID, response);
                callback(false, "Profile image upload failed: " + response);
            }
        },
        contentType
    );
}

void HttpClient::uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    uploadProfileImg(accountID, gifData, username, "image/gif", callback);
}

void HttpClient::downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf) {
    PaimonDebug::log("[HttpClient] Downloading profile image for account {} (self={})", accountID, isSelf);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/profileimgs/" + std::to_string(accountID);
        PaimonDebug::log("[HttpClient] Worker exhausted, trying CDN for profile img: {}", cdnUrl);
        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders, [callback = std::move(callback)](bool success, std::vector<uint8_t> const& data) {
            if (success && !data.empty()) {
                callback(true, data, 0, 0);
            } else {
                callback(false, {}, 0, 0);
            }
        });
        return;
    }

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    std::string url = m_serverURL + "/profileimgs/" + std::to_string(accountID);
    if (isSelf) {
        url += "?self=1";
    }

    performBinaryRequest(url, headers, [callback = std::move(callback), accountID](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Profile image downloaded for account {}: {} bytes", accountID, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No profile image found for account {}", accountID);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::uploadProfileConfig(int accountID, std::string const& jsonConfig, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile config for account {}", accountID);

    std::string url = m_serverURL + "/api/profiles/config/upload";

    web::MultipartForm form;
    form.param("accountID", std::to_string(accountID));
    // the server matches the gd username against the viewer token / mod code.
    form.param("username", getSafeAccountUsername());
    form.param("config", jsonConfig);

    auto req = web::WebRequest();
    req.acceptEncoding("gzip, deflate");
    req.followRedirects(false);
    req.header("X-API-Key", m_apiKey);
    if (!m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }
    if (!m_viewerToken.empty()) {
        req.header("X-Viewer-Token", m_viewerToken);
    }
    req.bodyMultipart(form);

    auto callbackGate = m_callbackGate;
    WebHelper::dispatch(std::move(req), "POST", url, [callbackGate, callback = std::move(callback)](web::WebResponse res) mutable {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        bool success = res.ok();
        std::string body = res.string().unwrapOr("");
        std::string responseStr = success
            ? body
            : ("HTTP " + std::to_string(res.code()) + ": " + (body.empty() ? std::string("Unknown error") : body));

        if (callback) callback(success, responseStr);
    });
}

void HttpClient::downloadProfileConfig(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading profile config for account {}", accountID);

    std::string url;
    std::vector<std::string> headers;

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        url = m_cdnBaseURL + "/thumbnails/profiles/config/" + std::to_string(accountID) + ".json";
        PaimonDebug::log("[HttpClient] Worker exhausted, using CDN for profile config: {}", url);
        headers = { "Accept: application/json" };
    } else {
        url = m_serverURL + "/api/profiles/config/" + std::to_string(accountID) + ".json";
        headers = { "X-API-Key: " + m_apiKey };
    }

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        callback(success, response);
    });
}

void HttpClient::downloadProfile(int accountID, std::string const& username, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading profile background for account {} (user: {})", accountID, username);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/profilebackground/" + std::to_string(accountID);
        PaimonDebug::log("[HttpClient] Worker exhausted, trying CDN for profile: {}", cdnUrl);
        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders, [callback = std::move(callback), accountID](bool success, std::vector<uint8_t> const& data) {
            if (success && !data.empty()) {
                PaimonDebug::log("[HttpClient] CDN profile download for account {}: {} bytes", accountID, data.size());
                callback(true, data, 0, 0);
            } else {
                PaimonDebug::warn("[HttpClient] CDN profile fallback failed for account {}", accountID);
                callback(false, {}, 0, 0);
            }
        });
        return;
    }

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };


    std::string url = m_serverURL + "/profilebackground/" + std::to_string(accountID);

    performBinaryRequest(url, headers, [callback = std::move(callback), accountID](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Profile downloaded for account {}: {} bytes", accountID, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No profile found for account {}", accountID);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::batchCheckProfiles(std::vector<int> const& accountIDs, GenericCallback callback) {
    if (accountIDs.empty()) {
        callback(false, "");
        return;
    }

    auto arr = matjson::Value::array();
    for (int id : accountIDs) {
        arr.push(id);
    }
    matjson::Value body;
    body["accountIDs"] = arr;

    std::string url = m_serverURL + "/profilebackground/batch-check";
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json"
    };

    PaimonDebug::log("[HttpClient] Batch check profiles: {} accounts", accountIDs.size());
    performRequest(url, "POST", body.dump(), headers, std::move(callback));
}

void HttpClient::uploadLevelMedia(int levelId, std::vector<uint8_t> const& data, std::string const& username,
        UploadCallback callback, std::string const& levelMeta, LevelMediaSpec const& spec) {
    PaimonDebug::log("[HttpClient] Uploading {} for level {}, size: {} bytes", spec.kindLabel, levelId, data.size());

    std::string url = m_serverURL + spec.endpoint;
    std::string filename = std::to_string(levelId) + "." + spec.ext;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", spec.pathField},
        {"levelId", std::to_string(levelId)},
    };
    auto tail = accountFieldTail(username);
    formFields.insert(formFields.end(), tail.begin(), tail.end());
    if (!levelMeta.empty()) formFields.push_back({"levelMeta", levelMeta});

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    // x-mod-code sends moderator uploads directly; others use the pending queue.
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
    }

    performUpload(url, "image", filename, data, formFields, headers,
        [callback = std::move(callback), levelId, spec](bool success, std::string const& response) {
            if (success) {
                if (spec.logSuccess) {
                    PaimonDebug::log("[HttpClient] Upload successful for level {}", levelId);
                }
                callback(true, extractServerMessage(response, "Upload successful"));
            } else {
                log::error("[HttpClient] {} failed for level {}: {}", spec.failLogLabel, levelId, response);
                callback(false, std::string(spec.failPrefix) + response);
            }
        },
        spec.contentType
    );
}

void HttpClient::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    static constexpr LevelMediaSpec kSpec = {"/mod/upload", "png", "image/png", "/thumbnails", "thumbnail as PNG", "Upload", "Upload failed: ", true};
    uploadLevelMedia(levelId, pngData, username, std::move(callback), levelMeta, kSpec);
}

void HttpClient::uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    static constexpr LevelMediaSpec kSpec = {"/mod/upload-gif", "gif", "image/gif", "/thumbnails", "GIF", "GIF upload", "GIF Upload failed: ", false};
    uploadLevelMedia(levelId, gifData, username, std::move(callback), levelMeta, kSpec);
}

void HttpClient::uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    static constexpr LevelMediaSpec kSpec = {"/mod/upload-video", "mp4", "video/mp4", "/thumbnails/video", "video", "Video upload", "Video Upload failed: ", false};
    uploadLevelMedia(levelId, mp4Data, username, std::move(callback), levelMeta, kSpec);
}

void HttpClient::getThumbnails(int levelId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/thumbnails/list?levelId=" + std::to_string(levelId);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        callback(success, response);
    }, false);
}

void HttpClient::getThumbnailInfo(int levelId, GenericCallback callback) {
     std::string url = m_serverURL + "/api/thumbnails/info?levelId=" + std::to_string(levelId);
     performRequest(url, "GET", "", {}, callback, false);
}

void HttpClient::uploadLevelExtra(int levelId, std::vector<uint8_t> const& pngData, std::string const& username,
        UploadCallback callback, std::string const& levelMeta, char const* kind, char const* kindLabel) {
    PaimonDebug::log("[HttpClient] Uploading {} for level {}, size: {} bytes", kind, levelId, pngData.size());

    std::string url = m_serverURL + "/api/" + kind + "s/upload";
    std::string filename = std::to_string(levelId) + ".webp";

    int accountID = getSafeAccountID();

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", std::string("/") + kind + "s"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    if (!levelMeta.empty()) formFields.push_back({"levelMeta", levelMeta});

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(url, "image", filename, pngData, formFields, headers,
        [callback = std::move(callback), levelId, kindLabel = std::string(kindLabel)](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] {} upload successful for level {}", kindLabel, levelId);
                callback(true, fmt::format("{} uploaded successfully", kindLabel));
            } else {
                log::error("[HttpClient] {} upload failed for level {}: {}", kindLabel, levelId, response);
                callback(false, fmt::format("{} upload failed: {}", kindLabel, response));
            }
        },
        "image/png"
    );
}

void HttpClient::uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    uploadLevelExtra(levelId, pngData, username, std::move(callback), levelMeta, "suggestion", "Suggestion");
}

void HttpClient::uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    uploadLevelExtra(levelId, pngData, username, std::move(callback), levelMeta, "update", "Update");
}

void HttpClient::downloadLevelExtra(int levelId, DownloadCallback callback, char const* kind, char const* kindLabel) {
    PaimonDebug::log("[HttpClient] Downloading {} for level {}", kind, levelId);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/" + kind + "s/" + std::to_string(levelId) + ".webp";
        PaimonDebug::log("[HttpClient] Worker exhausted, using CDN for {}: {}", kind, cdnUrl);
        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders, [callback = std::move(callback)](bool success, std::vector<uint8_t> const& data) {
            if (success && !data.empty()) {
                callback(true, data, 0, 0);
            } else {
                callback(false, {}, 0, 0);
            }
        });
        return;
    }

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    std::string url = m_serverURL + "/" + kind + "s/" + std::to_string(levelId) + ".webp";

    performBinaryRequest(url, headers, [callback = std::move(callback), levelId, kindLabel = std::string(kindLabel), kind = std::string(kind)](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] {} downloaded for level {}: {} bytes", kindLabel, levelId, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No {} found for level {}", kind, levelId);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadSuggestion(int levelId, DownloadCallback callback) {
    downloadLevelExtra(levelId, std::move(callback), "suggestion", "Suggestion");
}

void HttpClient::downloadUpdate(int levelId, DownloadCallback callback) {
    downloadLevelExtra(levelId, std::move(callback), "update", "Update");
}


void HttpClient::fetchManifest(std::vector<int> const& levelIds, std::function<void(bool)> callback) {
    if (levelIds.empty()) {
        if (callback) callback(false);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_manifestFetchMutex);
        if (isManifestCooldownActive()) {
            PaimonDebug::log("[HttpClient] fetchManifest rejected: cooldown active");
            if (callback) callback(false);
            return;
        }
        if (m_manifestFetchInFlight) {
            m_manifestPendingCallbacks.push_back(std::move(callback));
            PaimonDebug::log("[HttpClient] fetchManifest coalesced: {} pending callbacks",
                m_manifestPendingCallbacks.size());
            return;
        }
        m_manifestFetchInFlight = true;
        if (callback) m_manifestPendingCallbacks.push_back(std::move(callback));
    }

    std::string ids;
    for (size_t i = 0; i < levelIds.size(); i++) {
        if (i > 0) ids += ",";
        ids += std::to_string(levelIds[i]);
    }

    std::string url = m_serverURL + "/api/manifest?ids=" + ids;
    PaimonDebug::log("[HttpClient] fetchManifest for {} levels: {}", levelIds.size(), url);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };

    performRequest(url, "GET", "", headers, [this](bool success, std::string const& response) {
        std::vector<std::function<void(bool)>> callbacks;
        bool hadRateLimit = false;
        int retryAfter = 0;

        {
            std::lock_guard<std::mutex> lock(m_manifestFetchMutex);
            callbacks = std::move(m_manifestPendingCallbacks);
            m_manifestFetchInFlight = false;

            if (!success && response.find("429") != std::string::npos) {
                hadRateLimit = true;
                auto pos = response.find("\"retryAfter\":");
                if (pos != std::string::npos) {
                    auto numStart = pos + 13;
                    auto numEnd = response.find_first_not_of("0123456789", numStart);
                    if (numEnd != numStart) {
                        auto parsed = geode::utils::numFromString<int>(response.substr(numStart, numEnd - numStart));
                        if (parsed.isOk()) retryAfter = parsed.unwrap();
                    }
                }
            }
        }

        if (paimon::isRuntimeShuttingDown()) {
            for (auto& cb : callbacks) {
                if (cb) cb(false);
            }
            return;
        }

        if (hadRateLimit) {
            setManifestCooldown(retryAfter);
        }

        if (success) {
            auto changedIds = updateManifestFromJson(response);
            saveManifestToDisk();
            PaimonDebug::log("[HttpClient] Manifest fetched and cached successfully");

            if (!changedIds.empty()) {
                PaimonDebug::log("[HttpClient] Manifest revision changed for {} levels (no auto-invalidation)", changedIds.size());
            }

            for (auto& cb : callbacks) {
                if (cb) cb(true);
            }
        } else {
            PaimonDebug::warn("[HttpClient] Failed to fetch manifest: {}", response);
            for (auto& cb : callbacks) {
                if (cb) cb(false);
            }
        }
    }, false);
}

bool HttpClient::parseManifestEntry(std::string const& key, matjson::Value& val,
        int& levelIdOut, ManifestEntry& entryOut, bool stampNow) {
    if (!val.isObject()) return false;

    auto parsed = geode::utils::numFromString<int>(key);
    if (!parsed.isOk()) return false;
    int levelId = parsed.unwrap();
    if (levelId <= 0) return false;

    entryOut.format        = val["format"].asString().unwrapOr("");
    entryOut.cdnUrl        = val["cdnUrl"].asString().unwrapOr("");
    entryOut.version       = val["version"].asString().unwrapOr("");
    entryOut.id            = val["id"].asString().unwrapOr("");
    entryOut.revisionToken = val["revisionToken"].asString().unwrapOr("");
    if (stampNow) {
        entryOut.cachedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    } else {
        entryOut.cachedAt = val["cachedAt"].asInt().unwrapOr(0);
    }

    if (entryOut.cdnUrl.empty()) return false;
    if (!isUrlSafe(entryOut.cdnUrl)) return false;
    levelIdOut = levelId;
    return true;
}

std::vector<int> HttpClient::updateManifestFromJson(std::string const& json) {
    std::vector<int> changedIds;
    auto parseResult = matjson::parse(json);
    if (!parseResult.isOk()) {
        PaimonDebug::warn("[HttpClient] Manifest JSON parse error");
        return changedIds;
    }
    auto& root = parseResult.unwrap();
    if (!root.isObject()) {
        PaimonDebug::warn("[HttpClient] Manifest root is not an object");
        return changedIds;
    }

    std::lock_guard<std::mutex> lock(m_manifestMutex);
    int count = 0;

    if (root.contains("_cdnBaseUrl")) {
        auto keyVal = root["_cdnBaseUrl"].asString();
        if (keyVal.isOk() && !keyVal.unwrap().empty() && isUrlSafe(keyVal.unwrap())) {
            m_cdnBaseURL = keyVal.unwrap();
            PaimonDebug::log("[HttpClient] Got CDN base URL from manifest: {}", m_cdnBaseURL);
        }
    }

    for (auto& [key, val] : root) {
        ManifestEntry entry;
        int levelId = 0;
        if (!parseManifestEntry(key, val, levelId, entry, true)) continue;

        if (!entry.revisionToken.empty()) {
            auto it = m_manifestCache.find(levelId);
            if (it != m_manifestCache.end() && !it->second.revisionToken.empty()
                && it->second.revisionToken != entry.revisionToken) {
                changedIds.push_back(levelId);
            }
        }
        m_manifestCache[levelId] = std::move(entry);
        count++;
    }

    PaimonDebug::log("[HttpClient] Manifest updated: {} entries cached", count);

    while (m_manifestCache.size() > MAX_MANIFEST_ENTRIES) {
        m_manifestCache.erase(m_manifestCache.begin());
    }

    return changedIds;
}

std::optional<HttpClient::ManifestEntry> HttpClient::getManifestEntry(int levelId) {
    std::lock_guard<std::mutex> lock(m_manifestMutex);
    auto it = m_manifestCache.find(levelId);
    if (it != m_manifestCache.end()) {
        if (it->second.cachedAt > 0) {
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (now - it->second.cachedAt > MANIFEST_ENTRY_TTL) {
                PaimonDebug::log("[HttpClient] Manifest entry expired for level {} (age={}s)",
                    levelId, now - it->second.cachedAt);
                m_manifestCache.erase(it);
                return std::nullopt;
            }
        }
        return it->second;
    }
    return std::nullopt;
}

void HttpClient::removeManifestEntry(int levelId) {
    {
        std::lock_guard<std::mutex> lock(m_manifestMutex);
        m_manifestCache.erase(levelId);
    }
    saveManifestToDisk();
}

void HttpClient::removeExistsEntry(int levelId) {
    std::lock_guard<std::mutex> lock(m_existsCacheMutex);
    m_existsCache.erase(levelId);
}

void HttpClient::saveManifestToDisk() {
    if (!paimon::settings::general::enableDiskCache()) return;

    // serialize under the lock; disk i/o stays off the main thread.
    std::string json;
    size_t entryCount = 0;
    auto path = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "manifest_cache.json";

    {
        std::lock_guard<std::mutex> lock(m_manifestMutex);
        if (m_manifestCache.empty()) return;

        matjson::Value root = matjson::Value::object();
        if (!m_cdnBaseURL.empty()) {
            root["_cdnBaseUrl"] = m_cdnBaseURL;
        }
        for (auto& [levelId, entry] : m_manifestCache) {
            matjson::Value obj = matjson::Value::object();
            obj["format"]   = entry.format;
            obj["cdnUrl"]   = entry.cdnUrl;
            obj["version"]  = entry.version;
            obj["id"]       = entry.id;
            obj["cachedAt"] = entry.cachedAt;
            if (!entry.revisionToken.empty()) {
                obj["revisionToken"] = entry.revisionToken;
            }
            root[std::to_string(levelId)] = std::move(obj);
        }

        json = root.dump(matjson::NO_INDENTATION);
        entryCount = m_manifestCache.size();
    }

    paimon::ThreadTracker::get().spawn([path, json = std::move(json), entryCount]() {
        geode::utils::thread::setName("PaimonManifestSave");
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        auto writeRes = geode::utils::file::writeStringSafe(paimon::paths::toSdkPath(path), json);
        if (writeRes.isErr()) {
            PaimonDebug::warn("[HttpClient] Failed to write manifest_cache.json: {}", writeRes.unwrapErr());
            return;
        }
        PaimonDebug::log("[HttpClient] Manifest saved to disk ({} entries)", entryCount);
    });
}

void HttpClient::loadManifestFromDisk() {
    if (!paimon::settings::general::enableDiskCache()) return;
    auto path = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "manifest_cache.json";

    auto readRes = geode::utils::file::readString(paimon::paths::toSdkPath(path));
    if (readRes.isErr()) return;

    std::string json = readRes.unwrap();
    if (json.empty()) return;

    auto parseResult = matjson::parse(json);
    if (!parseResult.isOk()) {
        PaimonDebug::warn("[HttpClient] manifest_cache.json parse error");
        return;
    }
    auto& root = parseResult.unwrap();
    if (!root.isObject()) return;

    std::lock_guard<std::mutex> lock(m_manifestMutex);
    int count = 0;

    if (root.contains("_cdnBaseUrl")) {
        auto keyVal = root["_cdnBaseUrl"].asString();
        if (keyVal.isOk() && !keyVal.unwrap().empty() && isUrlSafe(keyVal.unwrap())) {
            m_cdnBaseURL = keyVal.unwrap();
        }
    }

    for (auto& [key, val] : root) {
        ManifestEntry entry;
        int levelId = 0;
        if (!parseManifestEntry(key, val, levelId, entry, false)) continue;

        m_manifestCache[levelId] = std::move(entry);
        count++;
    }

    PaimonDebug::log("[HttpClient] Manifest loaded from disk: {} entries", count);

    while (m_manifestCache.size() > MAX_MANIFEST_ENTRIES) {
        m_manifestCache.erase(m_manifestCache.begin());
    }
}

void HttpClient::downloadReported(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading reported thumbnail for level {}", levelId);
    downloadThumbnail(levelId, callback);
}

void HttpClient::fetchViaWorker(int levelId, bool dropManifestEntry) {
    if (isWorkerExhausted()) {
        // worker exhaustion is transient; let thumbnailloader retry.
        PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fallback for level {} (will retry later)", levelId);
        resolveInflight(levelId, false, {});
        return;
    }

    auto headers = std::vector<std::string>{
        "X-API-Key: " + m_apiKey,
        "Connection: keep-alive"
    };
    std::string url = m_serverURL + "/t/" + std::to_string(levelId);

    performBinaryRequestEx(url, headers, [this, levelId, dropManifestEntry](bool ws, std::vector<uint8_t> const& wd, int status) {
        if (ws && !wd.empty()) {
            PaimonDebug::log("[HttpClient] Worker fallback success for level {}: {} bytes", levelId, wd.size());
            resolveInflight(levelId, true, wd);
        } else if (isMissingStatus(status)) {
            PaimonDebug::warn("[HttpClient] Level {} has no thumbnail (HTTP {})", levelId, status);
            markThumbnailNotFound(levelId);
            if (dropManifestEntry) removeManifestEntry(levelId);
            resolveInflight(levelId, false, {});
        } else {
            // timeout/5xx/429/net-cut is not "missing": negative caching
            // blanked the level and dropped the good manifest entry.
            PaimonDebug::warn("[HttpClient] Transient failure for level {} (HTTP {}), not caching as missing", levelId, status);
            resolveInflight(levelId, false, {});
        }
    });
}

void HttpClient::fetchViaCdn(int levelId, std::string const& cdnUrl, bool dropManifestEntry, bool bestEffort) {
    if (!isUrlSafe(cdnUrl)) {
        PaimonDebug::warn("[HttpClient] Refusing unsafe CDN URL for level {}", levelId);
        resolveInflight(levelId, false, {});
        return;
    }
    std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
    performBinaryRequest(cdnUrl, cdnHeaders,
        [this, levelId, dropManifestEntry, bestEffort](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            if (bestEffort) PaimonDebug::log("[HttpClient] CDN best-effort success for level {}: {} bytes", levelId, data.size());
            else PaimonDebug::log("[HttpClient] CDN download success for level {}: {} bytes", levelId, data.size());
            resolveInflight(levelId, true, data);
        } else {
            // cdn miss: fall back to the worker without invalidating the manifest.
            if (bestEffort) PaimonDebug::warn("[HttpClient] CDN best-effort failed for level {} (may not exist), falling back to Worker", levelId);
            else PaimonDebug::warn("[HttpClient] CDN download failed for level {}, falling back to Worker", levelId);
            fetchViaWorker(levelId, dropManifestEntry);
        }
    }, 4 /* cdn timeout; fall back quickly if slow */);
}

void HttpClient::downloadThumbnail(int levelId, bool, DownloadCallback callback) {
    downloadThumbnail(levelId, callback);
}

void HttpClient::downloadThumbnail(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] downloadThumbnail for level {} (single format, no extension)", levelId);

    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        auto it = m_inflightDownloads.find(levelId);
        if (it != m_inflightDownloads.end()) {
            PaimonDebug::log("[HttpClient] download already in-flight for level {}, coalescing callback", levelId);
            it->second.push_back(std::move(callback));
            return;
        }
        m_inflightDownloads[levelId].push_back(std::move(callback));
    }

    auto manifestEntry = getManifestEntry(levelId);
    if (manifestEntry.has_value() && !manifestEntry->cdnUrl.empty()) {
        std::string cdnUrl = manifestEntry->cdnUrl;
        if (!manifestEntry->version.empty()) {
            cdnUrl += (cdnUrl.find('?') != std::string::npos ? "&" : "?");
            cdnUrl += "_pv=" + manifestEntry->version;
        }
        PaimonDebug::log("[HttpClient] Manifest hit for level {}: CDN URL={}", levelId, cdnUrl);

        fetchViaCdn(levelId, cdnUrl, true, false);
        return;
    }

    // on a cold manifest, try cdn before the worker.
    if (!m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/thumbnails/" + std::to_string(levelId) + ".webp";
        PaimonDebug::log("[HttpClient] Manifest miss for level {}, trying CDN best-effort first: {}", levelId, cdnUrl);

        fetchViaCdn(levelId, cdnUrl, false, true);
        return;
    }

    PaimonDebug::log("[HttpClient] Manifest miss for level {}, using Worker fallback", levelId);

    auto headers = std::vector<std::string>{
        "X-API-Key: " + m_apiKey,
        "Connection: keep-alive"
    };

    std::string url = m_serverURL + "/t/" + std::to_string(levelId);

    performBinaryRequestEx(url, headers, [this, levelId](bool success, std::vector<uint8_t> const& data, int status) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Found thumbnail for level {}", levelId);
            resolveInflight(levelId, true, data);
        } else if (isMissingStatus(status)) {
            PaimonDebug::warn("[HttpClient] Level {} has no thumbnail (HTTP {})", levelId, status);
            markThumbnailNotFound(levelId);
            resolveInflight(levelId, false, {});
        } else {
            PaimonDebug::warn("[HttpClient] Transient failure for level {} (HTTP {}), not caching as missing", levelId, status);
            resolveInflight(levelId, false, {});
        }
    });
}

bool HttpClient::isWorkerExhausted() {
    if (!m_workerExhausted.load(std::memory_order_acquire)) return false;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - m_exhaustedAt > EXHAUSTED_RECOVERY_SECONDS) {
        m_workerExhausted.store(false, std::memory_order_release);
        m_consecutiveWorkerFailures.store(0, std::memory_order_release);
        PaimonDebug::log("[HttpClient] Worker exhaustion reset after {}s recovery period", EXHAUSTED_RECOVERY_SECONDS);
        return false;
    }
    return true;
}

void HttpClient::markWorkerExhausted() {
    if (m_workerExhausted.load(std::memory_order_acquire)) return;
    m_workerExhausted.store(true, std::memory_order_release);
    m_exhaustedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    PaimonDebug::warn("[HttpClient] Worker quota exhausted! Falling back to CDN for reads");
}

bool HttpClient::isManifestCooldownActive() const {
    return std::chrono::steady_clock::now() < m_manifestCooldownUntil;
}

void HttpClient::setManifestCooldown(int retryAfterSeconds) {
    int backoff = std::max(retryAfterSeconds, MANIFEST_COOLDOWN_SECONDS);
    m_manifestCooldownUntil = std::chrono::steady_clock::now() + std::chrono::seconds(backoff);
    PaimonDebug::warn("[HttpClient] Manifest fetch cooldown: {}s (server retryAfter={})", backoff, retryAfterSeconds);
}

bool HttpClient::isThumbnailNotFound(int levelId) const {
    std::lock_guard lock(m_notFoundMutex);
    auto it = m_notFoundCache.find(levelId);
    if (it == m_notFoundCache.end()) return false;
    if (std::chrono::steady_clock::now() - it->second >= std::chrono::seconds(NOT_FOUND_TTL_SECONDS)) {
        m_notFoundCache.erase(it);
        return false;
    }
    return true;
}

void HttpClient::markThumbnailNotFound(int levelId) const {
    std::lock_guard lock(m_notFoundMutex);
    m_notFoundCache[levelId] = std::chrono::steady_clock::now();
}

void HttpClient::clearThumbnailNotFound(int levelId) {
    std::lock_guard lock(m_notFoundMutex);
    m_notFoundCache.erase(levelId);
}

void HttpClient::resolveInflight(int levelId, bool success, std::vector<uint8_t> const& data) {
    std::vector<DownloadCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        auto it = m_inflightDownloads.find(levelId);
        if (it != m_inflightDownloads.end()) {
            callbacks = std::move(it->second);
            m_inflightDownloads.erase(it);
        }
    }
    PaimonDebug::log("[HttpClient] resolveInflight level {}: success={}, {} callbacks", levelId, success, callbacks.size());
    if (success) clearThumbnailNotFound(levelId);
    for (auto& cb : callbacks) {
        cb(success, data, 0, 0);
    }
}

void HttpClient::checkThumbnailExists(int levelId, CheckCallback callback) {
    auto manifestEntry = getManifestEntry(levelId);
    if (manifestEntry.has_value() && !manifestEntry->cdnUrl.empty()) {
        PaimonDebug::log("[HttpClient] checkExists: manifest hit for level {} - skipping network", levelId);
        callback(true);
        return;
    }

    time_t now = std::time(nullptr);
    {
        std::lock_guard<std::mutex> lock(m_existsCacheMutex);
        auto cacheIt = m_existsCache.find(levelId);
        if (cacheIt != m_existsCache.end()) {
            if (now - cacheIt->second.timestamp < EXISTS_CACHE_DURATION) {
                callback(cacheIt->second.exists);
                return;
            } else {
                m_existsCache.erase(cacheIt);
            }
        }
    }

    if (isWorkerExhausted()) {
        PaimonDebug::warn("[HttpClient] Worker exhausted, skipping exists check for level {}", levelId);
        callback(false);
        return;
    }

    std::string url = m_serverURL + "/api/exists?levelId=" + std::to_string(levelId) + "&path=thumbnails";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };

    performRequest(url, "GET", "", headers, [this, callback, levelId, now](bool success, std::string const& response) {
        if (paimon::isRuntimeShuttingDown()) {
            callback(false);
            return;
        }
        if (success) {
            bool exists = response.find("\"exists\":true") != std::string::npos ||
                          response.find("\"exists\": true") != std::string::npos;
            {
                std::lock_guard<std::mutex> lock(m_existsCacheMutex);
                m_existsCache[levelId] = {exists, now};
            }
            PaimonDebug::log("[HttpClient] Thumbnail exists check for level {}: {} (cached)", levelId, exists);
            callback(exists);
        } else {
            PaimonDebug::warn("[HttpClient] Failed to check thumbnail exists for level {}", levelId);
            callback(false);
        }
    }, false);
}

void HttpClient::reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Reordering thumbnails for level {} ({} ids)", levelId, thumbnailIds.size());

    if (levelId <= 0 || thumbnailIds.size() < 2) {
        callback(false, "Invalid thumbnail reorder payload");
        return;
    }

    std::string username = getSafeAccountUsername();
    int accountID = getSafeAccountID();
    if (username.empty() || accountID <= 0) {
        callback(false, "Debes estar logueado para reordenar miniaturas.");
        return;
    }

    matjson::Value idArray = matjson::Value::array();
    for (auto const& thumbnailId : thumbnailIds) {
        idArray.push(thumbnailId);
    }

    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"username", username},
        {"accountID", accountID},
        {"thumbnailIds", idArray}
    });

    std::string endpoint = "/api/thumbnails/reorder/" + std::to_string(levelId);
    postWithAuth(endpoint, json.dump(), std::move(callback));
}

void HttpClient::checkModerator(std::string const& username, ModeratorCallback callback) {
    checkModeratorAccount(username, 0, callback);
}

void HttpClient::checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback) {
    auto* account = GJAccountManager::get();
    auto* game = GameManager::get();
    if (!account || !game || accountID <= 0 || account->m_accountID != accountID ||
        geode::utils::string::toLower(game->m_playerName) != geode::utils::string::toLower(username)) {
        callback(false, false);
        return;
    }

    auto const sessionToken = activeSessionToken();
    auto const modCode = activeModCode();
    auto const credential = !sessionToken.empty() ? sessionToken : modCode;
    std::string key = username + "#" + std::to_string(accountID) + "#" + credential;
    {
        std::lock_guard<std::mutex> lock(m_inflightModMutex);
        auto it = m_inflightModChecks.find(key);
        if (it != m_inflightModChecks.end()) {
            it->second.push_back(std::move(callback));
            return;
        }
        m_inflightModChecks[key].push_back(std::move(callback));
    }

    auto endpoint = "/api/moderator/check?username=" + encodeQueryParam(username)
        + "&accountID=" + std::to_string(accountID);
    // retry on transport/timeout/5xx with backoff; a replaced callback gate
    // or shutdown ends the chain silently.
    auto sessionGate = m_callbackGate;
    auto attempts = std::make_shared<int>(0);
    auto tryOnce = std::make_shared<std::function<void()>>();
    *tryOnce = [this, endpoint, key, username, accountID, credential, sessionGate, attempts, tryOnce]() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto currentGate = m_callbackGate;
        if (!sessionGate || !currentGate || currentGate != sessionGate ||
            !currentGate->load(std::memory_order_acquire)) {
            return;
        }
        get(endpoint, [this, key, username, accountID, credential, sessionGate, attempts, tryOnce](bool success, std::string const& response) {
            if (!success && *attempts < kVerdictMaxAttempts - 1 && !isRateLimitFailure(response) &&
                !paimon::isRuntimeShuttingDown()) {
                float delay = kModCheckRetryDelays[*attempts];
                (*attempts)++;
                PaimonDebug::log("[HttpClient] checkModeratorAccount retry {}/{} in {:.1f}s",
                    *attempts + 1, kVerdictMaxAttempts, delay);
                paimon::scheduleMainThreadDelay(delay, [tryOnce]() { (*tryOnce)(); });
                return;
            }
            bool isMod = false;
            bool isAdmin = false;
            auto parsed = matjson::parse(response);
            auto* account = GJAccountManager::get();
            auto* game = GameManager::get();
            auto const liveSession = activeSessionToken();
            auto const liveCred = !liveSession.empty() ? liveSession : activeModCode();
            bool sameAccount = account && game && account->m_accountID == accountID &&
                geode::utils::string::toLower(game->m_playerName) == geode::utils::string::toLower(username) &&
                liveCred == credential;
            if (!paimon::isRuntimeShuttingDown() && sameAccount) {
                paimon::modauth::SessionRoles roles;
                if (success && parsed.isOk() && parsed.unwrap().isObject()) {
                    auto const& json = parsed.unwrap();
                    if (!credential.empty() && json["authenticated"].asBool().unwrapOr(false) &&
                        json["accountID"].asInt().unwrapOr(0) == accountID) {
                        roles.authenticated = true;
                        roles.isAdmin = json["isAdmin"].asBool().unwrapOr(false);
                        roles.isModerator = roles.isAdmin || json["isModerator"].asBool().unwrapOr(false);
                        roles.isVip = roles.isModerator || json["isVip"].asBool().unwrapOr(false);
                        roles.isHelper = json["isHelper"].asBool().unwrapOr(false);
                        roles.isIdea = json["isIdea"].asBool().unwrapOr(false);
                        roles.primaryRole = json["primaryRole"].asString().unwrapOr(
                            roles.isAdmin ? "admin" : roles.isModerator ? "moderator" :
                            roles.isVip ? "vip" : roles.isHelper ? "helper" : roles.isIdea ? "idea" : "user");
                        if (json.contains("permissions") && json["permissions"].isArray()) {
                            if (auto arr = json["permissions"].asArray(); arr.isOk()) {
                                for (auto const& item : arr.unwrap()) {
                                    if (auto p = item.asString(); p.isOk() && !p.unwrap().empty()) {
                                        roles.permissions.push_back(p.unwrap());
                                    }
                                }
                            }
                        }
                        isAdmin = roles.isAdmin;
                        isMod = roles.isModerator;
                    }
                }
                paimon::modauth::setVerifiedSession(username, accountID, roles);
                // persisted vip flag mirrors the volatile verdict as a ui hint only.
                Mod::get()->setSavedValue<bool>("is-verified-vip", paimon::modauth::isVerifiedVip());
            }
            resolveModCheckInflight(key, isMod, isAdmin);
        });
    };
    (*tryOnce)();
}

void HttpClient::resolveModCheckInflight(std::string const& key, bool isMod, bool isAdmin) {
    std::vector<ModeratorCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_inflightModMutex);
        auto it = m_inflightModChecks.find(key);
        if (it != m_inflightModChecks.end()) {
            callbacks = std::move(it->second);
            m_inflightModChecks.erase(it);
        }
    }

    for (auto& cb : callbacks) {
        cb(isMod, isAdmin);
    }
}

void HttpClient::getBanList(BanListCallback callback) {
    PaimonDebug::log("[HttpClient] Getting ban list");

    // admin reads need the volatile, freshly verified moderator session;
    // raw gd fields are not proof of a role.
    if (!paimon::modauth::isVerified()) {
        if (callback) callback(false, "Verifica tu cuenta de moderador antes de continuar.");
        return;
    }
    std::string const reqUser = paimon::modauth::verifiedUsername();
    int const reqAccountID = paimon::modauth::verifiedAccountID();
    auto const sessionToken = activeSessionToken();
    auto const modCode = activeModCode();
    if (!paimon::text::isSafeAccountName(reqUser) || reqAccountID <= 0 ||
        reqAccountID != getSafeAccountID() || (sessionToken.empty() && modCode.empty())) {
        if (callback) callback(false, "La cuenta cambio. Verifica la sesion antes de continuar.");
        return;
    }

    // url-encoded to prevent parameter injection.
    std::string url = m_serverURL + "/api/admin/banlist?username=" + encodeQueryParam(reqUser)
        + "&accountID=" + std::to_string(reqAccountID);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    if (!sessionToken.empty()) headers.push_back("X-Session-Token: " + sessionToken);
    if (!modCode.empty()) headers.push_back("X-Mod-Code: " + modCode);
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::checkBanned(BanCheckCallback callback) {
    // independent of cleanTasks: cached bans still need to learn about an
    // official unban. the fixed https endpoint never follows redirects.
    std::string const reqUser = getSafeAccountUsername();
    int const reqAccountID = getSafeAccountID();
    if (reqUser.empty() || reqAccountID <= 0 || !paimon::text::isSafeAccountName(reqUser)) {
        if (callback) callback(false, false, "");
        return;
    }
    auto attempts = std::make_shared<int>(0);
    auto sharedCb = std::make_shared<BanCheckCallback>(std::move(callback));
    auto tryOnce = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> const weakRetry = tryOnce;
    *tryOnce = [this, reqUser, reqAccountID, attempts, sharedCb, weakRetry]() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto retry = weakRetry.lock();
        if (!retry) return;
        if (getSafeAccountID() != reqAccountID || getSafeAccountUsername() != reqUser) {
            if (*sharedCb) (*sharedCb)(false, false, "");
            return;
        }

        // unpredictable per attempt; bound to the response when the server
        // echoes it as check/nonce/_check.
        static std::atomic<std::uint64_t> nonceCounter{0};
        std::random_device random;
        std::uint64_t const nonceValue =
            (static_cast<std::uint64_t>(random()) << 32) ^ random() ^
            static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now()
                .time_since_epoch().count()) ^
            (nonceCounter.fetch_add(1, std::memory_order_relaxed) * 0x9e3779b97f4a7c15ULL);
        std::string const nonce = std::to_string(nonceValue);

        std::string const url = std::string(paimon::ban::kVerificationEndpoint) +
            "username=" + encodeQueryParam(reqUser) + "&accountID=" + std::to_string(reqAccountID) +
            "&_check=" + nonce;
        auto req = web::WebRequest();
        req.timeout(asp::Duration::fromSecs(10));
        req.followRedirects(false);
        req.header("X-API-Key", m_apiKey);
        req.header("Accept", "application/json");
        req.header("Cache-Control", "no-cache, no-store");
        req.header("Pragma", "no-cache");
        // extra ownership proof; empty tokens are omitted.
        if (auto const st = activeSessionToken(); !st.empty()) req.header("X-Session-Token", st);
        if (!m_viewerToken.empty()) req.header("X-Viewer-Token", m_viewerToken);
        WebHelper::dispatch(std::move(req), "GET", url,
            [reqUser, reqAccountID, nonce, attempts, sharedCb, retry](web::WebResponse res) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (getSafeAccountID() != reqAccountID || getSafeAccountUsername() != reqUser) {
                if (*sharedCb) (*sharedCb)(false, false, "");
                return;
            }
            auto const verdict = res.ok()
                ? paimon::ban::parseBanVerdict(res.string().unwrapOr(""), reqAccountID, nonce)
                : std::nullopt;
            if (verdict) {
                if (*sharedCb) (*sharedCb)(true, verdict->banned, verdict->reason);
                return;
            }
            bool const retryable = res.code() <= 0 || res.code() >= 500 || res.ok();
            if (retryable && *attempts < kVerdictMaxAttempts - 1) {
                float const delay = kBanCheckRetryDelays[(*attempts)++];
                paimon::scheduleMainThreadDelay(delay, [retry]() { (*retry)(); });
                return;
            }
            if (*sharedCb) (*sharedCb)(false, false, "");
        });
    };
    (*tryOnce)();
}

void HttpClient::banUser(std::string const& username, std::string const& reason, BanUserCallback callback) {
    // every moderation mutation needs a fresh volatile verification; a
    // persisted flag or raw gd username must never authorize a ban.
    if (!paimon::modauth::isVerified()) {
        if (callback) callback(false, "Verifica tu cuenta de moderador antes de continuar.");
        return;
    }
    std::string const adminUser = paimon::modauth::verifiedUsername();
    int const accountID = paimon::modauth::verifiedAccountID();
    if (!paimon::text::isSafeAccountName(username) || !paimon::text::isSafeAccountName(adminUser) ||
        accountID <= 0 || accountID != getSafeAccountID() || (activeSessionToken().empty() && activeModCode().empty())) {
        if (callback) callback(false, "La cuenta cambio. Verifica la sesion antes de continuar.");
        return;
    }
    if (paimon::ban::sameAccountName(username, adminUser)) {
        if (callback) callback(false, "No puedes banearte a ti mismo.");
        return;
    }
    std::string const cleanReason = paimon::text::sanitizeDisplayText(reason, 200);
    if (cleanReason.empty()) {
        if (callback) callback(false, "Ingresa una razon valida.");
        return;
    }

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"reason", cleanReason},
        {"admin", adminUser},
        {"adminUser", adminUser},
        {"accountID", accountID}
    });

    // postWithAuth re-checks the actor against the current gd session and
    // adds X-Mod-Code only when authorized.
    postWithAuth("/api/admin/ban", json.dump(), [callback = std::move(callback)](bool success, std::string const& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::unbanUser(std::string const& username, BanUserCallback callback) {
    if (!paimon::modauth::isVerified()) {
        if (callback) callback(false, "Verifica tu cuenta de moderador antes de continuar.");
        return;
    }
    std::string const adminUser = paimon::modauth::verifiedUsername();
    int const accountID = paimon::modauth::verifiedAccountID();
    if (!paimon::text::isSafeAccountName(username) || !paimon::text::isSafeAccountName(adminUser) ||
        accountID <= 0 || accountID != getSafeAccountID() || (activeSessionToken().empty() && activeModCode().empty())) {
        if (callback) callback(false, "La cuenta cambio. Verifica la sesion antes de continuar.");
        return;
    }
    if (paimon::ban::sameAccountName(username, adminUser)) {
        if (callback) callback(false, "No puedes desbanearte a ti mismo.");
        return;
    }

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", accountID}
    });

    postWithAuth("/api/admin/unban", json.dump(), [callback = std::move(callback)](bool success, std::string const& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::getModerators(ModeratorsListCallback callback) {
    std::string url;
    std::vector<std::string> headers;

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        url = m_cdnBaseURL + "/system/public/api/moderators.json";
        headers = { "Accept: application/json" };
    } else {
        url = m_serverURL + "/api/moderators";
        headers = { "X-API-Key: " + m_apiKey };
    }

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (!success) {
            callback(false, {});
            return;
        }
        auto res = matjson::parse(response);
        if (!res.isOk()) {
            callback(false, {});
            return;
        }
        auto json = res.unwrap();
        std::vector<std::string> moderators;
        if (json.contains("moderators") && json["moderators"].isArray()) {
            auto arrRes = json["moderators"].asArray();
            if (arrRes.isOk()) {
                for (auto const& item : arrRes.unwrap()) {
                    if (item.contains("username")) {
                         moderators.push_back(item["username"].asString().unwrapOr(""));
                    }
                }
            }
        }
        callback(true, moderators);
    }, false);
}

void HttpClient::checkUserRoles(std::string const& username, int accountID, UserRolesCallback callback) {
    std::string url = m_serverURL + "/api/moderator/check?username=" + encodeQueryParam(username);
    if (accountID > 0) url += "&accountID=" + std::to_string(accountID);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (paimon::isRuntimeShuttingDown()) { callback({}, false); return; }
        UserRoleFlags flags;
        if (!success) { callback(flags, false); return; }

        auto res = matjson::parse(response);
        if (!res.isOk()) { callback(flags, false); return; }
        auto json = res.unwrap();

        flags.isMod    = json["isModerator"].asBool().unwrapOr(false);
        flags.isAdmin  = json["isAdmin"].asBool().unwrapOr(false);
        flags.isVip    = json["isVip"].asBool().unwrapOr(false);
        flags.isHelper = json["isHelper"].asBool().unwrapOr(false);
        flags.isIdea   = json["isIdea"].asBool().unwrapOr(false);
        if (flags.isAdmin) flags.isMod = true;
        callback(flags, true);
    }, false);
}

void HttpClient::postRoleChange(char const* endpoint, std::string const& role, std::string const& username, int targetAccountID, GenericCallback callback) {
    int adminAccountID = 0;
    std::string adminUser;
    if (auto* am = GJAccountManager::get()) adminAccountID = am->m_accountID;
    if (auto* gm = GameManager::get()) adminUser = gm->m_playerName;

    matjson::Value json = matjson::makeObject({
        {"role", role},
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", adminAccountID},
        {"targetAccountID", targetAccountID}
    });
    postWithAuth(endpoint, json.dump(), std::move(callback));
}

void HttpClient::addRoleMember(std::string const& role, std::string const& username, int targetAccountID, GenericCallback callback) {
    postRoleChange("/api/admin/add-role", role, username, targetAccountID, std::move(callback));
}

void HttpClient::removeRoleMember(std::string const& role, std::string const& username, int targetAccountID, GenericCallback callback) {
    postRoleChange("/api/admin/remove-role", role, username, targetAccountID, std::move(callback));
}

void HttpClient::getRoleMembers(std::string const& role, GenericCallback callback) {
    int adminAccountID = 0;
    std::string adminUser;
    if (auto* am = GJAccountManager::get()) adminAccountID = am->m_accountID;
    if (auto* gm = GameManager::get()) adminUser = gm->m_playerName;

    std::string url = m_serverURL + "/api/admin/role-members?role=" + encodeQueryParam(role)
        + "&username=" + encodeQueryParam(adminUser)
        + "&accountID=" + std::to_string(adminAccountID);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Admin-User: " + adminUser,
        "Accept: application/json"
    };
    if (!m_modCode.empty()) headers.push_back("X-Mod-Code: " + m_modCode);

    performRequest(url, "GET", "", headers, std::move(callback), false);
}

void HttpClient::getTopList(char const* name, GenericCallback callback) {
    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/system/public/api/" + name + ".json";
        std::vector<std::string> headers = { "Accept: application/json" };
        performRequest(cdnUrl, "GET", "", headers, callback, false);
        return;
    }
    std::string url = m_serverURL + "/api/" + name + "?limit=100";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::getTopCreators(GenericCallback callback) {
    getTopList("top-creators", std::move(callback));
}

void HttpClient::getTopThumbnails(GenericCallback callback) {
    getTopList("top-thumbnails", std::move(callback));
}

void HttpClient::getUserUploads(std::string const& username, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Getting uploads for user {}", username);
    std::string url = m_serverURL + "/api/user/" + username + "/uploads";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::submitReport(int levelId, std::string const& username, std::string const& note, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Submitting report for level {} by user {}", levelId, username);
    std::string url = m_serverURL + "/api/report/submit";
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"username", username},
        {"note", note}
    });
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, callback);
}

void HttpClient::uploadCrashLog(CrashReport const& report, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading crash log {} ({} + {} bytes)",
        report.crashlogName, report.crashlog.size(), report.geodeLog.size());

    std::string url = m_serverURL + "/api/crashlogs/report";
    matjson::Value json = matjson::makeObject({
        {"username", getSafeAccountUsername()},
        {"accountID", getSafeAccountID()},
        {"modVersion", Mod::get()->getVersion().toVString()},
        {"geodeVersion", report.geodeVersion},
        {"gameVersion", report.gameVersion},
        {"platform", GEODE_PLATFORM_NAME},
        {"crashedAt", report.crashedAt},
        {"crashlogName", report.crashlogName},
        {"crashlog", report.crashlog},
        {"geodeLog", report.geodeLog}
    });
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, callback);
}

void HttpClient::getRating(int levelId, std::string const& username, std::string const& thumbnailId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/v2/ratings/" + std::to_string(levelId) + "?username=" + encodeQueryParam(username);
    if (!thumbnailId.empty()) url += "&thumbnailId=" + encodeQueryParam(thumbnailId);
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/v2/ratings/vote";
    auto account = AccountVerifier::get().verify();
    matjson::Value json = matjson::makeObject({
        {"levelID", levelId},
        {"stars", stars},
        {"username", username},
        {"accountID", account.accountID},
        {"isOfficialServer", account.isOfficialServer}
    });
    if (!thumbnailId.empty()) json["thumbnailId"] = thumbnailId;

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, callback);
}

bool HttpClient::guardUrlSafe(std::string const& url, DownloadCallback const& callback) {
    if (isUrlSafe(url)) return true;
    PaimonDebug::log("[HttpClient] Blocked unsafe URL: {}", url);
    if (callback) callback(false, {}, 0, 0);
    return false;
}

std::vector<std::string> HttpClient::urlDownloadHeaders(std::string const& url) {
    // do not send the api key to arbitrary hosts.
    std::vector<std::string> headers;
    if (isTrustedBackendUrl(url)) headers.push_back("X-API-Key: " + m_apiKey);
    return headers;
}

void HttpClient::downloadFromUrl(std::string const& url, DownloadCallback callback) {
    if (!guardUrlSafe(url, callback)) return;

    performBinaryRequest(url, urlDownloadHeaders(url), [callback = std::move(callback)](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            callback(true, data, 0, 0);
        } else {
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadFromUrlRaw(std::string const& url, DownloadCallback callback) {
    if (!guardUrlSafe(url, callback)) return;
    performBinaryRequestEx(url, urlDownloadHeaders(url),
        [callback = std::move(callback)](bool success, std::vector<uint8_t> const& data, int) {
            if (callback) callback(success && !data.empty(), data, 0, 0);
        }, 30, false, false);
}

void HttpClient::get(std::string const& endpoint, GenericCallback callback) {
    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        static const std::unordered_map<std::string, std::string> cdnEndpoints = {
            {"/api/daily/current", "/system/public/api/daily/current.json"},
            {"/api/weekly/current", "/system/public/api/weekly/current.json"},
            {"/api/moderators", "/system/public/api/moderators.json"},
            {"/api/top-creators", "/system/public/api/top-creators.json"},
            {"/api/top-thumbnails", "/system/public/api/top-thumbnails.json"},
        };

        std::string path = endpoint;
        auto qpos = path.find('?');
        if (qpos != std::string::npos) path = path.substr(0, qpos);

        auto cdnIt = cdnEndpoints.find(path);
        if (cdnIt != cdnEndpoints.end()) {
            std::string cdnUrl = m_cdnBaseURL + cdnIt->second;
            PaimonDebug::log("[HttpClient] Worker exhausted, using CDN fallback: {}", cdnUrl);
            std::vector<std::string> headers = { "Accept: application/json" };
            performRequest(cdnUrl, "GET", "", headers, callback, false);
            return;
        }
    }

    std::string url = endpoint;
    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        url = m_serverURL + endpoint;
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    if (auto const st = activeSessionToken(); !st.empty()) {
        headers.push_back("X-Session-Token: " + st);
    }
    if (auto const mc = activeModCode(); !mc.empty()) {
        headers.push_back("X-Mod-Code: " + mc);
    }
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::getProfileStats(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Fetching profile stats for account {}", accountID);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        // profile stats are private; there is no cdn fallback.
        PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fetch profile stats for account {}", accountID);
        callback(false, "Worker quota exhausted");
        return;
    }

    std::string url = m_serverURL + "/api/profile/stats/" + std::to_string(accountID);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::downloadProfileBundle(int accountID, std::string const& username, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Fetching profile bundle for account {} (username={})", accountID, username);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fetch profile bundle for account {}", accountID);
        callback(false, "Worker quota exhausted");
        return;
    }

    std::string url = m_serverURL + "/api/profile/bundle/" + std::to_string(accountID);
    if (!username.empty()) {
        url += "?username=" + encodeQueryParam(username);
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::fetchInit(std::string const& username, int accountID, std::vector<int> const& levelIds, InitCallback callback) {
    PaimonDebug::log("[HttpClient] fetchInit for user={} accountID={} levels={}", username, accountID, levelIds.size());

    if (isWorkerExhausted()) {
        PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fetch init");
        if (callback) callback(false, {});
        return;
    }

    matjson::Value body;
    body["username"] = username;
    body["accountID"] = accountID;
    auto arr = matjson::Value::array();
    for (int id : levelIds) {
        arr.push(id);
    }
    body["levelIds"] = arr;

    std::string url = m_serverURL + "/api/init";
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };

    performRequest(url, "POST", body.dump(), headers, [this, callback = std::move(callback), username = std::string(username), accountID](bool success, std::string const& response) {
        if (paimon::isRuntimeShuttingDown()) {
            if (callback) callback(false, {});
            return;
        }
        if (!success || response.empty()) {
            if (callback) callback(false, {});
            return;
        }

        auto parsed = matjson::parse(response);
        if (!parsed.isOk()) {
            if (callback) callback(false, {});
            return;
        }
        auto json = parsed.unwrap();

        InitResult result;

        paimon::modauth::SessionRoles roles;
        if (json.contains("moderator") && json["moderator"].isObject()) {
            auto& mod = json["moderator"];
            bool const authenticated = mod["authenticated"].asBool().unwrapOr(false);
            result.isModerator = authenticated && mod["isModerator"].asBool().unwrapOr(false);
            result.isAdmin = authenticated && mod["isAdmin"].asBool().unwrapOr(false);
            result.isVip = authenticated && mod["isVip"].asBool().unwrapOr(false);
            result.newModCode = mod["newModCode"].asString().unwrapOr("");
            result.gdVerificationFailed = mod["gdVerificationFailed"].asBool().unwrapOr(false);

            if (result.isAdmin) result.isModerator = true;
            if (authenticated) {
                roles.authenticated = true;
                roles.isAdmin = result.isAdmin;
                roles.isModerator = result.isModerator;
                roles.isVip = result.isVip || result.isModerator;
                roles.isHelper = mod["isHelper"].asBool().unwrapOr(false);
                roles.isIdea = mod["isIdea"].asBool().unwrapOr(false);
                roles.primaryRole = mod["primaryRole"].asString().unwrapOr(
                    roles.isAdmin ? "admin" : roles.isModerator ? "moderator" :
                    roles.isVip ? "vip" : roles.isHelper ? "helper" : roles.isIdea ? "idea" : "user");
                if (mod.contains("permissions") && mod["permissions"].isArray()) {
                    if (auto arr = mod["permissions"].asArray(); arr.isOk()) {
                        for (auto const& item : arr.unwrap()) {
                            if (auto p = item.asString(); p.isOk() && !p.unwrap().empty()) {
                                roles.permissions.push_back(p.unwrap());
                            }
                        }
                    }
                }
            }
        }



        if (json.contains("manifest") && json["manifest"].isObject()) {
            matjson::Value manifestPayload = json["manifest"];
            if (json.contains("cdnBaseUrl")) {
                manifestPayload["_cdnBaseUrl"] = json["cdnBaseUrl"];
            }
            updateManifestFromJson(manifestPayload.dump());
            saveManifestToDisk();
        }

        if (json.contains("cdnBaseUrl")) {
            result.cdnBaseUrl = json["cdnBaseUrl"].asString().unwrapOr("");
            if (!result.cdnBaseUrl.empty()) {
                m_cdnBaseURL = result.cdnBaseUrl;
            }
        }

        if (json.contains("daily") && !json["daily"].isNull()) {
            result.dailyJson = json["daily"].dump();
        }
        if (json.contains("weekly") && !json["weekly"].isNull()) {
            result.weeklyJson = json["weekly"].dump();
        }

        // roles are volatile-only: stamp the session from this server verdict
        // and mirror vip to disk as a ui hint, never as authority.
        paimon::modauth::setVerifiedSession(username, accountID, roles);
        Mod::get()->setSavedValue<bool>("is-verified-vip", paimon::modauth::isVerifiedVip());

        if (callback) callback(true, result);
    }, true);
}

void HttpClient::fetchBatchRatings(std::vector<int> const& levelIds, std::string const& username,
    std::unordered_map<int, std::string> const& thumbnailIds, BatchRatingsCallback callback) {
    if (levelIds.empty()) {
        if (callback) callback(true, {});
        return;
    }

    PaimonDebug::log("[HttpClient] fetchBatchRatings for {} levels", levelIds.size());

    if (isWorkerExhausted()) {
        PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fetch batch ratings");
        if (callback) callback(false, {});
        return;
    }

    matjson::Value body;
    body["username"] = username;
    auto arr = matjson::Value::array();
    for (int id : levelIds) {
        arr.push(id);
    }
    body["levelIds"] = arr;

    if (!thumbnailIds.empty()) {
        matjson::Value thumbMap;
        for (auto& [id, thumbId] : thumbnailIds) {
            thumbMap[std::to_string(id)] = thumbId;
        }
        body["thumbnailIds"] = thumbMap;
    }

    std::string url = m_serverURL + "/api/v2/ratings/batch";
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };

    performRequest(url, "POST", body.dump(), headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (!success || response.empty()) {
            if (callback) callback(false, {});
            return;
        }

        auto parsed = matjson::parse(response);
        if (!parsed.isOk()) {
            if (callback) callback(false, {});
            return;
        }
        auto json = parsed.unwrap();

        std::unordered_map<int, BatchRatingEntry> ratings;
        if (json.contains("ratings") && json["ratings"].isObject()) {
            for (auto& [key, val] : json["ratings"]) {
                if (!val.isObject()) continue;
                int levelId = 0;
                auto numResult = geode::utils::numFromString<int>(key);
                if (numResult.isOk()) levelId = numResult.unwrap();
                if (levelId <= 0) continue;

                BatchRatingEntry entry;
                entry.average = static_cast<float>(val["average"].asDouble().unwrapOr(0.0));
                entry.count = static_cast<int>(val["count"].asInt().unwrapOr(0));
                entry.userVote = static_cast<int>(val["userVote"].asInt().unwrapOr(0));
                ratings[levelId] = entry;
            }
        }

        if (callback) callback(true, ratings);
    }, false);
}

void HttpClient::fetchDiscovery(int creatorsLimit, int thumbnailsLimit, int uploadsLimit, DiscoveryCallback callback) {
    PaimonDebug::log("[HttpClient] fetchDiscovery (creators={}, thumbs={}, uploads={})", creatorsLimit, thumbnailsLimit, uploadsLimit);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fetch discovery");
        if (callback) callback(false, "");
        return;
    }

    std::string url = m_serverURL + "/api/discovery?creatorsLimit=" + std::to_string(creatorsLimit)
        + "&thumbnailsLimit=" + std::to_string(thumbnailsLimit)
        + "&uploadsLimit=" + std::to_string(uploadsLimit);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (callback) callback(success, response);
    }, false);
}

void HttpClient::fetchQueueSummary(std::string const& username, int accountID, int previewCount, QueueSummaryCallback callback) {
    PaimonDebug::log("[HttpClient] fetchQueueSummary for user={}", username);

    if (isWorkerExhausted()) {
        if (callback) callback(false, "");
        return;
    }

    std::string url = m_serverURL + "/api/queue/summary?username=" + encodeQueryParam(username)
        + "&accountID=" + std::to_string(accountID)
        + "&preview=" + std::to_string(previewCount);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    if (auto const st = activeSessionToken(); !st.empty()) {
        headers.push_back("X-Session-Token: " + st);
    }
    if (auto const mc = activeModCode(); !mc.empty()) {
        headers.push_back("X-Mod-Code: " + mc);
    }

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (callback) callback(success, response);
    }, false);
}

void HttpClient::fetchBatchProfileBundle(std::vector<std::pair<int, std::string>> const& accounts, BatchBundleCallback callback) {
    if (accounts.empty()) {
        if (callback) callback(true, R"({"bundles":{}})");
        return;
    }

    PaimonDebug::log("[HttpClient] fetchBatchProfileBundle for {} accounts", accounts.size());

    if (isWorkerExhausted()) {
        if (callback) callback(false, "");
        return;
    }

    auto arr = matjson::Value::array();
    for (auto const& [id, name] : accounts) {
        matjson::Value acc;
        acc["accountID"] = id;
        acc["username"] = name;
        arr.push(acc);
    }
    matjson::Value body;
    body["accounts"] = arr;

    std::string url = m_serverURL + "/api/profile/batch-bundle";
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };

    performRequest(url, "POST", body.dump(), headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (callback) callback(success, response);
    }, false);
}

void HttpClient::post(std::string const& endpoint, std::string const& data, GenericCallback callback) {
    std::string url = endpoint;
    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        url = m_serverURL + endpoint;
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", data, headers, callback);
}

void HttpClient::postWithAuth(std::string const& endpoint, std::string const& data, GenericCallback callback) {
    std::string url = endpoint;
    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        url = m_serverURL + endpoint;
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    auto const sessionToken = activeSessionToken();
    auto const modCode = activeModCode();
    bool moderationAction = endpoint.starts_with("/api/queue/") || endpoint.starts_with("/api/admin/") ||
        endpoint == "/api/daily/set" || endpoint == "/api/weekly/set";
    if (moderationAction) {
        auto parsed = matjson::parse(data);
        if ((sessionToken.empty() && modCode.empty()) || !parsed.isOk() || !parsed.unwrap().isObject()) {
            callback(false, "Inicia sesion y verifica tus permisos antes de continuar.");
            return;
        }
        auto const& body = parsed.unwrap();
        auto actor = body["adminUser"].asString().unwrapOr("");
        if (actor.empty()) actor = body["moderator"].asString().unwrapOr("");
        if (actor.empty()) actor = body["actor"].asString().unwrapOr("");
        if (actor.empty()) actor = body["username"].asString().unwrapOr("");
        auto* account = GJAccountManager::get();
        auto* game = GameManager::get();
        if (!account || !game || account->m_accountID <= 0 ||
            body["accountID"].asInt().unwrapOr(0) != account->m_accountID ||
            geode::utils::string::toLower(actor) != geode::utils::string::toLower(game->m_playerName)) {
            callback(false, "La cuenta cambio. Verifica la sesion antes de continuar.");
            return;
        }
    }
    if (!sessionToken.empty()) headers.push_back("X-Session-Token: " + sessionToken);
    if (!modCode.empty()) headers.push_back("X-Mod-Code: " + modCode);
    performRequest(url, "POST", data, headers, [callback](bool success, std::string const& response) {
        auto parsed = matjson::parse(response);
        if (success && parsed.isOk() && parsed.unwrap().isObject() && parsed.unwrap().contains("success")) {
            success = parsed.unwrap()["success"].asBool().unwrapOr(false);
        }
        callback(success, response);
    }, false);
}

void HttpClient::postWithoutModCode(std::string const& endpoint, std::string const& data, GenericCallback callback) {
    std::string url = endpoint;
    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        url = m_serverURL + endpoint;
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", data, headers, callback, false);
}

void HttpClient::uploadMultipart(std::string const& endpoint,
    std::vector<std::pair<std::string, std::string>> const& fields,
    std::string const& fileField, std::string const& filename,
    std::vector<uint8_t> const& data, std::string const& contentType,
    GenericCallback callback) {
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    if (!m_modCode.empty()) headers.push_back("X-Mod-Code: " + m_modCode);
    performUpload(m_serverURL + endpoint, fileField, filename, data, fields, headers,
        std::move(callback), contentType);
}

void HttpClient::getBinaryWithAuth(std::string const& endpoint,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback) {
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    performBinaryRequest(m_serverURL + endpoint, headers, std::move(callback), 20, true);
}

void HttpClient::getRawBinaryWithAuth(std::string const& endpoint,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&, int)> callback) {
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    if (!m_viewerToken.empty()) headers.push_back("X-Viewer-Token: " + m_viewerToken);
    performBinaryRequestEx(m_serverURL + endpoint, headers, std::move(callback), 25, true, false);
}

void HttpClient::getPetShopList(GenericCallback callback) {
    get("/api/pet-shop/list", callback);
}

void HttpClient::downloadPetShopItem(std::string const& itemId, std::string const& format,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback) {
    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/pet-shop/" + itemId + "." + format;
        PaimonDebug::log("[HttpClient] Worker exhausted, using CDN for pet-shop item: {}", cdnUrl);
        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders, callback);
        return;
    }
    std::string url = m_serverURL + "/api/pet-shop/download/" + itemId + "." + format;
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    performBinaryRequest(url, headers, callback);
}

void HttpClient::uploadPetShopItem(std::string const& name, std::string const& creator,
    std::vector<uint8_t> const& imageData, std::string const& format,
    UploadCallback callback) {
    std::string url = m_serverURL + "/api/pet-shop/upload";
    std::string ct = (format == "gif") ? "image/gif" : "image/png";
    std::string filename = "pet." + format;

    std::vector<std::pair<std::string, std::string>> fields = {
        {"name", name},
        {"creator", creator}
    };
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    if (auto const mc = activeModCode(); !mc.empty()) headers.push_back("X-Mod-Code: " + mc);
    performUpload(url, "image", filename, imageData, fields, headers, callback, ct);
}

void HttpClient::getWhitelist(std::string const& type, GenericCallback callback) {
    std::string username = getSafeAccountUsername();
    int accountID = getSafeAccountID();
    std::string url = m_serverURL + "/api/whitelist?type=" + encodeQueryParam(type)
        + "&username=" + encodeQueryParam(username)
        + "&accountID=" + std::to_string(accountID);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    if (auto const mc = activeModCode(); !mc.empty()) headers.push_back("X-Mod-Code: " + mc);
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::postWhitelist(char const* endpoint, std::string const& targetUsername, std::string const& type, GenericCallback callback) {
    std::string adminUser = getSafeAccountUsername();
    int accountID = getSafeAccountID();

    matjson::Value json = matjson::makeObject({
        {"username", targetUsername},
        {"type", type},
        {"adminUser", adminUser},
        {"moderator", adminUser},
        {"accountID", accountID}
    });

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    if (auto const mc = activeModCode(); !mc.empty()) {
        headers.push_back("X-Mod-Code: " + mc);
    }
    performRequest(m_serverURL + endpoint, "POST", json.dump(), headers, callback);
}

void HttpClient::addToWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback) {
    postWhitelist("/api/whitelist/add", targetUsername, type, std::move(callback));
}

void HttpClient::removeFromWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback) {
    postWhitelist("/api/whitelist/remove", targetUsername, type, std::move(callback));
}


bool HttpClient::isUrlSafe(std::string const& url) {
    return paimon::net::isPublicHttpUrl(url);
}

void HttpClient::postBadgeForm(std::string const& url, geode::utils::web::MultipartForm form, GenericCallback callback) {
    if (!isTrustedBackendUrl(url)) {
        PaimonDebug::log("[HttpClient] Refusing badge upload to untrusted host");
        if (callback) callback(false, "untrusted host");
        return;
    }
    auto req = web::WebRequest();
    req.acceptEncoding("gzip, deflate");
    req.followRedirects(false);
    req.header("X-API-Key", m_apiKey);
    if (auto const st = activeSessionToken(); !st.empty()) req.header("X-Session-Token", st);
    if (auto const mc = activeModCode(); !mc.empty()) req.header("X-Mod-Code", mc);
    req.bodyMultipart(form);

    auto callbackGate = m_callbackGate;
    WebHelper::dispatch(std::move(req), "POST", url, [callbackGate, callback = std::move(callback)](web::WebResponse res) mutable {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) return;
        bool success = res.ok();
        std::string body = res.string().unwrapOr("");
        std::string responseStr = success
            ? body
            : ("HTTP " + std::to_string(res.code()) + ": " + (body.empty() ? std::string("Unknown error") : body));
        if (callback) callback(success, responseStr);
    });
}

void HttpClient::uploadCustomBadge(int accountID, std::string const& emoteName, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading custom badge for account {}: {}", accountID, emoteName);

    web::MultipartForm form;
    form.param("accountID", std::to_string(accountID));
    form.param("username", getSafeAccountUsername());
    form.param("emoteName", emoteName);

    postBadgeForm(m_serverURL + "/api/profile/badge", std::move(form), std::move(callback));
}

void HttpClient::downloadCustomBadge(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading custom badge for account {}", accountID);

    std::string url = m_serverURL + "/api/profile/badge/" + std::to_string(accountID);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        callback(success, response);
    });
}

void HttpClient::deleteCustomBadge(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Deleting custom badge for account {}", accountID);

    web::MultipartForm form;
    form.param("accountID", std::to_string(accountID));
    form.param("username", getSafeAccountUsername());

    postBadgeForm(m_serverURL + "/api/profile/badge/delete", std::move(form), std::move(callback));
}

void HttpClient::downloadCustomBadgeBatch(std::vector<int> const& accountIDs, GenericCallback callback) {
    if (accountIDs.empty()) {
        if (callback) callback(true, R"({"badges":{}})");
        return;
    }

    std::string ids;
    for (size_t i = 0; i < accountIDs.size(); ++i) {
        if (i > 0) ids += ',';
        ids += std::to_string(accountIDs[i]);
    }

    PaimonDebug::log("[HttpClient] Downloading custom badges batch: {}", ids);

    std::string url = m_serverURL + "/api/profile/badge/batch?ids=" + ids;

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        callback(success, response);
    });
}

namespace {
bool parseBatchResponse(std::string const& body,
                        std::unordered_map<int, HttpClient::BatchItem>& out) {
    auto parsed = matjson::parse(body);
    if (!parsed.isOk()) {
        PaimonDebug::warn("[HttpClient] Batch parse error: {}", body.substr(0, 200));
        return false;
    }
    auto const& root = parsed.unwrap();
    if (!root.isObject()) return false;
    auto itemsRes = root["items"];
    if (!itemsRes.isObject()) {
        return true;
    }
    for (auto const& [key, val] : itemsRes) {
        int id = 0;
        auto parseResult = geode::utils::numFromString<int>(std::string(key));
        if (!parseResult.isOk()) continue;
        id = parseResult.unwrap();
        if (id <= 0) continue;

        HttpClient::BatchItem item;
        item.ok = val["ok"].asBool().unwrapOr(false);
        item.format = val["format"].asString().unwrapOr("");
        if (item.ok) {
            std::string b64 = val["data"].asString().unwrapOr("");
            if (!b64.empty()) {
                std::vector<uint8_t> bytes;
                if (paimon::base64Decode(b64, bytes) && !bytes.empty()) {
                    item.data = std::move(bytes);
                } else {
                    item.ok = false;
                }
            } else {
                item.ok = false;
            }
        }
        out.emplace(id, std::move(item));
    }
    return true;
}

std::string buildBatchIdsJson(std::string const& key, std::vector<int> const& ids, size_t cap) {
    matjson::Value root = matjson::Value::object();
    matjson::Value arr = matjson::Value::array();
    size_t pushed = 0;
    for (int id : ids) {
        if (id <= 0) continue;
        arr.push(id);
        if (++pushed >= cap) break;
    }
    root[key] = arr;
    return root.dump();
}
}

// each bunny object read costs one of 50 subrequests per invocation: batch
// sizes mirror the worker max_batch_*_fetches; overshooting 500s the batch.
static constexpr size_t MAX_ASSET_BATCH = 15;
static constexpr size_t MAX_PROFILE_BATCH = 10;

void HttpClient::downloadBatchAssets(std::vector<int> const& ids, char const* idsKey,
        size_t maxBatch, char const* endpoint, char const* logLabel, bool warnExhausted,
        BatchDownloadCallback callback) {
    if (ids.empty()) {
        if (callback) callback(true, {});
        return;
    }
    if (isWorkerExhausted()) {
        if (warnExhausted) PaimonDebug::warn("[HttpClient] Worker exhausted, batch {} skipped", logLabel);
        if (callback) callback(false, {});
        return;
    }

    std::string body = buildBatchIdsJson(idsKey, ids, maxBatch);
    std::string url = m_serverURL + endpoint;

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json"
    };

    PaimonDebug::log("[HttpClient] {}: {} ids -> {}", logLabel, ids.size(), url);

    performRequest(url, "POST", body, headers,
        [callback = std::move(callback)](bool success, std::string const& response) {
            std::unordered_map<int, BatchItem> items;
            if (!success) {
                if (callback) callback(false, items);
                return;
            }
            bool parsed = parseBatchResponse(response, items);
            if (callback) callback(parsed, items);
        }, false);
}

void HttpClient::downloadThumbnailsBatch(std::vector<int> const& levelIds,
                                         BatchDownloadCallback callback) {
    downloadBatchAssets(levelIds, "ids", MAX_ASSET_BATCH, "/api/thumbnails/batch",
        "downloadThumbnailsBatch", true, std::move(callback));
}

void HttpClient::getThumbnailsBatch(std::vector<int> const& levelIds, BatchListCallback callback) {
    if (levelIds.empty()) {
        if (callback) callback(true, {});
        return;
    }

    // the list-batch endpoint caps ids per request at MAX_ASSET_BATCH; chunk
    // so the rest go in follow-up requests instead of being dropped.
    if (levelIds.size() <= MAX_ASSET_BATCH) {
        getThumbnailsBatchChunk(levelIds, std::move(callback));
        return;
    }

    struct MergeState {
        std::unordered_map<int, std::string> items;
        size_t remaining = 0;
        bool allOk = true;
    };
    auto state = std::make_shared<MergeState>();
    state->remaining = (levelIds.size() + MAX_ASSET_BATCH - 1) / MAX_ASSET_BATCH;
    for (size_t i = 0; i < levelIds.size(); i += MAX_ASSET_BATCH) {
        auto end = std::min(levelIds.size(), i + MAX_ASSET_BATCH);
        std::vector<int> chunk(levelIds.begin() + i, levelIds.begin() + end);
        getThumbnailsBatchChunk(std::move(chunk),
            [state, callback](bool ok, std::unordered_map<int, std::string> const& items) {
                if (!ok) {
                    state->allOk = false;
                } else {
                    state->items.insert(items.begin(), items.end());
                }
                if (--state->remaining == 0 && callback) {
                    callback(state->allOk, state->items);
                }
            });
    }
}

void HttpClient::getThumbnailsBatchChunk(std::vector<int> const& levelIds, BatchListCallback callback) {
    if (levelIds.empty()) {
        if (callback) callback(true, {});
        return;
    }

    std::string body = buildBatchIdsJson("ids", levelIds, MAX_ASSET_BATCH);
    std::string url = m_serverURL + "/api/thumbnails/list-batch";

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json"
    };

    PaimonDebug::log("[HttpClient] getThumbnailsBatch: {} ids -> {}", levelIds.size(), url);

    performRequest(url, "POST", body, headers,
        [callback = std::move(callback)](bool success, std::string const& response) {
            std::unordered_map<int, std::string> items;
            if (!success) {
                if (callback) callback(false, items);
                return;
            }
            auto parsed = matjson::parse(response);
            if (!parsed.isOk()) {
                PaimonDebug::warn("[HttpClient] getThumbnailsBatch parse error");
                if (callback) callback(false, items);
                return;
            }
            auto const& root = parsed.unwrap();
            if (!root.isObject() || !root.contains("thumbnails")) {
                if (callback) callback(true, items);
                return;
            }
            auto const& thumbsObj = root["thumbnails"];
            if (!thumbsObj.isObject()) {
                if (callback) callback(true, items);
                return;
            }
            for (auto const& [key, val] : thumbsObj) {
                int id = 0;
                auto parseResult = geode::utils::numFromString<int>(std::string(key));
                if (!parseResult.isOk()) continue;
                id = parseResult.unwrap();
                if (id <= 0) continue;
                matjson::Value wrapped = matjson::Value::object();
                wrapped["thumbnails"] = val;
                items.emplace(id, wrapped.dump());
            }
            if (callback) callback(true, items);
        }, false);
}

void HttpClient::downloadProfileBackgroundsBatch(std::vector<int> const& accountIDs,
                                                 BatchDownloadCallback callback) {
    downloadBatchAssets(accountIDs, "accountIDs", MAX_PROFILE_BATCH, "/api/profilebackground/batch",
        "downloadProfileBackgroundsBatch", false, std::move(callback));
}

void HttpClient::downloadProfileImgsBatch(std::vector<int> const& accountIDs,
                                          BatchDownloadCallback callback) {
    downloadBatchAssets(accountIDs, "accountIDs", MAX_PROFILE_BATCH, "/api/profileimgs/batch",
        "downloadProfileImgsBatch", false, std::move(callback));
}
