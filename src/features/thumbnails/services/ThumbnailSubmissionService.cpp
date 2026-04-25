#include "ThumbnailSubmissionService.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../framework/HookInterceptor.hpp"
#include "ThumbnailLoader.hpp"
#include "ThumbnailTransportClient.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;

// ── uploads ─────────────────────────────────────────────────────────

void ThumbnailSubmissionService::uploadSuggestion(int levelId,
    std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    // ── Hook interceptors ───────────────────────────────────────────
    paimon::HookContext ctx{"upload", levelId, username, "png", pngData.size(), &pngData};
    auto hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "validate";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "security-check";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }

    HttpClient::get().uploadSuggestion(levelId, pngData, username,
        [this, callback, levelId, username](bool success, std::string const& message) {
            if (success) m_uploadCount++;
            paimon::HookContext postCtx{"upload", levelId, username, "png", 0, nullptr};
            paimon::HookInterceptor::get().runPostHooks(postCtx, success);
            callback(success, message);
        });
}

void ThumbnailSubmissionService::uploadUpdate(int levelId,
    std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    // ── Hook interceptors ───────────────────────────────────────────
    paimon::HookContext ctx{"upload", levelId, username, "png", pngData.size(), &pngData};
    auto hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "validate";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "security-check";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }

    HttpClient::get().uploadUpdate(levelId, pngData, username,
        [this, callback, levelId, username](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ThumbnailLoader::get().invalidateLevel(levelId);
            }
            paimon::HookContext postCtx{"upload", levelId, username, "png", 0, nullptr};
            paimon::HookInterceptor::get().runPostHooks(postCtx, success);
            callback(success, message);
        });
}

// ── downloads ───────────────────────────────────────────────────────

void ThumbnailSubmissionService::downloadSuggestion(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadSuggestion(levelId,
        [callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }
            callback(success, ThumbnailTransportClient::bytesToTexture(data));
        });
}

void ThumbnailSubmissionService::downloadSuggestionImage(std::string const& filename,
                                                         DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    std::string url = HttpClient::get().getServerURL() + "/" + filename;
    HttpClient::get().downloadFromUrl(url,
        [callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }
            callback(success, ThumbnailTransportClient::bytesToTexture(data));
        });
}

void ThumbnailSubmissionService::downloadUpdate(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadUpdate(levelId,
        [callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }
            callback(success, ThumbnailTransportClient::bytesToTexture(data));
        });
}

void ThumbnailSubmissionService::downloadReported(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadReported(levelId,
        [callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }
            callback(success, ThumbnailTransportClient::bytesToTexture(data));
        });
}
