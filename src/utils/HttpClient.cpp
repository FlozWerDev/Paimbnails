#include "HttpClient.hpp"
#include "Debug.hpp"
#include "WebHelper.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../core/Settings.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <matjson.hpp>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <filesystem>

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
} // namespace

HttpClient::HttpClient() {
    // config base del server
    m_serverURL = "https://api.flozwer.org";
    m_forumServerURL = "https://paimbnailspost.onrender.com";

    // Public client identifier used by the backend for shared mod/bot access.
    // This value is intentionally shipped with the client, so persisting it to
    // saved values is unnecessary.
    m_apiKey = "074b91c9-6631-4670-a6f08a2ce970-0183-471b";

    // cargo el mod code que tenga guardado
    m_modCode = Mod::get()->getSavedValue<std::string>("mod-code", "");
    m_callbackGate = std::make_shared<std::atomic<bool>>(true);

    // hydrate manifest cache from disk (avoids Worker requests on restart)
    loadManifestFromDisk();

    PaimonDebug::log("[HttpClient] Initialized with server: {}", m_serverURL);
    PaimonDebug::log("[HttpClient] Forum server: {}", m_forumServerURL);
}

void HttpClient::cleanTasks() {
    // Must be called on the main thread only.
    // Old callbacks captured the previous shared_ptr and will see `false`;
    // new requests will capture the fresh gate set to `true`.
    if (m_callbackGate) {
        m_callbackGate->store(false, std::memory_order_release);
    }
    m_callbackGate = std::make_shared<std::atomic<bool>>(true);

    // Limpiar failed cache y cooldown global para que los thumbnails se
    // puedan reintentar en la nueva pantalla/pagina. Esto evita que
    // errores transitorios (rate-limit, timeout) persistan entre navegaciones.
    ThumbnailLoader::get().clearFailedCache();
}

void HttpClient::setServerURL(std::string const& url) {
    m_serverURL = url;
    if (!m_serverURL.empty() && m_serverURL.back() == '/') {
        m_serverURL.pop_back();
    }
    PaimonDebug::log("[HttpClient] Server URL updated to: {}", m_serverURL);
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

    // pongo headers
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);

            if (key == "X-Mod-Code" || key == "x-mod-code") {
                hasExplicitModCodeHeader = true;
            }
        }
    }

    if (includeStoredModCode && !hasExplicitModCodeHeader && !m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    if (method == "POST" && !postData.empty()) {
        req.bodyString(postData);
    }

    WebHelper::dispatch(std::move(req), method, url, [callbackGate, callback, this](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        bool success = res.ok();
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

        // Detect Worker quota exhaustion (CF 1015 / 503) but NOT per-IP rate limit (429 RATE_LIMITED).
        // A 429 with code=RATE_LIMITED is a 60-second sliding-window limit — transient.
        // Marking the Worker exhausted on a transient 429 would disable the mod for 1 hour,
        // which is the root cause of "works fine after restarting the game".
        if (!success && res.code() == 503) {
            markWorkerExhausted();
        } else if (!success && res.code() == 429) {
            // Only treat as exhaustion if it's a CF-level quota error, not our own rate limiter
            std::string body = res.string().unwrapOr("");
            bool isAppRateLimit = body.find("RATE_LIMITED") != std::string::npos;
            if (!isAppRateLimit) {
                markWorkerExhausted();
            }
        }

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

    // Prefer WebP for smaller download size / faster decode, fall back to PNG/GIF
    req.header("Accept", "image/webp,image/png,image/gif,*/*");

    // meto headers
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }

    if (includeModCode && !m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    std::string urlCopy = url; // pa logs

    WebHelper::dispatch(std::move(req), "GET", url, [callbackGate, callback, urlCopy](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        bool success = res.ok();
        std::vector<uint8_t> data = success ? res.data() : std::vector<uint8_t>{};

        int statusCode = res.code();
        PaimonDebug::log("[HttpClient] Binary GET {} -> status={}, size={}", urlCopy, statusCode, data.size());

        // Check Content-Type: if server returned JSON/HTML error, treat as failure
        if (success && !data.empty()) {
            auto ct = res.header("Content-Type");
            std::string contentType = ct.has_value() ? std::string(ct.value()) : "";
            PaimonDebug::log("[HttpClient] Binary response Content-Type: {}", contentType);

            // If content-type is JSON or HTML, it's an error response, not binary data
            if (contentType.find("application/json") != std::string::npos ||
                contentType.find("text/html") != std::string::npos) {
                std::string body(data.begin(), data.begin() + std::min(data.size(), (size_t)500));
                PaimonDebug::log("[HttpClient] Binary request got non-image response: {}", body);
                success = false;
                data.clear();
            }

            // Also validate magic bytes: PNG, JPEG, GIF, WEBP, BMP
            if (success && data.size() >= 4) {
                bool validImage = false;
                // PNG: 89 50 4E 47
                if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) validImage = true;
                // JPEG: FF D8 FF
                else if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) validImage = true;
                // GIF: GIF87a or GIF89a
                else if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8'
                    && (data[4] == '7' || data[4] == '9') && data[5] == 'a') validImage = true;
                // WEBP: RIFF....WEBP
                else if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
                    && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') validImage = true;
                // BMP: BM
                else if (data[0] == 'B' && data[1] == 'M') validImage = true;
                // MP4/ISO BMFF: ftyp box at offset 4
                else if (data.size() >= 8 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') validImage = true;

                if (!validImage) {
                    std::string preview(data.begin(), data.begin() + std::min(data.size(), (size_t)200));
                    PaimonDebug::log("[HttpClient] Binary response does not look like an image. First bytes: {}", preview);
                    success = false;
                    data.clear();
                }
            }
        }

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
    // uso el MultipartForm de geode v5
    web::MultipartForm form;

    // meto los campos del form
    for (auto const& field : formFields) {
        form.param(field.first, field.second);
    }
    
    // agrego el archivo
    form.file(fieldName, std::span<uint8_t const>(data), filename, fileContentType);

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(30));
    req.acceptEncoding("gzip, deflate");

    // aplico headers — only include X-Mod-Code if explicitly passed in headers array
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }

    // mando body multipart de geode
    req.bodyMultipart(form);

    WebHelper::dispatch(std::move(req), "POST", url, [callbackGate, callback](web::WebResponse res) {
        if (!callbackGate || !callbackGate->load(std::memory_order_acquire)) {
            return;
        }
        bool success = res.ok();
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

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
    // No X-Mod-Code for profile uploads — regular users upload to pending queue

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
                // parse response to detect pending verification
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
    // No X-Mod-Code for profile GIF uploads

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
    // No X-Mod-Code for profile video uploads

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

    // deducir extension del content type
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
    // No X-Mod-Code for profile image uploads — regular users upload to pending queue

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
                // parse response to detect pending verification
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

    // When Worker is exhausted, try CDN with common profile img path pattern
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

    // uso MultipartForm de Geode v5
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
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

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

    // When Worker is exhausted, try CDN with common profile path pattern
    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        // Profile backgrounds use resolveLatestKey on Worker — best-effort CDN path
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

    // descargar desde /profilebackground/ (endpoint dedicado, separado de thumbnails)

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

    // armo el JSON: {"accountIDs":[123,456,...]}
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

void HttpClient::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
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

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    // Include X-Mod-Code if available — moderators upload directly, regular users go to pending queue
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

void HttpClient::uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
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

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    // Include X-Mod-Code if available — moderators upload directly, regular users go to pending queue
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

void HttpClient::uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback) {
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

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    // Include X-Mod-Code if available — moderators upload directly, regular users go to pending queue
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

void HttpClient::uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
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
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    // No X-Mod-Code for suggestion uploads
    
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

void HttpClient::uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
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
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    // No X-Mod-Code for update uploads
    
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

    // CDN fallback when Worker is exhausted
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

    // CDN fallback when Worker is exhausted
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

// ── Manifest cache ─────────────────────────────────────────────

void HttpClient::fetchManifest(std::vector<int> const& levelIds, std::function<void(bool)> callback) {
    if (levelIds.empty()) {
        if (callback) callback(false);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_manifestFetchMutex);
        // Circuit breaker: si estamos en cooldown tras un 429, rechazar inmediatamente
        if (isManifestCooldownActive()) {
            PaimonDebug::log("[HttpClient] fetchManifest rejected: cooldown active");
            if (callback) callback(false);
            return;
        }
        // Coalescing: si ya hay un fetch en vuelo, solo encolar el callback
        if (m_manifestFetchInFlight) {
            m_manifestPendingCallbacks.push_back(std::move(callback));
            PaimonDebug::log("[HttpClient] fetchManifest coalesced: {} pending callbacks",
                m_manifestPendingCallbacks.size());
            return;
        }
        m_manifestFetchInFlight = true;
        // El callback original también va a la cola para ejecutarse junto con los pending
        if (callback) m_manifestPendingCallbacks.push_back(std::move(callback));
    }

    // build comma-separated ids query param
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

            // Detectar 429 y parsear retryAfter del body JSON para activar cooldown
            if (!success && response.find("429") != std::string::npos) {
                hadRateLimit = true;
                // Parsear retryAfter del JSON: buscar "retryAfter":55
                auto pos = response.find("\"retryAfter\":");
                if (pos != std::string::npos) {
                    auto numStart = pos + 13; // len("\"retryAfter\":")
                    auto numEnd = response.find_first_not_of("0123456789", numStart);
                    if (numEnd != numStart) {
                        auto parsed = geode::utils::numFromString<int>(response.substr(numStart, numEnd - numStart));
                        if (parsed.isOk()) retryAfter = parsed.unwrap();
                    }
                }
            }
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

    // Extract CDN Pull Zone base URL for fallback downloads
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
            // Detect revision changes: if we had an entry before and the token changed,
            // the thumbnail data on the server is different from what we cached.
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

    // prune if over limit — erase arbitrary entries to stay within bounds
    while (m_manifestCache.size() > MAX_MANIFEST_ENTRIES) {
        m_manifestCache.erase(m_manifestCache.begin());
    }

    return changedIds;
}

std::optional<HttpClient::ManifestEntry> HttpClient::getManifestEntry(int levelId) {
    std::lock_guard<std::mutex> lock(m_manifestMutex);
    auto it = m_manifestCache.find(levelId);
    if (it != m_manifestCache.end()) {
        // TTL check: expire entries older than 24 hours
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
    // persistir la eliminacion al disco para que un reinicio no recargue la URL CDN vieja
    saveManifestToDisk();
}

void HttpClient::removeExistsEntry(int levelId) {
    m_existsCache.erase(levelId);
}

void HttpClient::saveManifestToDisk() {
    if (!paimon::settings::general::enableDiskCache()) return;
    std::lock_guard<std::mutex> lock(m_manifestMutex);

    if (m_manifestCache.empty()) return;

    auto path = Mod::get()->getSaveDir() / "manifest_cache.json";

    matjson::Value root = matjson::Value::object();
    // Persist CDN Pull Zone base URL for fallback downloads
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

    std::string json = root.dump(matjson::NO_INDENTATION);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    // writeStringSafe does atomic tmp → rename internally
    auto writeRes = geode::utils::file::writeStringSafe(path, json);
    if (writeRes.isErr()) {
        PaimonDebug::warn("[HttpClient] Failed to write manifest_cache.json: {}", writeRes.unwrapErr());
        return;
    }
    PaimonDebug::log("[HttpClient] Manifest saved to disk ({} entries)", m_manifestCache.size());
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

    // Load CDN Pull Zone base URL for fallback downloads
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

    // prune if over limit
    while (m_manifestCache.size() > MAX_MANIFEST_ENTRIES) {
        m_manifestCache.erase(m_manifestCache.begin());
    }
}

void HttpClient::downloadReported(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading reported thumbnail for level {}", levelId);
    downloadThumbnail(levelId, callback);
}

void HttpClient::downloadThumbnail(int levelId, bool isGif, DownloadCallback callback) {
    // Unificado: ambos formatos (GIF y estatico) usan el mismo pipeline
    // con manifest/CDN + Worker fallback. El servidor retorna el formato
    // correcto automaticamente y el cliente lo detecta por magic bytes.
    downloadThumbnail(levelId, callback);
}

void HttpClient::downloadThumbnail(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] downloadThumbnail para level {} (formato unico, sin extension)", levelId);

    // ── In-flight dedup: si ya hay un download activo para este level,
    //    solo encolamos el callback y esperamos el resultado compartido.
    {
        std::lock_guard<std::mutex> lock(m_inflightMutex);
        auto it = m_inflightDownloads.find(levelId);
        if (it != m_inflightDownloads.end()) {
            PaimonDebug::log("[HttpClient] download already in-flight for level {}, coalescing callback", levelId);
            it->second.push_back(std::move(callback));
            return;
        }
        // Primera request — registrar en el mapa con este callback
        m_inflightDownloads[levelId].push_back(std::move(callback));
    }

    // Check manifest cache first — use Bunny CDN Pull Zone URL if available
    auto manifestEntry = getManifestEntry(levelId);
    if (manifestEntry.has_value() && !manifestEntry->cdnUrl.empty()) {
        // cache-buster: agregar _pv=<version> para evitar que el CDN sirva contenido viejo
        std::string cdnUrl = manifestEntry->cdnUrl;
        if (!manifestEntry->version.empty()) {
            cdnUrl += (cdnUrl.find('?') != std::string::npos ? "&" : "?");
            cdnUrl += "_pv=" + manifestEntry->version;
        }
        PaimonDebug::log("[HttpClient] Manifest hit for level {}: CDN URL={}", levelId, cdnUrl);

        // Pull Zone URLs are public — no auth headers needed
        std::vector<std::string> cdnHeaders = { "Connection: keep-alive" };
        // CDN timeout corto (6s) — si falla, saltar rapido al Worker
        performBinaryRequest(cdnUrl, cdnHeaders,
            [this, levelId](bool success, std::vector<uint8_t> const& data) {
            if (success && !data.empty()) {
                PaimonDebug::log("[HttpClient] CDN download success for level {}: {} bytes", levelId, data.size());
                resolveInflight(levelId, true, data);
            } else {
                // CDN URL stale or unreachable — fall back to Worker (NO invalidar manifest
                // porque puede ser un problema temporal del CDN, no de la URL)
                PaimonDebug::warn("[HttpClient] CDN download failed for level {}, falling back to Worker", levelId);

                if (isWorkerExhausted()) {
                    // Worker exhausted: no fallback available, fail gracefully
                    PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fallback for level {}", levelId);
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
                        // Ambos fallaron — ahora si invalidar manifest para la proxima vez
                        PaimonDebug::warn("[HttpClient] No thumbnail found for level {} (CDN + Worker both failed)", levelId);
                        removeManifestEntry(levelId);
                        resolveInflight(levelId, false, {});
                    }
                });
            }
        }, 4 /* CDN timeout 4s — fast fallback to Worker if CDN is slow */);
        return;
    }

    // No manifest entry — try CDN best-effort first (fast path), then Worker fallback.
    // Bunny CDN responds in ~20-50ms vs Worker ~200-500ms, so CDN-first saves latency
    // even when the manifest is cold. A 404 from CDN is also fast (~50ms).
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
                    PaimonDebug::warn("[HttpClient] Worker exhausted, cannot fallback for level {}", levelId);
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
                        resolveInflight(levelId, false, {});
                    }
                });
            }
        }, 4 /* fast CDN timeout — if CDN is slow, jump to Worker quickly */);
        return;
    }

    // Fallback: use Worker endpoint /t/{levelId}
    PaimonDebug::log("[HttpClient] Manifest miss for level {}, using Worker fallback", levelId);

    auto headers = std::vector<std::string>{
        "X-API-Key: " + m_apiKey,
        "Connection: keep-alive"
    };

    // Single request: /t/{levelId} without extension — server auto-detects format.
    // Client handles all formats (GIF/WebP/PNG/JPG) via magic bytes in bytesToTexture().
    std::string url = m_serverURL + "/t/" + std::to_string(levelId);

    performBinaryRequest(url, headers, [this, levelId](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Found thumbnail for level {}", levelId);
            resolveInflight(levelId, true, data);
        } else {
            PaimonDebug::warn("[HttpClient] No thumbnail found for level {}", levelId);
            resolveInflight(levelId, false, {});
        }
    });
}

bool HttpClient::isWorkerExhausted() {
    if (!m_workerExhausted.load(std::memory_order_acquire)) return false;
    // Auto-recovery: reset after EXHAUSTED_RECOVERY_SECONDS
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - m_exhaustedAt > EXHAUSTED_RECOVERY_SECONDS) {
        m_workerExhausted.store(false, std::memory_order_release);
        PaimonDebug::log("[HttpClient] Worker exhaustion reset after {}s recovery period", EXHAUSTED_RECOVERY_SECONDS);
        return false;
    }
    return true;
}

void HttpClient::markWorkerExhausted() {
    if (m_workerExhausted.load(std::memory_order_acquire)) return; // already marked
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
    for (auto& cb : callbacks) {
        cb(success, data, 0, 0);
    }
}

void HttpClient::checkThumbnailExists(int levelId, CheckCallback callback) {
    // Fast path: if manifest has an entry, thumbnail definitely exists — no network needed
    auto manifestEntry = getManifestEntry(levelId);
    if (manifestEntry.has_value() && !manifestEntry->cdnUrl.empty()) {
        PaimonDebug::log("[HttpClient] checkExists: manifest hit for level {} — skipping network", levelId);
        callback(true);
        return;
    }

    time_t now = std::time(nullptr);
    auto cacheIt = m_existsCache.find(levelId);
    if (cacheIt != m_existsCache.end()) {
        if (now - cacheIt->second.timestamp < EXISTS_CACHE_DURATION) {
            callback(cacheIt->second.exists);
            return;
        } else {
            m_existsCache.erase(cacheIt);
        }
    }

    // If Worker is exhausted, skip the network check — assume unknown (false)
    if (isWorkerExhausted()) {
        PaimonDebug::warn("[HttpClient] Worker exhausted, skipping exists check for level {}", levelId);
        callback(false);
        return;
    }
    
    std::string url = m_serverURL + "/api/exists?levelId=" + std::to_string(levelId) + "&path=thumbnails";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    
    performRequest(url, "GET", "", headers, [this, callback, levelId, now](bool success, std::string const& response) {
        if (success) {
            bool exists = response.find("\"exists\":true") != std::string::npos || 
                          response.find("\"exists\": true") != std::string::npos;
            
            m_existsCache[levelId] = {exists, now};
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

    // ── In-flight dedup: coalesce concurrent checks for the same user ──
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
                // guardo el nuevo mod code si viene del server
                Mod::get()->setSavedValue<bool>("gd-verification-failed", false);
                if (json.contains("newModCode")) {
                    std::string newCode = json["newModCode"].asString().unwrapOr("");
                    if (!newCode.empty()) {
                        this->setModCode(newCode);
                        PaimonDebug::log("[HttpClient] Received and saved new moderator code (prefijo: {}...)", newCode.substr(0, 8));
                    } else {
                        log::warn("[HttpClient] Server respondio newModCode vacio para {}#{}", username, accountID);
                    }
                } else if (isMod || isAdmin) {
                    // Check if GDBrowser verification failed
                    bool gdFailed = false;
                    if (json.contains("gdVerificationFailed")) {
                        gdFailed = json["gdVerificationFailed"].asBool().unwrapOr(false);
                    }
                    if (gdFailed) {
                        log::warn("[HttpClient] Mod/admin {}#{} verificado pero GDBrowser fallo — no se pudo generar mod-code. Reintenta mas tarde.", username, accountID);
                        // Store gdVerificationFailed as saved value so UI can detect it
                        Mod::get()->setSavedValue<bool>("gd-verification-failed", true);
                    } else {
                        log::warn("[HttpClient] Server NO devolvio newModCode para mod/admin {}#{}. El mod-code actual puede estar desactualizado.", username, accountID);
                        Mod::get()->setSavedValue<bool>("gd-verification-failed", false);
                    }
                }
            } else {
                PaimonDebug::warn("[HttpClient] JSON parse failed in moderator check, falling back to string search");
                // fallback: busco a mano en el string
                isMod = response.find("\"isModerator\":true") != std::string::npos || response.find("\"isModerator\": true") != std::string::npos;
                isAdmin = response.find("\"isAdmin\":true") != std::string::npos || response.find("\"isAdmin\": true") != std::string::npos;
                isVip = response.find("\"isVip\":true") != std::string::npos || response.find("\"isVip\": true") != std::string::npos;
            }

            // Regla global: admin tambien cuenta como moderador.
            if (isAdmin) {
                isMod = true;
            }

            // guardar estado VIP como saved value pa uso local
            Mod::get()->setSavedValue<bool>("is-verified-vip", isVip);
            PaimonDebug::log("[HttpClient] User {}#{} => moderator: {}, admin: {}, vip: {}", username, accountID, isMod, isAdmin, isVip);
        } else {
            log::error("[HttpClient] Failed secure moderator check for {}#{}: {}", username, accountID, response);
            log::error("[HttpClient] Server URL: {}", m_serverURL);
            // Si es 401, probablemente API key mismatch
            if (response.find("401") != std::string::npos) {
                log::error("[HttpClient] HTTP 401 = API key mismatch. Expected key may differ from server.");
            }
            // 429 — activar cooldown para futuros checks (server told us to back off)
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
    std::string url = m_serverURL + "/api/admin/banlist?username=" + reqUser + "&accountID=" + std::to_string(reqAccountID);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback);
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
    // validar que la URL sea segura (prevenir SSRF)
    if (!isUrlSafe(url)) {
        PaimonDebug::log("[HttpClient] Blocked unsafe URL: {}", url);
        if (callback) callback(false, {}, 0, 0);
        return;
    }

    // Verificar cache de texturas antes de descargar
    auto* cachedTex = CCTextureCache::sharedTextureCache()->textureForKey(url.c_str());
    if (cachedTex) {
        PaimonDebug::log("[HttpClient] Cache hit for URL: {}", url);
        // Crear datos dummy para mantener compatibilidad con el callback
        callback(true, {}, static_cast<int>(cachedTex->getPixelsWide()), static_cast<int>(cachedTex->getPixelsHigh()));
        return;
    }

    // Solo enviar X-API-Key a URLs del servidor principal (Cloudflare Worker).
    // Enviar el API key a Vercel u otros hosts puede causar rechazo si el
    // servidor cambio su validacion de headers.
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
    // validar que la URL sea segura (prevenir SSRF)
    if (!isUrlSafe(url)) {
        PaimonDebug::log("[HttpClient] Blocked unsafe URL: {}", url);
        if (callback) callback(false, {}, 0, 0);
        return;
    }
    // Descarga binaria SIN validar magic bytes de imagen.
    // util para archivos de audio (MP3, OGG, etc.) que no pasan
    // la validacion de formato de imagen en performBinaryRequest.
    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(30));
    req.acceptEncoding("gzip, deflate");

    // Solo enviar X-API-Key a URLs del servidor principal (Cloudflare Worker).
    // Enviar el API key a Vercel u otros hosts puede causar rechazo si el
    // servidor cambio su validacion de headers.
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

        // Solo verificar Content-Type para rechazar errores JSON/HTML
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
    // If Worker is exhausted, try CDN fallback for known read-only endpoints
    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        // Map Worker endpoints to CDN public JSON paths
        static const std::unordered_map<std::string, std::string> cdnEndpoints = {
            {"/api/daily/current", "/system/public/api/daily/current.json"},
            {"/api/weekly/current", "/system/public/api/weekly/current.json"},
            {"/api/moderators", "/system/public/api/moderators.json"},
            {"/api/top-creators", "/system/public/api/top-creators.json"},
            {"/api/top-thumbnails", "/system/public/api/top-thumbnails.json"},
        };

        // Strip query string for matching
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
    // Include X-Mod-Code for authenticated requests that need moderator verification
    if (!m_modCode.empty()) {
        headers.push_back("X-Mod-Code: " + m_modCode);
        PaimonDebug::log("[HttpClient] get with mod-code (prefijo: {}...)", m_modCode.substr(0, 8));
    }
    performRequest(url, "GET", "", headers, callback, false);
}

void HttpClient::getProfileStats(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Fetching profile stats for account {}", accountID);

    if (isWorkerExhausted() && !m_cdnBaseURL.empty()) {
        // Profile stats are per-user — no public CDN snapshot available
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
        url += "?username=" + username;
    }
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback, false);
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
    // Solo incluir X-Mod-Code si tenemos uno valido.
    // Un header vacio hace que el server lo interprete como code incorrecto
    // en vez de caer al fallback GDBrowser.
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

// ════════════════════════════════════════════════════════════
// Pet Shop API
// ════════════════════════════════════════════════════════════

void HttpClient::getPetShopList(GenericCallback callback) {
    get("/api/pet-shop/list", callback);
}

void HttpClient::downloadPetShopItem(std::string const& itemId, std::string const& format,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback) {
    // CDN fallback when Worker is exhausted
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

// ════════════════════════════════════════════════════════════
// Whitelist API
// ════════════════════════════════════════════════════════════

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
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
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
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(m_serverURL + "/api/whitelist/remove", "POST", json.dump(), headers, callback);
}

// ════════════════════════════════════════════════════════════
// SSRF prevention
// ════════════════════════════════════════════════════════════

bool HttpClient::isUrlSafe(std::string const& url) {
    if (url.empty()) return false;

    // bloquear esquemas peligrosos
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.starts_with("file://") || lower.starts_with("ftp://") ||
        lower.starts_with("gopher://") || lower.starts_with("data:")) {
        return false;
    }

    // solo permitir http/https
    if (!lower.starts_with("http://") && !lower.starts_with("https://")) {
        return false;
    }

    // extraer host de la URL
    size_t hostStart = lower.find("://");
    if (hostStart == std::string::npos) return false;
    hostStart += 3;

    // rechazar credentials en URL (user:pass@host)
    size_t atPos = lower.find('@', hostStart);
    size_t slashPos = lower.find('/', hostStart);
    if (atPos != std::string::npos && (slashPos == std::string::npos || atPos < slashPos)) {
        return false;
    }

    std::string hostPort = (slashPos != std::string::npos)
        ? lower.substr(hostStart, slashPos - hostStart)
        : lower.substr(hostStart);

    // quitar puerto
    size_t colonPos = hostPort.rfind(':');
    std::string host = (colonPos != std::string::npos)
        ? hostPort.substr(0, colonPos)
        : hostPort;

    if (host.empty()) return false;

    // bloquear localhost
    if (host == "localhost" || host == "127.0.0.1" || host == "::1" ||
        host == "[::1]" || host == "0.0.0.0") {
        return false;
    }

    // bloquear rangos privados (10.*, 172.16-31.*, 192.168.*, 169.254.*)
    if (host.starts_with("10.") || host.starts_with("192.168.") ||
        host.starts_with("169.254.")) {
        return false;
    }
    if (host.starts_with("172.")) {
        // 172.16.0.0 - 172.31.255.255
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
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));
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
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));
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
