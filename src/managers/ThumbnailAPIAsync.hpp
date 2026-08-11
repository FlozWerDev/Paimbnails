#pragma once

#include <Geode/Geode.hpp>
#include <arc/future/Future.hpp>
#include "../utils/ThumbnailTypes.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// ThumbnailAPI exposed as arc::Future; new code should use these calls.
namespace paimon::thumb_api {

using namespace geode::prelude;

arc::Future<ThumbnailGalleryResult> getThumbnails(int levelId, bool forceRefresh = false);
arc::Future<ThumbnailApiMessageResult> getThumbnailInfo(int levelId);
std::string getThumbnailURL(int levelId);
arc::Future<ThumbnailApiMessageResult> uploadThumbnail(
    int levelId, std::vector<uint8_t> const& pngData, std::string const& username);
arc::Future<ThumbnailApiMessageResult> uploadGIF(
    int levelId, std::vector<uint8_t> const& gifData, std::string const& username);
arc::Future<ThumbnailApiMessageResult> uploadVideo(
    int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username);
arc::Future<ThumbnailTextureResult> downloadThumbnail(int levelId, bool isGif = false);
arc::Future<bool> checkExists(int levelId);
arc::Future<ThumbnailApiMessageResult> deleteThumbnail(
    int levelId, std::string const& thumbnailId, std::string const& username, int accountID);
arc::Future<ThumbnailApiMessageResult> reorderThumbnails(
    int levelId, std::vector<std::string> const& thumbnailIds);
arc::Future<ThumbnailRatingResult> getRating(
    int levelId, std::string const& username, std::string const& thumbnailId);
arc::Future<ThumbnailApiMessageResult> submitVote(
    int levelId, int stars, std::string const& username, std::string const& thumbnailId);
arc::Future<ThumbnailTextureResult> getThumbnail(int levelId);
arc::Future<ThumbnailTextureResult> downloadFromUrl(std::string const& url);
arc::Future<ThumbnailDataResult> downloadFromUrlData(std::string const& url);
arc::Future<ThumbnailApiMessageResult> getTopCreators();
arc::Future<ThumbnailApiMessageResult> getTopThumbnails();
arc::Future<ThumbnailApiMessageResult> getUserUploads(std::string const& username);

arc::Future<ThumbnailApiMessageResult> uploadSuggestion(
    int levelId, std::vector<uint8_t> const& pngData, std::string const& username);
arc::Future<ThumbnailApiMessageResult> uploadUpdate(
    int levelId, std::vector<uint8_t> const& pngData, std::string const& username);
arc::Future<ThumbnailTextureResult> downloadSuggestion(int levelId);
arc::Future<ThumbnailTextureResult> downloadSuggestionImage(std::string const& filename);
arc::Future<ThumbnailTextureResult> downloadUpdate(int levelId);
arc::Future<ThumbnailTextureResult> downloadReported(int levelId);

arc::Future<ThumbnailModeratorResult> checkModerator(std::string const& username);
arc::Future<ThumbnailModeratorResult> checkModeratorAccount(std::string const& username, int accountID);
arc::Future<ThumbnailModeratorResult> checkUserStatus(std::string const& username);
arc::Future<ThumbnailApiMessageResult> addModerator(std::string const& username, std::string const& adminUser);
arc::Future<ThumbnailApiMessageResult> removeModerator(std::string const& username, std::string const& adminUser);
struct ThumbnailQueueResult {
    bool success = false;
    std::vector<PendingItem> items;
};
arc::Future<ThumbnailQueueResult> syncVerificationQueue(PendingCategory category);
arc::Future<ThumbnailApiMessageResult> claimQueueItem(
    int levelId, PendingCategory category, std::string const& username, std::string const& type = "");
arc::Future<ThumbnailApiMessageResult> acceptQueueItem(
    int levelId, PendingCategory category, std::string const& username,
    std::string const& targetFilename = "", std::string const& type = "");
arc::Future<ThumbnailApiMessageResult> rejectQueueItem(
    int levelId, PendingCategory category, std::string const& username,
    std::string const& reason, std::string const& type = "");
arc::Future<ThumbnailApiMessageResult> submitReport(
    int levelId, std::string const& username, std::string const& note);

arc::Future<ThumbnailApiMessageResult> uploadProfile(
    int accountID, std::vector<uint8_t> const& pngData, std::string const& username);
arc::Future<ThumbnailApiMessageResult> uploadProfileGIF(
    int accountID, std::vector<uint8_t> const& gifData, std::string const& username);
arc::Future<ThumbnailApiMessageResult> uploadProfileVideo(
    int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username);
arc::Future<ThumbnailTextureResult> downloadProfile(int accountID, std::string const& username);
struct ProfileBatchCheckResult {
    bool success = false;
    std::unordered_set<int> found;
    std::unordered_map<int, ProfileConfig> configs;
};
arc::Future<ProfileBatchCheckResult> batchCheckProfiles(std::vector<int> const& accountIDs);
arc::Future<ThumbnailApiMessageResult> uploadProfileImg(
    int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType);
arc::Future<ThumbnailApiMessageResult> uploadProfileImgGIF(
    int accountID, std::vector<uint8_t> const& gifData, std::string const& username);
arc::Future<ThumbnailTextureResult> downloadProfileImg(int accountID, bool isSelf = false);
arc::Future<ThumbnailTextureResult> downloadPendingProfile(int accountID);
struct ProfileConfigResult {
    bool success = false;
    ProfileConfig config;
};
arc::Future<ProfileConfigResult> downloadProfileConfig(int accountID);
arc::Future<ThumbnailApiMessageResult> uploadProfileConfig(int accountID, ProfileConfig const& config);

} // namespace paimon::thumb_api
