#include "HttpClient.hpp"
#include "Debug.hpp"
#include "WebHelper.hpp"
#include "ThreadTracker.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../core/Settings.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <filesystem>
#include "FormatDetect.hpp"

using namespace geode::prelude;

namespace {
std::string urlEncodeParam(std::string_view input) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;

    for (unsigned char ch : input) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
            continue;
        }

        encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }

    return encoded.str();
}

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

bool decodeBase64(std::string const& input, std::vector<uint8_t>& out) {
    static int8_t const* T = []() {
        static int8_t arr[256];
        for (int i = 0; i < 256; ++i) arr[i] = -1;
        char const* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) arr[(unsigned char)alphabet[i]] = (int8_t)i;
        return arr;
    }();

    out.clear();
    out.reserve((input.size() / 4) * 3);

    int bits = 0, value = 0;
    for (unsigned char c : input) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        int8_t v = T[c];
        if (v < 0) return false;
        value = (value << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((value >> bits) & 0xFF));
        }
    }
    return true;
}
}

HttpClient::HttpClient() {
    m_serverURL = "https://api.flozwer.org";
    m_forumServerURL = "https://paimbnailsbot.onrender.com";

    // The client key is shipped and never persisted.
    m_apiKey = "074b91c9-6631-4670-a6f08a2ce970-0183-471b";

    m_modCode = Mod::get()->getSavedValue<std::string>("mod-code", "");
    m_callbackGate = std::make_shared<std::atomic<bool>>(true);

    loadManifestFromDisk();

    PaimonDebug::log("[HttpClient] Initialized with server: {}", m_serverURL);
    PaimonDebug::log("[HttpClient] Forum server: {}", m_forumServerURL);
}

void HttpClient::cleanTasks(bool allowNewRequests) {
    if (m_callbackGate) {
        m_callbackGate->store(false, std::memory_order_release);
    }
    if (allowNewRequests) {
        m_callbackGate = std::make_shared<std::atomic<bool>>(true);
        ThumbnailLoader::get().clearFailedCache();
    } else {
        m_callbackGate.reset();
    }
}

void HttpClient::setServerURL(std::string const& url) {
    m_serverURL = url;
    if (!m_serverURL.empty() && m_serverURL.back() == '/') {
        m_serverURL.pop_back();
    }
    PaimonDebug::log("[HttpClient] Server URL updated to: {}", m_serverURL);
}

std::string HttpClient::buildAssetURL(std::string const& path, std::string const& defaultFolder) const {
    if (path.empty()) return {};

    if (path.starts_with("http://") || path.starts_with("https://")) {
        return path;
    }

    std::string normalized = path;
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }

    if (!defaultFolder.empty() && normalized.find('/') == std::string::npos) {
        auto prefix = defaultFolder + "/";
        normalized = prefix + normalized;
    }

    return m_serverURL + "/" + normalized;
}

void HttpClient::setForumServerURL(std::string const& url) {
    m_forumServerURL = url;
    if (!m_forumServerURL.empty() && m_forumServerURL.back() == '/') {
        m_forumServerURL.pop_back();
    }
    PaimonDebug::log("[HttpClient] Forum server URL updated to: {}", m_forumServerURL);
}

std::string HttpClient::encodeQueryParam(std::string const& value) {
    return urlEncodeParam(value);
}

void HttpClient::setModCode(std::string const& code) {
    m_modCode = code;
    Mod::get()->setSavedValue("mod-code", code);
    PaimonDebug::log("[HttpClient] Mod code updated.");
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

// Apply headers and detect an explicit X-Mod-Code.
static void applyHeaderList(web::WebRequest& req, std::vector<std::string> const& headers,
                            bool* outHasModCode = nullptr) {
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos == std::string::npos) continue;
        std::string key = header.substr(0, colonPos);
        std::string value = header.substr(colonPos + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        req.header(key, value);
        if (outHasModCode && (key == "X-Mod-Code" || key == "x-mod-code")) {
            *outHasModCode = true;
        }
    }
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
    req.timeout(std::chrono::seconds(10));
    req.acceptEncoding("gzip, deflate");

    bool hasExplicitModCodeHeader = false;

    applyHeaderList(req, headers, &hasExplicitModCodeHeader);

    if (includeStoredModCode && !hasExplicitModCodeHeader && !m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    if (method == "POST" && !postData.empty()) {
        req.bodyString(postData);
    }

    // Capture stable state; workers may outlive the singleton.
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

        // Count repeated server failures, but keep app rate limits transient.
        bool failureCounted = false;
        if (!success && res.code() == 503) {
            int newCount = consecutiveFailuresRef->fetch_add(1, std::memory_order_acq_rel) + 1;
            failureCounted = true;
            if (newCount >= EXHAUSTION_THRESHOLD) {
                workerExhaustedRef->store(true, std::memory_order_release);
                exhaustedAtRef->store(static_cast<int64_t>(std::time(nullptr)), std::memory_order_release);
                PaimonDebug::warn("[HttpClient] Worker marked exhausted after {} consecutive 503s", newCount);
            }
        } else if (!success && res.code() == 429) {
            // Distinguish the app limiter from a platform quota failure.
            bool isAppRateLimit = body.find("RATE_LIMITED") != std::string::npos
                || body.find("Rate limit exceeded") != std::string::npos;
            if (!isAppRateLimit) {
                int newCount = consecutiveFailuresRef->fetch_add(1, std::memory_order_acq_rel) + 1;
                failureCounted = true;
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

void HttpClient::performBinaryRequest(
    std::string const& url,
    std::vector<std::string> const& headers,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback,
    int timeoutSeconds,
    bool includeModCode
) {
    auto callbackGate = m_callbackGate;
    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(timeoutSeconds));
    req.userAgent("Paimbnails/2.x (Geode)");
    req.acceptEncoding("gzip, deflate");

    req.header("Accept", "image/webp,image/png,image/gif,*/*");

    applyHeaderList(req, headers);

    if (includeModCode && !m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    std::string urlCopy = url;

    WebHelper::dispatch(std::move(req), "GET", url, [callbackGate, callback, urlCopy](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        bool success = res.ok();
        std::vector<uint8_t> data = success ? res.data() : std::vector<uint8_t>{};

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

            if (success && data.size() >= 4) {
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
        if (callback) callback(success, data);
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
    req.timeout(std::chrono::seconds(30));
    req.acceptEncoding("gzip, deflate");

    applyHeaderList(req, headers);

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

void HttpClient::uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile background for account {} ({} bytes)", accountID, pngData.size());

    std::string url = m_serverURL + "/api/backgrounds/upload";
    std::string filename = std::to_string(accountID) + ".png";

    auto account = AccountVerifier::get().verify();
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        pngData,
        formFields,
        headers,
        [callback = std::move(callback), accountID](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile upload successful for account {}", accountID);
                std::string resultMsg = "Profile upload successful";
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("pendingVerification") && json["pendingVerification"].asBool().unwrapOr(false)) {
                        resultMsg = "pending_verification";
                    }
                    if (json.contains("message") && json["message"].isString()) {
                        auto serverMsg = json["message"].asString().unwrapOr("");
                        if (!serverMsg.empty()) resultMsg = serverMsg;
                    }
                }
                callback(true, resultMsg);
            } else {
                log::error("[HttpClient] Profile upload failed for account {}: {}", accountID, response);
                callback(false, "Profile upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile background GIF for account {} ({} bytes)", accountID, gifData.size());

    std::string url = m_serverURL + "/api/backgrounds/upload-gif";
    std::string filename = std::to_string(accountID) + ".gif";

    auto account = AccountVerifier::get().verify();
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        gifData,
        formFields,
        headers,
        [callback = std::move(callback), accountID](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile GIF upload successful for account {}", accountID);
                std::string resultMsg = "Profile GIF upload successful";
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("pendingVerification") && json["pendingVerification"].asBool().unwrapOr(false)) {
                        resultMsg = "pending_verification";
                    }
                    if (json.contains("message") && json["message"].isString()) {
                        auto serverMsg = json["message"].asString().unwrapOr("");
                        if (!serverMsg.empty()) resultMsg = serverMsg;
                    }
                }
                callback(true, resultMsg);
            } else {
                log::error("[HttpClient] Profile GIF upload failed for account {}: {}", accountID, response);
                callback(false, "Profile GIF upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile background video for account {} ({} bytes)", accountID, mp4Data.size());

    std::string url = m_serverURL + "/api/backgrounds/upload-video";
    std::string filename = std::to_string(accountID) + ".mp4";

    auto account = AccountVerifier::get().verify();
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        mp4Data,
        formFields,
        headers,
        [callback = std::move(callback), accountID](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile video upload successful for account {}", accountID);
                std::string resultMsg = "Profile video upload successful";
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("message") && json["message"].isString()) {
                        auto serverMsg = json["message"].asString().unwrapOr("");
                        if (!serverMsg.empty()) resultMsg = serverMsg;
                    }
                }
                callback(true, resultMsg);
            } else {
                log::error("[HttpClient] Profile video upload failed for account {}: {}", accountID, response);
                callback(false, "Profile video upload failed: " + response);
            }
        },
        "video/mp4"
    );
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

    auto account = AccountVerifier::get().verify();
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/profileimgs"},
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };

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
                std::string resultMsg = "Profile image upload successful";
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("pendingVerification") && json["pendingVerification"].asBool().unwrapOr(false)) {
                        resultMsg = "pending_verification";
                    }
                    if (json.contains("message") && json["message"].isString()) {
                        auto serverMsg = json["message"].asString().unwrapOr("");
                        if (!serverMsg.empty()) resultMsg = serverMsg;
                    }
                }
                callback(true, resultMsg);
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
        performBinaryRequest(cdnUrl, cdnHeaders, [callback = std::move(callback), accountID](bool success, std::vector<uint8_t> const& data) {
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
    form.param("config", jsonConfig);

    auto req = web::WebRequest();
    req.acceptEncoding("gzip, deflate");
    req.header("X-API-Key", m_apiKey);
    if (!m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
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

void HttpClient::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    PaimonDebug::log("[HttpClient] Uploading thumbnail as PNG for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/mod/upload";
    std::string filename = std::to_string(levelId) + ".png"; 
    
    auto account = AccountVerifier::get().verify();

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(account.accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };
    if (!levelMeta.empty()) formFields.push_back({"levelMeta", levelMeta});

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    // X-Mod-Code sends moderator uploads directly; others use the pending queue.
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
    }

    performUpload(
        url,
        "image",
        filename,
        pngData,
        formFields,
        headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Upload successful for level {}", levelId);
                std::string message = "Upload successful";
                auto parsed = matjson::parse(response);
                if (parsed.isOk()) {
                    auto msgVal = parsed.unwrap()["message"].asString();
                    if (msgVal.isOk()) message = msgVal.unwrap();
                }
                callback(true, message);
            } else {
                log::error("[HttpClient] Upload failed for level {}: {}", levelId, response);
                callback(false, "Upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    PaimonDebug::log("[HttpClient] Uploading GIF for level {}, size: {} bytes", levelId, gifData.size());
    
    std::string url = m_serverURL + "/mod/upload-gif";
    std::string filename = std::to_string(levelId) + ".gif";
    
    auto account = AccountVerifier::get().verify();

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(account.accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };
    if (!levelMeta.empty()) formFields.push_back({"levelMeta", levelMeta});

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
    }
    
    performUpload(url, "image", filename, gifData, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                std::string message = "Upload successful";
                auto parsed = matjson::parse(response);
                if (parsed.isOk()) {
                    auto msgVal = parsed.unwrap()["message"].asString();
                    if (msgVal.isOk()) message = msgVal.unwrap();
                }
                callback(true, message);
            } else {
                log::error("[HttpClient] GIF upload failed for level {}: {}", levelId, response);
                callback(false, "GIF Upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    PaimonDebug::log("[HttpClient] Uploading video for level {}, size: {} bytes", levelId, mp4Data.size());
    
    std::string url = m_serverURL + "/mod/upload-video";
    std::string filename = std::to_string(levelId) + ".mp4";
    
    auto account = AccountVerifier::get().verify();

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails/video"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(account.accountID)},
        {"isOfficialServer", account.isOfficialServer ? "true" : "false"}
    };
    if (!levelMeta.empty()) formFields.push_back({"levelMeta", levelMeta});

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
    }
    
    performUpload(url, "image", filename, mp4Data, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                std::string message = "Upload successful";
                auto parsed = matjson::parse(response);
                if (parsed.isOk()) {
                    auto msgVal = parsed.unwrap()["message"].asString();
                    if (msgVal.isOk()) message = msgVal.unwrap();
                }
                callback(true, message);
            } else {
                log::error("[HttpClient] Video upload failed for level {}: {}", levelId, response);
                callback(false, "Video Upload failed: " + response);
            }
        },
        "video/mp4"
    );
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

void HttpClient::uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    PaimonDebug::log("[HttpClient] Uploading suggestion for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/suggestions/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = getSafeAccountID();

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/suggestions"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    if (!levelMeta.empty()) formFields.push_back({"levelMeta", levelMeta});
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Suggestion upload successful for level {}", levelId);
                callback(true, "Suggestion uploaded successfully");
            } else {
                log::error("[HttpClient] Suggestion upload failed for level {}: {}", levelId, response);
                callback(false, "Suggestion upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta) {
    PaimonDebug::log("[HttpClient] Uploading update for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/updates/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = getSafeAccountID();

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/updates"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    if (!levelMeta.empty()) formFields.push_back({"levelMeta", levelMeta});
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Update upload successful for level {}", levelId);
                callback(true, "Update uploaded successfully");
            } else {
                log::error("[HttpClient] Update upload failed for level {}: {}", levelId, response);
                callback(false, "Update upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::downloadSuggestion(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading suggestion for level {}", levelId);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/suggestions/" + std::to_string(levelId) + ".webp";
        PaimonDebug::log("[HttpClient] Worker exhausted, using CDN for suggestion: {}", cdnUrl);
        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders, [callback = std::move(callback), levelId](bool success, std::vector<uint8_t> const& data) {
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

    std::string url = m_serverURL + "/suggestions/" + std::to_string(levelId) + ".webp";
    
    performBinaryRequest(url, headers, [callback = std::move(callback), levelId](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Suggestion downloaded for level {}: {} bytes", levelId, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No suggestion found for level {}", levelId);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadUpdate(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading update for level {}", levelId);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/updates/" + std::to_string(levelId) + ".webp";
        PaimonDebug::log("[HttpClient] Worker exhausted, using CDN for update: {}", cdnUrl);
        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders, [callback = std::move(callback), levelId](bool success, std::vector<uint8_t> const& data) {
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

    std::string url = m_serverURL + "/updates/" + std::to_string(levelId) + ".webp";
    
    performBinaryRequest(url, headers, [callback = std::move(callback), levelId](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Update downloaded for level {}: {} bytes", levelId, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No update found for level {}", levelId);
            callback(false, {}, 0, 0);
        }
    });
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
        if (keyVal.isOk() && !keyVal.unwrap().empty()) {
            m_cdnBaseURL = keyVal.unwrap();
            PaimonDebug::log("[HttpClient] Got CDN base URL from manifest: {}", m_cdnBaseURL);
        }
    }

    for (auto& [key, val] : root) {
        if (!val.isObject()) continue;

        auto parsed = geode::utils::numFromString<int>(key);
        if (!parsed.isOk()) continue;
        int levelId = parsed.unwrap();
        if (levelId <= 0) continue;

        ManifestEntry entry;
        entry.format         = val["format"].asString().unwrapOr("");
        entry.cdnUrl         = val["cdnUrl"].asString().unwrapOr("");
        entry.version        = val["version"].asString().unwrapOr("");
        entry.id             = val["id"].asString().unwrapOr("");
        entry.revisionToken  = val["revisionToken"].asString().unwrapOr("");
        entry.cachedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (!entry.cdnUrl.empty()) {
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

    // Serialize under the lock; disk I/O stays off the main thread.
    std::string json;
    size_t entryCount = 0;
    auto path = Mod::get()->getSaveDir() / "manifest_cache.json";

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
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        auto writeRes = geode::utils::file::writeStringSafe(path, json);
        if (writeRes.isErr()) {
            PaimonDebug::warn("[HttpClient] Failed to write manifest_cache.json: {}", writeRes.unwrapErr());
            return;
        }
        PaimonDebug::log("[HttpClient] Manifest saved to disk ({} entries)", entryCount);
    });
}

void HttpClient::loadManifestFromDisk() {
    if (!paimon::settings::general::enableDiskCache()) return;
    auto path = Mod::get()->getSaveDir() / "manifest_cache.json";

    auto readRes = geode::utils::file::readString(path);
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
        if (keyVal.isOk() && !keyVal.unwrap().empty()) {
            m_cdnBaseURL = keyVal.unwrap();
        }
    }

    for (auto& [key, val] : root) {
        if (!val.isObject()) continue;

        auto parsed = geode::utils::numFromString<int>(key);
        if (!parsed.isOk()) continue;
        int levelId = parsed.unwrap();
        if (levelId <= 0) continue;

        ManifestEntry entry;
        entry.format   = val["format"].asString().unwrapOr("");
        entry.cdnUrl   = val["cdnUrl"].asString().unwrapOr("");
        entry.version  = val["version"].asString().unwrapOr("");
        entry.id       = val["id"].asString().unwrapOr("");
        entry.revisionToken = val["revisionToken"].asString().unwrapOr("");
        entry.cachedAt = val["cachedAt"].asInt().unwrapOr(0);

        if (!entry.cdnUrl.empty()) {
            m_manifestCache[levelId] = std::move(entry);
            count++;
        }
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

void HttpClient::downloadThumbnail(int levelId, bool isGif, DownloadCallback callback) {
    downloadThumbnail(levelId, callback);
}

void HttpClient::downloadThumbnail(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] downloadThumbnail para level {} (formato unico, sin extension)", levelId);

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

        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders,
            [this, levelId](bool success, std::vector<uint8_t> const& data) {
            if (success && !data.empty()) {
                PaimonDebug::log("[HttpClient] CDN download success for level {}: {} bytes", levelId, data.size());
                resolveInflight(levelId, true, data);
            } else {
                // CDN miss: fall back to the Worker without invalidating the manifest.
                PaimonDebug::warn("[HttpClient] CDN download failed for level {}, falling back to Worker", levelId);

                if (isWorkerExhausted()) {
                    // Worker exhaustion is transient; let ThumbnailLoader retry.
                    PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fallback for level {} (will retry later)", levelId);
                    resolveInflight(levelId, false, {});
                    return;
                }

                auto headers = std::vector<std::string>{
                    "X-API-Key: " + m_apiKey,
                    "Connection: keep-alive"
                };
                std::string url = m_serverURL + "/t/" + std::to_string(levelId);

                performBinaryRequest(url, headers, [this, levelId](bool ws, std::vector<uint8_t> const& wd) {
                    if (ws && !wd.empty()) {
                        PaimonDebug::log("[HttpClient] Worker fallback success for level {}: {} bytes", levelId, wd.size());
                        resolveInflight(levelId, true, wd);
                    } else {
                        PaimonDebug::warn("[HttpClient] No thumbnail found for level {} (CDN + Worker both failed)", levelId);
                        markThumbnailNotFound(levelId);
                        removeManifestEntry(levelId);
                        resolveInflight(levelId, false, {});
                    }
                });
            }
        }, 4 /* CDN timeout; fall back quickly if slow */);
        return;
    }

    // On a cold manifest, try CDN before the Worker.
    if (!m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/thumbnails/thumbnails/" + std::to_string(levelId) + ".webp";
        PaimonDebug::log("[HttpClient] Manifest miss for level {}, trying CDN best-effort first: {}", levelId, cdnUrl);

        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        performBinaryRequest(cdnUrl, cdnHeaders,
            [this, levelId](bool success, std::vector<uint8_t> const& data) {
            if (success && !data.empty()) {
                PaimonDebug::log("[HttpClient] CDN best-effort success for level {}: {} bytes", levelId, data.size());
                resolveInflight(levelId, true, data);
            } else {
                PaimonDebug::warn("[HttpClient] CDN best-effort failed for level {} (may not exist), falling back to Worker", levelId);

                if (isWorkerExhausted()) {
                    PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fallback for level {} (will retry later)", levelId);
                    resolveInflight(levelId, false, {});
                    return;
                }

                auto headers = std::vector<std::string>{
                    "X-API-Key: " + m_apiKey,
                    "Connection: keep-alive"
                };
                std::string url = m_serverURL + "/t/" + std::to_string(levelId);

                performBinaryRequest(url, headers, [this, levelId](bool ws, std::vector<uint8_t> const& wd) {
                    if (ws && !wd.empty()) {
                        PaimonDebug::log("[HttpClient] Worker fallback success for level {}: {} bytes", levelId, wd.size());
                        resolveInflight(levelId, true, wd);
                    } else {
                        PaimonDebug::warn("[HttpClient] No thumbnail found for level {} (CDN + Worker both failed)", levelId);
                        markThumbnailNotFound(levelId);
                        resolveInflight(levelId, false, {});
                    }
                });
            }
        }, 4 /* CDN timeout; jump to Worker if slow */);
        return;
    }

    PaimonDebug::log("[HttpClient] Manifest miss for level {}, using Worker fallback", levelId);

    auto headers = std::vector<std::string>{
        "X-API-Key: " + m_apiKey,
        "Connection: keep-alive"
    };

    std::string url = m_serverURL + "/t/" + std::to_string(levelId);

    performBinaryRequest(url, headers, [this, levelId](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Found thumbnail for level {}", levelId);
            resolveInflight(levelId, true, data);
        } else {
            PaimonDebug::warn("[HttpClient] No thumbnail found for level {}", levelId);
            markThumbnailNotFound(levelId);
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
    PaimonDebug::log("[HttpClient] Checking moderator status for user: {} id:{}", username, accountID);

    // Coalesce concurrent checks for the same user.
    std::string key = username + "#" + std::to_string(accountID);
    {
        std::lock_guard<std::mutex> lock(m_inflightModMutex);
        auto it = m_inflightModChecks.find(key);
        if (it != m_inflightModChecks.end()) {
            PaimonDebug::log("[HttpClient] Moderator check already in-flight for {}, coalescing callback", key);
            it->second.push_back(std::move(callback));
            return;
        }
        m_inflightModChecks[key].push_back(std::move(callback));
    }

    std::string url = m_serverURL + "/api/moderator/check?username=" + encodeQueryParam(username);
    if (accountID > 0) url += "&accountID=" + std::to_string(accountID);

    PaimonDebug::log("[HttpClient] Moderator check URL: {}", url);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };

    performRequest(url, "GET", "", headers, [this, key, username, accountID](bool success, std::string const& response) {
        if (paimon::isRuntimeShuttingDown()) {
            resolveModCheckInflight(key, false, false);
            return;
        }
        bool isMod = false;
        bool isAdmin = false;
        bool isVip = false;

        if (success) {
            auto jsonRes = matjson::parse(response);
            if (jsonRes.isOk()) {
                auto json = jsonRes.unwrap();
                if (json.contains("isModerator")) {
                    isMod = json["isModerator"].asBool().unwrapOr(false);
                }
                if (json.contains("isAdmin")) {
                    isAdmin = json["isAdmin"].asBool().unwrapOr(false);
                }
                if (json.contains("isVip")) {
                    isVip = json["isVip"].asBool().unwrapOr(false);
                }
                Mod::get()->setSavedValue<bool>("gd-verification-failed", false);
                if (json.contains("newModCode")) {
                    std::string newCode = json["newModCode"].asString().unwrapOr("");
                    if (!newCode.empty()) {
                        HttpClient::get().setModCode(newCode);
                        PaimonDebug::log("[HttpClient] Received and saved new moderator code (prefijo: {}...)", newCode.substr(0, 8));
                    } else {
                        log::warn("[HttpClient] Server respondio newModCode vacio para {}#{}", username, accountID);
                    }
                } else if (isMod || isAdmin) {
                    bool gdFailed = false;
                    if (json.contains("gdVerificationFailed")) {
                        gdFailed = json["gdVerificationFailed"].asBool().unwrapOr(false);
                    }
                    if (gdFailed) {
                        log::warn("[HttpClient] Mod/admin {}#{} verificado pero GDBrowser fallo - no se pudo generar mod-code. Reintenta mas tarde.", username, accountID);
                        Mod::get()->setSavedValue<bool>("gd-verification-failed", true);
                    } else {
                        log::warn("[HttpClient] Server NO devolvio newModCode para mod/admin {}#{}. El mod-code actual puede estar desactualizado.", username, accountID);
                        Mod::get()->setSavedValue<bool>("gd-verification-failed", false);
                    }
                }
            } else {
                PaimonDebug::warn("[HttpClient] JSON parse failed in moderator check, falling back to string search");
                isMod = response.find("\"isModerator\":true") != std::string::npos || response.find("\"isModerator\": true") != std::string::npos;
                isAdmin = response.find("\"isAdmin\":true") != std::string::npos || response.find("\"isAdmin\": true") != std::string::npos;
                isVip = response.find("\"isVip\":true") != std::string::npos || response.find("\"isVip\": true") != std::string::npos;
            }

            if (isAdmin) {
                isMod = true;
            }

            Mod::get()->setSavedValue<bool>("is-verified-vip", isVip);
            PaimonDebug::log("[HttpClient] User {}#{} => moderator: {}, admin: {}, vip: {}", username, accountID, isMod, isAdmin, isVip);
        } else {
            log::error("[HttpClient] Failed secure moderator check for {}#{}: {}", username, accountID, response);
            log::error("[HttpClient] Server URL: {}", m_serverURL);
            if (response.find("401") != std::string::npos) {
                log::error("[HttpClient] HTTP 401 = API key mismatch. Expected key may differ from server.");
            }
            if (response.find("429") != std::string::npos) {
                int retryAfter = 0;
                auto pos = response.find("\"retryAfter\":");
                if (pos != std::string::npos) {
                    auto numStart = pos + 13;
                    auto numEnd = response.find_first_not_of("0123456789", numStart);
                    if (numEnd != numStart) {
                        auto parsed = geode::utils::numFromString<int>(response.substr(numStart, numEnd - numStart));
                        if (parsed.isOk()) retryAfter = parsed.unwrap();
                    }
                }
                int backoff = std::max(retryAfter, 10);
                PaimonDebug::warn("[HttpClient] Moderator check rate-limited, backing off {}s", backoff);
            }
        }

        resolveModCheckInflight(key, isMod, isAdmin);
    });
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
    PaimonDebug::log("[HttpClient] resolveModCheckInflight {}: {} callbacks", key, callbacks.size());
    for (auto& cb : callbacks) {
        cb(isMod, isAdmin);
    }
}

void HttpClient::getBanList(BanListCallback callback) {
    PaimonDebug::log("[HttpClient] Getting ban list");
    std::string reqUser = getSafeAccountUsername();
    int reqAccountID = getSafeAccountID();
    // URL-encode usernames to prevent parameter injection.
    std::string url = m_serverURL + "/api/admin/banlist?username=" + encodeQueryParam(reqUser)
        + "&accountID=" + std::to_string(reqAccountID);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    // Omit empty X-Mod-Code; the server treats it as invalid auth.
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
    }
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::checkBanned(BanCheckCallback callback) {
    std::string reqUser = getSafeAccountUsername();
    int reqAccountID = getSafeAccountID();
    std::string url = m_serverURL + "/api/banned?username=" + encodeQueryParam(reqUser)
        + "&accountID=" + std::to_string(reqAccountID);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (!success) {
            if (callback) callback(false, false, "");
            return;
        }
        auto res = matjson::parse(response);
        if (!res.isOk()) {
            if (callback) callback(false, false, "");
            return;
        }
        auto json = res.unwrap();
        bool banned = json.contains("banned") && json["banned"].asBool().unwrapOr(false);
        std::string reason = json.contains("reason") ? json["reason"].asString().unwrapOr("") : "";
        if (callback) callback(true, banned, reason);
    }, false);
}

void HttpClient::banUser(std::string const& username, std::string const& reason, BanUserCallback callback) {
    std::string url = m_serverURL + "/api/admin/ban";
    std::string adminUser = getSafeAccountUsername();
    int accountID = getSafeAccountID();

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"reason", reason},
        {"admin", adminUser},
        {"adminUser", adminUser},
        {"accountID", accountID}
    });
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, [callback = std::move(callback)](bool success, std::string const& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::unbanUser(std::string const& username, BanUserCallback callback) {
    std::string url = m_serverURL + "/api/admin/unban";
    std::string adminUser = getSafeAccountUsername();
    int accountID = getSafeAccountID();

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", accountID}
    });
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, [callback = std::move(callback)](bool success, std::string const& resp) {
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

void HttpClient::addRoleMember(std::string const& role, std::string const& username, int targetAccountID, GenericCallback callback) {
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
    postWithAuth("/api/admin/add-role", json.dump(), std::move(callback));
}

void HttpClient::removeRoleMember(std::string const& role, std::string const& username, int targetAccountID, GenericCallback callback) {
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
    postWithAuth("/api/admin/remove-role", json.dump(), std::move(callback));
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

void HttpClient::getTopCreators(GenericCallback callback) {
    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/system/public/api/top-creators.json";
        std::vector<std::string> headers = { "Accept: application/json" };
        performRequest(cdnUrl, "GET", "", headers, callback, false);
        return;
    }
    std::string url = m_serverURL + "/api/top-creators?limit=100";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::getTopThumbnails(GenericCallback callback) {
    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        std::string cdnUrl = m_cdnBaseURL + "/system/public/api/top-thumbnails.json";
        std::vector<std::string> headers = { "Accept: application/json" };
        performRequest(cdnUrl, "GET", "", headers, callback, false);
        return;
    }
    std::string url = m_serverURL + "/api/top-thumbnails?limit=100";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback, false);
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

void HttpClient::downloadFromUrl(std::string const& url, DownloadCallback callback) {
    if (!isUrlSafe(url)) {
        PaimonDebug::log("[HttpClient] Blocked unsafe URL: {}", url);
        if (callback) callback(false, {}, 0, 0);
        return;
    }

    // Do not send the API key to arbitrary hosts.
    std::vector<std::string> headers;
    if (url.find(m_serverURL) == 0 || url.find("api.flozwer.org") != std::string::npos) {
        headers.push_back("X-API-Key: " + m_apiKey);
    }
    performBinaryRequest(url, headers, [callback = std::move(callback)](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            callback(true, data, 0, 0);
        } else {
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadFromUrlRaw(std::string const& url, DownloadCallback callback) {
    if (!isUrlSafe(url)) {
        PaimonDebug::log("[HttpClient] Blocked unsafe URL: {}", url);
        if (callback) callback(false, {}, 0, 0);
        return;
    }
    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(30));
    req.acceptEncoding("gzip, deflate");

    if (url.find(m_serverURL) == 0 || url.find("api.flozwer.org") != std::string::npos) {
        req.header("X-API-Key", m_apiKey);
    }

    std::string urlCopy = url;

    auto callbackGate = m_callbackGate;
    WebHelper::dispatch(std::move(req), "GET", url, [callbackGate, callback, urlCopy](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        bool success = res.ok();
        std::vector<uint8_t> data = success ? res.data() : std::vector<uint8_t>{};

        int statusCode = res.code();
        PaimonDebug::log("[HttpClient] Raw binary GET {} -> status={}, size={}", urlCopy, statusCode, data.size());

        if (success && !data.empty()) {
            auto ct = res.header("Content-Type");
            std::string contentType = ct.has_value() ? std::string(ct.value()) : "";

            if (contentType.find("application/json") != std::string::npos ||
                contentType.find("text/html") != std::string::npos) {
                std::string body(data.begin(), data.begin() + std::min(data.size(), (size_t)500));
                PaimonDebug::log("[HttpClient] Raw binary request got error response: {}", body);
                success = false;
                data.clear();
            }
        }

        if (callback) {
            if (success && !data.empty()) {
                callback(true, data, 0, 0);
            } else {
                callback(false, {}, 0, 0);
            }
        }
    });
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
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
        PaimonDebug::log("[HttpClient] get with mod-code (prefijo: {}...)", m_modCode.substr(0, 8));
    }
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::getProfileStats(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Fetching profile stats for account {}", accountID);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        // Profile stats are private; there is no CDN fallback.
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

    performRequest(url, "POST", body.dump(), headers, [this, callback = std::move(callback)](bool success, std::string const& response) {
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

        if (json.contains("moderator") && json["moderator"].isObject()) {
            auto& mod = json["moderator"];
            result.isModerator = mod["isModerator"].asBool().unwrapOr(false);
            result.isAdmin = mod["isAdmin"].asBool().unwrapOr(false);
            result.isVip = mod["isVip"].asBool().unwrapOr(false);
            result.newModCode = mod["newModCode"].asString().unwrapOr("");
            result.gdVerificationFailed = mod["gdVerificationFailed"].asBool().unwrapOr(false);

            if (result.isAdmin) result.isModerator = true;
        }

        if (!result.newModCode.empty()) {
            HttpClient::get().setModCode(result.newModCode);
            PaimonDebug::log("[HttpClient] Init: received new mod code (prefix: {}...)", result.newModCode.substr(0, 8));
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

        Mod::get()->setSavedValue<bool>("is-verified-vip", result.isVip);

        if (callback) callback(true, result);
    }, false);
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
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
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
    // Do not send an empty X-Mod-Code; let the server use its fallback.
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
        PaimonDebug::log("[HttpClient] postWithAuth con mod-code (prefijo: {}...)", m_modCode.substr(0, 8));
    } else {
        log::warn("[HttpClient] postWithAuth SIN mod-code (vacio). Server usara fallback GDBrowser.");
    }
    performRequest(url, "POST", data, headers, callback);
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
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode
    };
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
        "X-Mod-Code: " + m_modCode,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::addToWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback) {
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
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
    }
    performRequest(m_serverURL + "/api/whitelist/add", "POST", json.dump(), headers, callback);
}

void HttpClient::removeFromWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback) {
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
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
    }
    performRequest(m_serverURL + "/api/whitelist/remove", "POST", json.dump(), headers, callback);
}


bool HttpClient::isUrlSafe(std::string const& url) {
    if (url.empty()) return false;

    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.starts_with("file://") || lower.starts_with("ftp://") ||
        lower.starts_with("gopher://") || lower.starts_with("data:")) {
        return false;
    }

    if (!lower.starts_with("http://") && !lower.starts_with("https://")) {
        return false;
    }

    size_t hostStart = lower.find("://");
    if (hostStart == std::string::npos) return false;
    hostStart += 3;

    size_t atPos = lower.find('@', hostStart);
    size_t slashPos = lower.find('/', hostStart);
    if (atPos != std::string::npos && (slashPos == std::string::npos || atPos < slashPos)) {
        return false;
    }

    std::string hostPort = (slashPos != std::string::npos)
        ? lower.substr(hostStart, slashPos - hostStart)
        : lower.substr(hostStart);

    size_t colonPos = hostPort.rfind(':');
    std::string host = (colonPos != std::string::npos)
        ? hostPort.substr(0, colonPos)
        : hostPort;

    if (host.empty()) return false;

    if (host == "localhost" || host == "127.0.0.1" || host == "::1" ||
        host == "[::1]" || host == "0.0.0.0" || host == "[::]") {
        return false;
    }

    if (host.starts_with("127.")) {
        return false;
    }

    if (host.starts_with("0.")) {
        return false;
    }

    if (host.starts_with("10.") || host.starts_with("192.168.")) {
        return false;
    }

    if (host.starts_with("169.254.")) {
        return false;
    }
    if (host.starts_with("100.")) {
        size_t dot = host.find('.', 4);
        if (dot != std::string::npos) {
            std::string octet2Str = host.substr(4, dot - 4);
            auto parsed = geode::utils::numFromString<int>(octet2Str);
            if (parsed.isOk()) {
                int octet2 = parsed.unwrap();
                if (octet2 >= 64 && octet2 <= 127) return false;
            }
        }
    }
    if (host.starts_with("172.")) {
        size_t dot = host.find('.', 4);
        if (dot != std::string::npos) {
            std::string octet2Str = host.substr(4, dot - 4);
            auto parsed = geode::utils::numFromString<int>(octet2Str);
            if (parsed.isOk()) {
                int octet2 = parsed.unwrap();
                if (octet2 >= 16 && octet2 <= 31) return false;
            }
        }
    }

    auto hostNoBrackets = host;
    if (hostNoBrackets.size() >= 2 && hostNoBrackets.front() == '[' && hostNoBrackets.back() == ']') {
        hostNoBrackets = hostNoBrackets.substr(1, hostNoBrackets.size() - 2);
    }
    if (hostNoBrackets.starts_with("fe80:") || hostNoBrackets.starts_with("fe80::")) {
        return false;
    }
    if (hostNoBrackets.starts_with("fc") || hostNoBrackets.starts_with("fd")) {
        if (hostNoBrackets.size() >= 4 &&
            (hostNoBrackets[2] == ':' || hostNoBrackets.find(':') < 5)) {
            return false;
        }
    }
    if (hostNoBrackets.starts_with("ff0") || hostNoBrackets.starts_with("ff:") ||
        hostNoBrackets.starts_with("ff1") || hostNoBrackets.starts_with("ff2") ||
        hostNoBrackets.starts_with("ff3") || hostNoBrackets.starts_with("ff4") ||
        hostNoBrackets.starts_with("ff5") || hostNoBrackets.starts_with("ff6") ||
        hostNoBrackets.starts_with("ff7") || hostNoBrackets.starts_with("ff8") ||
        hostNoBrackets.starts_with("ff9") || hostNoBrackets.starts_with("ffa") ||
        hostNoBrackets.starts_with("ffb") || hostNoBrackets.starts_with("ffc") ||
        hostNoBrackets.starts_with("ffd") || hostNoBrackets.starts_with("ffe") ||
        hostNoBrackets.starts_with("fff")) {
        return false;
    }
    if (hostNoBrackets.starts_with("::ffff:") || hostNoBrackets.starts_with("::FFFF:")) {
        std::string v4 = hostNoBrackets.substr(7);
        if (v4 == "127.0.0.1" || v4.starts_with("127.") || v4.starts_with("10.") ||
            v4.starts_with("192.168.") || v4.starts_with("169.254.") || v4 == "0.0.0.0") {
            return false;
        }
    }

    return true;
}

void HttpClient::uploadCustomBadge(int accountID, std::string const& emoteName, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading custom badge for account {}: {}", accountID, emoteName);

    std::string url = m_serverURL + "/api/profile/badge";

    web::MultipartForm form;
    form.param("accountID", std::to_string(accountID));
    form.param("emoteName", emoteName);

    auto req = web::WebRequest();
    req.acceptEncoding("gzip, deflate");
    req.header("X-API-Key", m_apiKey);
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

    std::string url = m_serverURL + "/api/profile/badge/delete";

    web::MultipartForm form;
    form.param("accountID", std::to_string(accountID));

    auto req = web::WebRequest();
    req.acceptEncoding("gzip, deflate");
    req.header("X-API-Key", m_apiKey);
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
                if (decodeBase64(b64, bytes) && !bytes.empty()) {
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

void HttpClient::downloadThumbnailsBatch(std::vector<int> const& levelIds,
                                         BatchDownloadCallback callback) {
    if (levelIds.empty()) {
        if (callback) callback(true, {});
        return;
    }
    if (isWorkerExhausted()) {
        PaimonDebug::warn("[HttpClient] Worker exhausted, batch thumbnails skipped");
        if (callback) callback(false, {});
        return;
    }

    static constexpr size_t MAX_BATCH = 40;
    std::string body = buildBatchIdsJson("ids", levelIds, MAX_BATCH);
    std::string url = m_serverURL + "/api/thumbnails/batch";

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json"
    };

    PaimonDebug::log("[HttpClient] downloadThumbnailsBatch: {} ids -> {}", levelIds.size(), url);

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

void HttpClient::getThumbnailsBatch(std::vector<int> const& levelIds, BatchListCallback callback) {
    if (levelIds.empty()) {
        if (callback) callback(true, {});
        return;
    }

    static constexpr size_t MAX_BATCH = 40;
    std::string body = buildBatchIdsJson("ids", levelIds, MAX_BATCH);
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
    if (accountIDs.empty()) {
        if (callback) callback(true, {});
        return;
    }
    if (isWorkerExhausted()) {
        if (callback) callback(false, {});
        return;
    }

    static constexpr size_t MAX_BATCH = 40;
    std::string body = buildBatchIdsJson("accountIDs", accountIDs, MAX_BATCH);
    std::string url = m_serverURL + "/api/profilebackground/batch";

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json"
    };

    PaimonDebug::log("[HttpClient] downloadProfileBackgroundsBatch: {} ids -> {}", accountIDs.size(), url);

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

void HttpClient::downloadProfileImgsBatch(std::vector<int> const& accountIDs,
                                          BatchDownloadCallback callback) {
    if (accountIDs.empty()) {
        if (callback) callback(true, {});
        return;
    }
    if (isWorkerExhausted()) {
        if (callback) callback(false, {});
        return;
    }

    static constexpr size_t MAX_BATCH = 40;
    std::string body = buildBatchIdsJson("accountIDs", accountIDs, MAX_BATCH);
    std::string url = m_serverURL + "/api/profileimgs/batch";

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json"
    };

    PaimonDebug::log("[HttpClient] downloadProfileImgsBatch: {} ids -> {}", accountIDs.size(), url);

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
