#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include "../../../utils/ThumbnailTypes.hpp"
#include "LocalThumbs.hpp"
#include <string>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

/**
 * ThumbnailTransportClient — operaciones de subida/bajada/consulta de miniaturas.
 * Extraido de ThumbnailAPI para separar dominio 'thumbnails' de moderacion/profiles.
 */
class ThumbnailTransportClient {
public:
    using UploadCallback      = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback    = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;
    using DownloadDataCallback= geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using ExistsCallback      = geode::CopyableFunction<void(bool exists)>;
    using ActionCallback      = geode::CopyableFunction<void(bool success, std::string const& message)>;

    using ThumbnailInfo = ::ThumbnailInfo;
    using ThumbnailListCallback = geode::CopyableFunction<void(bool success, std::vector<ThumbnailInfo> const& thumbnails)>;

    static ThumbnailTransportClient& get() {
        static ThumbnailTransportClient instance;
        return instance;
    }

    void setServerEnabled(bool enabled) { m_serverEnabled = enabled; }

    // lista miniaturas nivel
    void getThumbnails(int levelId, ThumbnailListCallback callback, bool forceRefresh = false);
    // info completa JSON
    void getThumbnailInfo(int levelId, ActionCallback callback);
    // URL directa
    std::string getThumbnailURL(int levelId);
    // subir PNG
    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData,
                         std::string const& username, UploadCallback callback);
    // subir GIF (mod/admin)
    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData,
                   std::string const& username, UploadCallback callback);
    // subir video MP4 (mod/admin)
    void uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data,
                     std::string const& username, UploadCallback callback);
    // descargar miniatura desde servidor
    void downloadThumbnail(int levelId, DownloadCallback callback);
    // verificar existencia
    void checkExists(int levelId, ExistsCallback callback);
    // borrar miniatura (mod)
    void deleteThumbnail(int levelId, std::string const& thumbnailId, std::string const& username, int accountID,
                         ActionCallback callback);
    // reordenar miniaturas (admin)
    void reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, ActionCallback callback);
    // votos
    void getRating(int levelId, std::string const& username,
                   std::string const& thumbnailId,
                   geode::CopyableFunction<void(bool, float, int, int)> callback);
    void submitVote(int levelId, int stars, std::string const& username,
                    std::string const& thumbnailId, ActionCallback callback);
    // obtener thumbnail (local -> server fallback)
    void getThumbnail(int levelId, DownloadCallback callback);
    // descargar desde URL arbitraria
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    void downloadFromUrlData(std::string const& url, DownloadDataCallback callback);
    // top lists
    void getTopCreators(ActionCallback callback);
    void getTopThumbnails(ActionCallback callback);
    void invalidateGalleryMetadata(int levelId);

    // utilidad compartida: datos binarios -> CCTexture2D (autorelease)
    // Soporta PNG, JPG, WebP, GIF, QOI, JPEG XL via ImagePlus; JPEG fallback via CCImage
    static cocos2d::CCTexture2D* webpToTexture(std::vector<uint8_t> const& data);
    static cocos2d::CCTexture2D* bytesToTexture(std::vector<uint8_t> const& data);
    static bool isGIFData(std::vector<uint8_t> const& data);

private:
    struct GalleryMetadataEntry {
        std::vector<ThumbnailInfo> thumbnails;
        std::string revisionToken;
        std::chrono::steady_clock::time_point fetchedAt{};
    };

    ThumbnailTransportClient() = default;
    ThumbnailTransportClient(ThumbnailTransportClient const&) = delete;
    ThumbnailTransportClient& operator=(ThumbnailTransportClient const&) = delete;

    bool m_serverEnabled = true;
    int  m_uploadCount   = 0;
    std::unordered_map<int, GalleryMetadataEntry> m_galleryCache;
    std::unordered_map<int, std::vector<ThumbnailListCallback>> m_galleryInFlight;
    std::unordered_map<int, uint64_t> m_galleryGenerations;
    std::mutex m_galleryMutex;

    cocos2d::CCTexture2D* loadFromLocal(int levelId);
};
