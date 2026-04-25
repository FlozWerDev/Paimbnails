#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <string>
#include <vector>

/**
 * ThumbnailSubmissionService — sugerencias, updates y descargas de
 * contenido pendiente.  Extraido de ThumbnailAPI.
 */
class ThumbnailSubmissionService {
public:
    using UploadCallback   = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;

    static ThumbnailSubmissionService& get() {
        static ThumbnailSubmissionService instance;
        return instance;
    }

    void setServerEnabled(bool enabled) { m_serverEnabled = enabled; }

    // sugerencias
    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData,
                          std::string const& username, UploadCallback callback);
    // updates
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData,
                      std::string const& username, UploadCallback callback);
    // descargas
    void downloadSuggestion(int levelId, DownloadCallback callback);
    void downloadSuggestionImage(std::string const& filename, DownloadCallback callback);
    void downloadUpdate(int levelId, DownloadCallback callback);
    void downloadReported(int levelId, DownloadCallback callback);

private:
    ThumbnailSubmissionService() = default;
    ThumbnailSubmissionService(ThumbnailSubmissionService const&) = delete;
    ThumbnailSubmissionService& operator=(ThumbnailSubmissionService const&) = delete;

    bool m_serverEnabled = true;
    int  m_uploadCount   = 0;
};
