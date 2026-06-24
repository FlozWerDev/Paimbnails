#include "ThumbnailAPIAsync.hpp"
#include "ThumbnailAPI.hpp"
#include "../framework/async/CallbackFuture.hpp"
#include "../features/thumbnails/services/ThumbnailTransportClient.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"

using namespace geode::prelude;

namespace paimon::thumb_api {
namespace {

arc::Future<ThumbnailApiMessageResult> messageFrom(auto&& start) {
    return paimon::async_api::awaitCallback<ThumbnailApiMessageResult>(std::forward<decltype(start)>(start));
}

arc::Future<ThumbnailTextureResult> textureFrom(auto&& start) {
    return paimon::async_api::awaitCallback<ThumbnailTextureResult>([&](auto cb) {
        std::forward<decltype(start)>(start)([cb](bool success, CCTexture2D* texture) {
            ThumbnailTextureResult result{.success = success};
            if (texture) result.texture = texture;
            cb(std::move(result));
        });
    });
}

arc::Future<ThumbnailModeratorResult> moderatorFrom(auto&& start) {
    return paimon::async_api::awaitCallback<ThumbnailModeratorResult>([&](auto cb) {
        std::forward<decltype(start)>(start)([cb](bool isModerator, bool isAdmin) {
            cb(ThumbnailModeratorResult{.isModerator = isModerator, .isAdmin = isAdmin});
        });
    });
}

} // namespace

arc::Future<ThumbnailGalleryResult> getThumbnails(int levelId, bool forceRefresh) {
    return ThumbnailTransportClient::get().fetchThumbnailsFuture(levelId, forceRefresh);
}

arc::Future<ThumbnailApiMessageResult> getThumbnailInfo(int levelId) {
    return messageFrom([levelId](auto cb) {
        ThumbnailAPI::get().getThumbnailInfo(levelId, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

std::string getThumbnailURL(int levelId) {
    return ThumbnailAPI::get().getThumbnailURL(levelId);
}

arc::Future<ThumbnailApiMessageResult> uploadThumbnail(
    int levelId, std::vector<uint8_t> const& pngData, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadThumbnail(levelId, pngData, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadGIF(
    int levelId, std::vector<uint8_t> const& gifData, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadGIF(levelId, gifData, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadVideo(
    int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadVideo(levelId, mp4Data, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailTextureResult> downloadThumbnail(int levelId, bool isGif) {
    return ThumbnailTransportClient::get().downloadThumbnailFuture(levelId, isGif);
}

arc::Future<bool> checkExists(int levelId) {
    return ThumbnailTransportClient::get().checkExistsFuture(levelId);
}

arc::Future<ThumbnailApiMessageResult> deleteThumbnail(
    int levelId, std::string const& thumbnailId, std::string const& username, int accountID
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().deleteThumbnail(levelId, thumbnailId, username, accountID, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> reorderThumbnails(
    int levelId, std::vector<std::string> const& thumbnailIds
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().reorderThumbnails(levelId, thumbnailIds, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailRatingResult> getRating(
    int levelId, std::string const& username, std::string const& thumbnailId
) {
    return paimon::async_api::awaitCallback<ThumbnailRatingResult>([&](auto cb) {
        ThumbnailAPI::get().getRating(levelId, username, thumbnailId,
            [cb](bool success, float average, int count, int userVote) {
                cb(ThumbnailRatingResult{
                    .success = success,
                    .average = average,
                    .count = count,
                    .userVote = userVote,
                });
            });
    });
}

arc::Future<ThumbnailApiMessageResult> submitVote(
    int levelId, int stars, std::string const& username, std::string const& thumbnailId
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().submitVote(levelId, stars, username, thumbnailId, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailTextureResult> getThumbnail(int levelId) {
    return ThumbnailTransportClient::get().getThumbnailFuture(levelId);
}

arc::Future<ThumbnailTextureResult> downloadFromUrl(std::string const& url) {
    return ThumbnailTransportClient::get().downloadFromUrlFuture(url);
}

arc::Future<ThumbnailDataResult> downloadFromUrlData(std::string const& url) {
    return ThumbnailTransportClient::get().downloadFromUrlDataFuture(url);
}

arc::Future<ThumbnailApiMessageResult> getTopCreators() {
    return messageFrom([](auto cb) {
        ThumbnailAPI::get().getTopCreators([cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> getTopThumbnails() {
    return messageFrom([](auto cb) {
        ThumbnailAPI::get().getTopThumbnails([cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> getUserUploads(std::string const& username) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().getUserUploads(username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadSuggestion(
    int levelId, std::vector<uint8_t> const& pngData, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadSuggestion(levelId, pngData, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadUpdate(
    int levelId, std::vector<uint8_t> const& pngData, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadUpdate(levelId, pngData, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailTextureResult> downloadSuggestion(int levelId) {
    return textureFrom([levelId](auto inner) {
        ThumbnailAPI::get().downloadSuggestion(levelId, std::move(inner));
    });
}

arc::Future<ThumbnailTextureResult> downloadSuggestionImage(std::string const& filename) {
    return textureFrom([&](auto inner) {
        ThumbnailAPI::get().downloadSuggestionImage(filename, std::move(inner));
    });
}

arc::Future<ThumbnailTextureResult> downloadUpdate(int levelId) {
    return textureFrom([levelId](auto inner) {
        ThumbnailAPI::get().downloadUpdate(levelId, std::move(inner));
    });
}

arc::Future<ThumbnailTextureResult> downloadReported(int levelId) {
    return textureFrom([levelId](auto inner) {
        ThumbnailAPI::get().downloadReported(levelId, std::move(inner));
    });
}

arc::Future<ThumbnailModeratorResult> checkModerator(std::string const& username) {
    return moderatorFrom([&](auto inner) {
        ThumbnailAPI::get().checkModerator(username, std::move(inner));
    });
}

arc::Future<ThumbnailModeratorResult> checkModeratorAccount(std::string const& username, int accountID) {
    return moderatorFrom([&](auto inner) {
        ThumbnailAPI::get().checkModeratorAccount(username, accountID, std::move(inner));
    });
}

arc::Future<ThumbnailModeratorResult> checkUserStatus(std::string const& username) {
    return moderatorFrom([&](auto inner) {
        ThumbnailAPI::get().checkUserStatus(username, std::move(inner));
    });
}

arc::Future<ThumbnailApiMessageResult> addModerator(std::string const& username, std::string const& adminUser) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().addModerator(username, adminUser, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> removeModerator(std::string const& username, std::string const& adminUser) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().removeModerator(username, adminUser, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailQueueResult> syncVerificationQueue(PendingCategory category) {
    return paimon::async_api::awaitCallback<ThumbnailQueueResult>([category](auto cb) {
        ThumbnailAPI::get().syncVerificationQueue(category, [cb](bool success, std::vector<PendingItem> const& items) {
            cb(ThumbnailQueueResult{.success = success, .items = items});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> claimQueueItem(
    int levelId, PendingCategory category, std::string const& username, std::string const& type
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().claimQueueItem(levelId, category, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        }, type);
    });
}

arc::Future<ThumbnailApiMessageResult> acceptQueueItem(
    int levelId, PendingCategory category, std::string const& username,
    std::string const& targetFilename, std::string const& type
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().acceptQueueItem(levelId, category, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        }, targetFilename, type);
    });
}

arc::Future<ThumbnailApiMessageResult> rejectQueueItem(
    int levelId, PendingCategory category, std::string const& username,
    std::string const& reason, std::string const& type
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().rejectQueueItem(levelId, category, username, reason, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        }, type);
    });
}

arc::Future<ThumbnailApiMessageResult> submitReport(
    int levelId, std::string const& username, std::string const& note
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().submitReport(levelId, username, note, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadProfile(
    int accountID, std::vector<uint8_t> const& pngData, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadProfile(accountID, pngData, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadProfileGIF(
    int accountID, std::vector<uint8_t> const& gifData, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadProfileGIF(accountID, gifData, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadProfileVideo(
    int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadProfileVideo(accountID, mp4Data, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailTextureResult> downloadProfile(int accountID, std::string const& username) {
    return textureFrom([&](auto inner) {
        ThumbnailAPI::get().downloadProfile(accountID, username, std::move(inner));
    });
}

arc::Future<ProfileBatchCheckResult> batchCheckProfiles(std::vector<int> const& accountIDs) {
    return paimon::async_api::awaitCallback<ProfileBatchCheckResult>([&](auto cb) {
        ThumbnailAPI::get().batchCheckProfiles(accountIDs,
            [cb](bool success, std::unordered_set<int> const& found,
                 std::unordered_map<int, ProfileConfig> const& configs) {
                cb(ProfileBatchCheckResult{.success = success, .found = found, .configs = configs});
            });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadProfileImg(
    int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadProfileImg(accountID, imgData, username, contentType, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadProfileImgGIF(
    int accountID, std::vector<uint8_t> const& gifData, std::string const& username
) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadProfileImgGIF(accountID, gifData, username, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

arc::Future<ThumbnailTextureResult> downloadProfileImg(int accountID, bool isSelf) {
    return textureFrom([=](auto inner) {
        ThumbnailAPI::get().downloadProfileImg(accountID, std::move(inner), isSelf);
    });
}

arc::Future<ThumbnailTextureResult> downloadPendingProfile(int accountID) {
    return textureFrom([accountID](auto inner) {
        ThumbnailAPI::get().downloadPendingProfile(accountID, std::move(inner));
    });
}

arc::Future<ProfileConfigResult> downloadProfileConfig(int accountID) {
    return paimon::async_api::awaitCallback<ProfileConfigResult>([accountID](auto cb) {
        ThumbnailAPI::get().downloadProfileConfig(accountID, [cb](bool success, ProfileConfig const& config) {
            cb(ProfileConfigResult{.success = success, .config = config});
        });
    });
}

arc::Future<ThumbnailApiMessageResult> uploadProfileConfig(int accountID, ProfileConfig const& config) {
    return messageFrom([&](auto cb) {
        ThumbnailAPI::get().uploadProfileConfig(accountID, config, [cb](bool s, std::string const& m) {
            cb(ThumbnailApiMessageResult{.success = s, .message = m});
        });
    });
}

} // namespace paimon::thumb_api