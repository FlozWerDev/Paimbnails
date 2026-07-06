#pragma once

#include <Geode/Geode.hpp>
#include <string>

struct ProfileConfig;
#include <vector>
#include <unordered_map>
#include <mutex>

class ProfileImageService {
public:
    using UploadCallback   = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;
    using ActionCallback   = geode::CopyableFunction<void(bool success, std::string const& message)>;

    static ProfileImageService& get() {
        static ProfileImageService instance;
        return instance;
    }

    void setServerEnabled(bool enabled) { m_serverEnabled = enabled; }

    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData,
                       std::string const& username, UploadCallback callback);
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData,
                          std::string const& username, UploadCallback callback);
    void uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data,
                            std::string const& username, UploadCallback callback);
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);

    static void processProfileBackgroundBytes(int accountID,
        std::vector<uint8_t> const& data, DownloadCallback callback);

    using BatchCheckCallback = geode::CopyableFunction<void(
        bool success,
        std::unordered_set<int> const& found,
        std::unordered_map<int, ProfileConfig> const& configs)>;
    void batchCheckProfiles(std::vector<int> const& accountIDs, BatchCheckCallback callback);

    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData,
                          std::string const& username, std::string const& contentType,
                          UploadCallback callback);
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData,
                             std::string const& username, UploadCallback callback);
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);
    std::string getProfileImgGifKey(int accountID) const;
    void rememberProfileImgGifKey(int accountID, std::string const& gifKey);
    void clearProfileImgGifKey(int accountID);

    void downloadPendingProfile(int accountID, DownloadCallback callback);

    void uploadProfileConfig(int accountID, ProfileConfig const& config, ActionCallback callback);
    void downloadProfileConfig(int accountID,
        geode::CopyableFunction<void(bool, ProfileConfig const&)> callback);

private:
    ProfileImageService();
    ProfileImageService(ProfileImageService const&) = delete;
    ProfileImageService& operator=(ProfileImageService const&) = delete;

    bool m_serverEnabled = true;
    int  m_uploadCount   = 0;
    mutable std::mutex m_profileImgGifMutex;
    std::unordered_map<int, std::string> m_profileImgGifKeys;
};
