#include "ThumbnailAPI.hpp"
#include <Geode/loader/Log.hpp>
#include "../features/profiles/services/ProfileThumbs.hpp"

using namespace geode::prelude;

/**
 * ThumbnailAPI.cpp â€” FACHADA DE COMPATIBILIDAD
 *
 * Toda la logica de negocio ha sido extraida a:
 *   - ThumbnailTransportClient   (thumbnails core)
 *   - ThumbnailSubmissionService  (sugerencias / updates)
 *   - ModerationService           (moderacion + cola)
 *   - ProfileImageService         (imagenes de perfil)
 *
 * Cada metodo delega al servicio correspondiente.
 * Los consumidores nuevos deben llamar al servicio directamente.
 */

ThumbnailAPI::ThumbnailAPI() {
    m_serverEnabled = true;
    log::info("[ThumbnailAPI] fachada inicializada");
}

void ThumbnailAPI::setServerEnabled(bool enabled) {
    m_serverEnabled = enabled;
    ThumbnailTransportClient::get().setServerEnabled(enabled);
    ThumbnailSubmissionService::get().setServerEnabled(enabled);
    ModerationService::get().setServerEnabled(enabled);
    ProfileImageService::get().setServerEnabled(enabled);
    log::info("[ThumbnailAPI] modo servidor cambiado a: {}", enabled);
}

// â”€â”€ Thumbnail core (ThumbnailTransportClient) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void ThumbnailAPI::getThumbnails(int levelId, ThumbnailListCallback callback) {
    log::info("[ThumbnailAPI] getThumbnails: levelId={}", levelId);
    ThumbnailTransportClient::get().getThumbnails(levelId, std::move(callback));
}
void ThumbnailAPI::getThumbnailInfo(int levelId, ActionCallback callback) {
    ThumbnailTransportClient::get().getThumbnailInfo(levelId, std::move(callback));
}
std::string ThumbnailAPI::getThumbnailURL(int levelId) {
    return ThumbnailTransportClient::get().getThumbnailURL(levelId);
}
void ThumbnailAPI::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    log::info("[ThumbnailAPI] uploadThumbnail: levelId={} user={} bytes={}", levelId, username, pngData.size());
    ThumbnailTransportClient::get().uploadThumbnail(levelId, pngData, username, std::move(callback));
}
void ThumbnailAPI::uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    ThumbnailTransportClient::get().uploadGIF(levelId, gifData, username, std::move(callback));
}
void ThumbnailAPI::uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback) {
    ThumbnailTransportClient::get().uploadVideo(levelId, mp4Data, username, std::move(callback));
}
void ThumbnailAPI::downloadThumbnail(int levelId, DownloadCallback callback) {
    log::info("[ThumbnailAPI] downloadThumbnail: levelId={}", levelId);
    ThumbnailTransportClient::get().downloadThumbnail(levelId, std::move(callback));
}
void ThumbnailAPI::checkExists(int levelId, ExistsCallback callback) {
    log::debug("[ThumbnailAPI] checkExists: levelId={}", levelId);
    ThumbnailTransportClient::get().checkExists(levelId, std::move(callback));
}
void ThumbnailAPI::deleteThumbnail(int levelId, std::string const& thumbnailId, std::string const& username, int accountID, ActionCallback callback) {
    log::info("[ThumbnailAPI] deleteThumbnail: levelId={} thumbId={} user={}", levelId, thumbnailId, username);
    ThumbnailTransportClient::get().deleteThumbnail(levelId, thumbnailId, username, accountID, std::move(callback));
}
void ThumbnailAPI::getRating(int levelId, std::string const& username, std::string const& thumbnailId, geode::CopyableFunction<void(bool success, float average, int count, int userVote)> callback) {
    ThumbnailTransportClient::get().getRating(levelId, username, thumbnailId, std::move(callback));
}
void ThumbnailAPI::submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, ActionCallback callback) {
    ThumbnailTransportClient::get().submitVote(levelId, stars, username, thumbnailId, std::move(callback));
}
void ThumbnailAPI::getThumbnail(int levelId, DownloadCallback callback) {
    ThumbnailTransportClient::get().getThumbnail(levelId, std::move(callback));
}
void ThumbnailAPI::downloadFromUrl(std::string const& url, DownloadCallback callback) {
    log::debug("[ThumbnailAPI] downloadFromUrl: url={}", url);
    ThumbnailTransportClient::get().downloadFromUrl(url, std::move(callback));
}
void ThumbnailAPI::downloadFromUrlData(std::string const& url, DownloadDataCallback callback) {
    ThumbnailTransportClient::get().downloadFromUrlData(url, std::move(callback));
}
void ThumbnailAPI::getTopCreators(ActionCallback callback) {
    ThumbnailTransportClient::get().getTopCreators(std::move(callback));
}
void ThumbnailAPI::getTopThumbnails(ActionCallback callback) {
    ThumbnailTransportClient::get().getTopThumbnails(std::move(callback));
}
cocos2d::CCTexture2D* ThumbnailAPI::webpToTexture(std::vector<uint8_t> const& webpData) {
    return ThumbnailTransportClient::webpToTexture(webpData);
}

// â”€â”€ Submissions (ThumbnailSubmissionService) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void ThumbnailAPI::uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    log::info("[ThumbnailAPI] uploadSuggestion: levelId={} user={} bytes={}", levelId, username, pngData.size());
    ThumbnailSubmissionService::get().uploadSuggestion(levelId, pngData, username, std::move(callback));
}
void ThumbnailAPI::uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    log::info("[ThumbnailAPI] uploadUpdate: levelId={} user={} bytes={}", levelId, username, pngData.size());
    ThumbnailSubmissionService::get().uploadUpdate(levelId, pngData, username, std::move(callback));
}
void ThumbnailAPI::downloadSuggestion(int levelId, DownloadCallback callback) {
    ThumbnailSubmissionService::get().downloadSuggestion(levelId, std::move(callback));
}
void ThumbnailAPI::downloadSuggestionImage(std::string const& filename, DownloadCallback callback) {
    ThumbnailSubmissionService::get().downloadSuggestionImage(filename, std::move(callback));
}
void ThumbnailAPI::downloadUpdate(int levelId, DownloadCallback callback) {
    ThumbnailSubmissionService::get().downloadUpdate(levelId, std::move(callback));
}
void ThumbnailAPI::downloadReported(int levelId, DownloadCallback callback) {
    ThumbnailSubmissionService::get().downloadReported(levelId, std::move(callback));
}

// â”€â”€ Moderation (ModerationService) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void ThumbnailAPI::checkModerator(std::string const& username, ModeratorCallback callback) {
    ModerationService::get().checkModerator(username, std::move(callback));
}
void ThumbnailAPI::checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback) {
    log::info("[ThumbnailAPI] checkModeratorAccount: user={} accountID={}", username, accountID);
    ModerationService::get().checkModeratorAccount(username, accountID, std::move(callback));
}
void ThumbnailAPI::checkUserStatus(std::string const& username, ModeratorCallback callback) {
    ModerationService::get().checkUserStatus(username, std::move(callback));
}
void ThumbnailAPI::addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    ModerationService::get().addModerator(username, adminUser, std::move(callback));
}
void ThumbnailAPI::removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    ModerationService::get().removeModerator(username, adminUser, std::move(callback));
}
void ThumbnailAPI::syncVerificationQueue(PendingCategory category, QueueCallback callback) {
    ModerationService::get().syncVerificationQueue(category, std::move(callback));
}
void ThumbnailAPI::claimQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& type) {
    ModerationService::get().claimQueueItem(levelId, category, username, std::move(callback), type);
}
void ThumbnailAPI::acceptQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& targetFilename, std::string const& type) {
    ModerationService::get().acceptQueueItem(levelId, category, username, std::move(callback), targetFilename, type);
}
void ThumbnailAPI::rejectQueueItem(int levelId, PendingCategory category, std::string const& username, std::string const& reason, ActionCallback callback, std::string const& type) {
    ModerationService::get().rejectQueueItem(levelId, category, username, reason, std::move(callback), type);
}
void ThumbnailAPI::submitReport(int levelId, std::string const& username, std::string const& note, ActionCallback callback) {
    log::info("[ThumbnailAPI] submitReport: levelId={} user={}", levelId, username);
    ModerationService::get().submitReport(levelId, username, note, std::move(callback));
}

// â”€â”€ Profiles (ProfileImageService) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void ThumbnailAPI::uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    log::info("[ThumbnailAPI] uploadProfile: accountID={} user={} bytes={}", accountID, username, pngData.size());
    ProfileImageService::get().uploadProfile(accountID, pngData, username, std::move(callback));
}
void ThumbnailAPI::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    ProfileImageService::get().uploadProfileGIF(accountID, gifData, username, std::move(callback));
}
void ThumbnailAPI::uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback) {
    ProfileImageService::get().uploadProfileVideo(accountID, mp4Data, username, std::move(callback));
}
void ThumbnailAPI::downloadProfile(int accountID, std::string const& username, DownloadCallback callback) {
    log::info("[ThumbnailAPI] downloadProfile: accountID={} user={}", accountID, username);
    ProfileImageService::get().downloadProfile(accountID, username, std::move(callback));
}
void ThumbnailAPI::batchCheckProfiles(std::vector<int> const& accountIDs, BatchCheckCallback callback) {
    log::info("[ThumbnailAPI] batchCheckProfiles: {} accounts", accountIDs.size());
    ProfileImageService::get().batchCheckProfiles(accountIDs, std::move(callback));
}
void ThumbnailAPI::uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback) {
    ProfileImageService::get().uploadProfileImg(accountID, imgData, username, contentType, std::move(callback));
}
void ThumbnailAPI::uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    ProfileImageService::get().uploadProfileImgGIF(accountID, gifData, username, std::move(callback));
}
void ThumbnailAPI::downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf) {
    ProfileImageService::get().downloadProfileImg(accountID, std::move(callback), isSelf);
}
void ThumbnailAPI::downloadPendingProfile(int accountID, DownloadCallback callback) {
    ProfileImageService::get().downloadPendingProfile(accountID, std::move(callback));
}
void ThumbnailAPI::uploadProfileConfig(int accountID, ProfileConfig const& config, ActionCallback callback) {
    ProfileImageService::get().uploadProfileConfig(accountID, config, std::move(callback));
}
void ThumbnailAPI::downloadProfileConfig(int accountID, geode::CopyableFunction<void(bool success, ProfileConfig const& config)> callback) {
    ProfileImageService::get().downloadProfileConfig(accountID, std::move(callback));
}

