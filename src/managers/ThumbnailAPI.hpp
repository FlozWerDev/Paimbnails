#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include "../utils/HttpClient.hpp"
#include "../utils/ThumbnailTypes.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../features/thumbnails/services/ThumbnailTransportClient.hpp"

struct ProfileConfig;
#include "../features/thumbnails/services/ThumbnailSubmissionService.hpp"
#include "../features/moderation/services/ModerationService.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include <string>
#include <optional>
#include <chrono>

/**
 * ThumbnailAPI — fachada de compatibilidad.
 *
 * Toda la logica de negocio se ha movido a servicios por dominio:
 *   - ThumbnailTransportClient  (features/thumbnails/services/)
 *   - ThumbnailSubmissionService (features/thumbnails/services/)
 *   - ModerationService          (features/moderation/services/)
 *   - ProfileImageService        (features/profiles/services/)
 *
 * Las llamadas via ThumbnailAPI::get().foo() siguen funcionando
 * pero delegan al servicio correspondiente.  Los consumidores nuevos
 * deben llamar al servicio directamente.
 */
class ThumbnailAPI {
public:
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;
    using DownloadDataCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using ExistsCallback = geode::CopyableFunction<void(bool exists)>;
    using ModeratorCallback = geode::CopyableFunction<void(bool isModerator, bool isAdmin)>;
    using QueueCallback = geode::CopyableFunction<void(bool success, std::vector<PendingItem> const& items)>;
    using ActionCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    using ThumbnailInfo = ::ThumbnailInfo;
    using ThumbnailListCallback = geode::CopyableFunction<void(bool success, std::vector<ThumbnailInfo> const& thumbnails)>;

    static ThumbnailAPI& get() {
        static ThumbnailAPI instance;
        return instance;
    }

    // funciones principales de la API
    
    /**
     * obtener lista miniaturas nivel
     * @param levelId Level ID
     * @param callback Callback with list of thumbnails
     */
    void getThumbnails(int levelId, ThumbnailListCallback callback);

    /**
     * obtener informacion completa miniaturas nivel
     * @param levelId Level ID
     * @param callback Callback with raw json response
     */
    void getThumbnailInfo(int levelId, ActionCallback callback);

    /**
     * obtener url miniatura nivel
     * @param levelId Level ID
     * @return URL string
     */
    std::string getThumbnailURL(int levelId);

    /**
     * subir miniatura a servidor
     * @param levelId Level ID
     * @param pngData PNG image data
     * @param username Username of uploader
     * @param callback Callback with success status and message
     */
    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);

    // subir miniatura GIF (solo mod/admin)
    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);

    // subir miniatura video MP4 (solo mod/admin)
    void uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback);

    // subir sugerencia (no moderador) a /suggestions
    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // subir propuesta de update (no moderador) a /updates
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // subir imagen de perfil por accountID
    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // subir GIF de perfil por accountID
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // subir video MP4 de perfil por accountID (mod/admin)
    void uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback);
    // descargar imagen de perfil por accountID
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);
    // batch check: pregunta cuales cuentas tienen perfil + configs
    using BatchCheckCallback = ProfileImageService::BatchCheckCallback;
    void batchCheckProfiles(std::vector<int> const& accountIDs, BatchCheckCallback callback);

    // subir imagen de foto de perfil (profileimg) por accountID
    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback);
    // subir GIF de foto de perfil (profileimg) por accountID
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // descargar foto de perfil (profileimg) por accountID
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    // descargar imagen desde una URL cualquiera
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    // descargar solo los datos binarios de una imagen desde una URL
    void downloadFromUrlData(std::string const& url, DownloadDataCallback callback);


    
    // subir config de perfil
    void uploadProfileConfig(int accountID, ProfileConfig const& config, ActionCallback callback);
    // bajar config de perfil
    void downloadProfileConfig(int accountID, geode::CopyableFunction<void(bool success, ProfileConfig const& config)> callback);

    // descargar sugerencia desde /suggestions
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // descargar imagen de sugerencia por nombre de archivo
    void downloadSuggestionImage(std::string const& filename, DownloadCallback callback);
    // descargar update desde /updates
    void downloadUpdate(int levelId, DownloadCallback callback);
    // descargar reportada (la mini actual del servidor)
    void downloadReported(int levelId, DownloadCallback callback);
    // descargar background de perfil pendiente (para moderadores en centro de verificacion)
    void downloadPendingProfile(int accountID, DownloadCallback callback);


    // sistema de votos
    void getRating(int levelId, std::string const& username, std::string const& thumbnailId, geode::CopyableFunction<void(bool success, float average, int count, int userVote)> callback);
    void submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, ActionCallback callback);

    /**
     * descargar miniatura desde servidor (con cache)
     * @param levelId Level ID
     * @param callback Callback with success status and texture
     */
    void downloadThumbnail(int levelId, DownloadCallback callback);
    
    /**
     * verificar si existe miniatura en servidor
     * @param levelId Level ID
     * @param callback Callback with exists status
     */
    void checkExists(int levelId, ExistsCallback callback);
    
    /**
     * verificar si usuario es moderador
     * @param username Username to check
     * @param callback Callback with moderator status
     */
    void checkModerator(std::string const& username, ModeratorCallback callback);
    // chequeo de moderador “seguro” con accountID > 0 obligatorio
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);
    
    /**
     * confirmar estado mod de cualquier usuario (publico)
     * no hace chequeo seguridad usuario actual.
     */
    void checkUserStatus(std::string const& username, ModeratorCallback callback);

    /**
     * obtener textura miniatura (prueba cache, local, luego servidor)
     * @param levelId Level ID
     * @param callback Callback with texture (or nullptr if not found)
     */
    void getThumbnail(int levelId, DownloadCallback callback);
    
    /**
     * sincronizar cola verificacion con servidor
     * @param category Category to sync (Verify, Update, Report)
     * @param callback Callback with items from server
     */
    void syncVerificationQueue(PendingCategory category, QueueCallback callback);
    
    /**
     * reclamar item cola verificacion (marcar en revision)
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void claimQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& type = "");
    
    /**
     * aceptar item cola verificacion
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void acceptQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& targetFilename = "", std::string const& type = "");
    
    /**
     * rechazar item cola verificacion
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param reason Rejection reason
     * @param callback Callback with success status
     */
    void rejectQueueItem(int levelId, PendingCategory category, std::string const& username, std::string const& reason, ActionCallback callback, std::string const& type = "");
    
    /**
     * enviar reporte al servidor
     * @param levelId Level ID
     * @param username Reporter username
     * @param note Report reason
     * @param callback Callback with success status
     */
    void submitReport(int levelId, std::string const& username, std::string const& note, ActionCallback callback);
    
    /**
     * aÃ±adir moderador (solo admin)
     * @param username Username to add
     * @param adminUser Admin username
     * @param callback Callback with success status
     */
    void addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);
    
    /**
     * quitar moderador (solo admin) - reutiliza endpoint add-moderator con action=remove
     * @param username Username to remove
     * @param adminUser Admin username
     * @param callback Callback with success status
     */
    void removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);

    /**
     * obtener top 100 creadores de miniaturas
     * @param callback Callback with raw json response
     */
    void getTopCreators(ActionCallback callback);

    /**
     * obtener top 100 mejores miniaturas rateadas
     * @param callback Callback with raw json response
     */
    void getTopThumbnails(ActionCallback callback);
    
    /**
     * borrar miniatura servidor (solo moderador)
     * @param levelId Level ID
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void deleteThumbnail(int levelId, std::string const& thumbnailId, std::string const& username, int accountID, ActionCallback callback);
    void reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, ActionCallback callback);
    
    // configuracion
    void setServerEnabled(bool enabled);

    // helper pa convertir datos a CCTexture2D
    cocos2d::CCTexture2D* webpToTexture(std::vector<uint8_t> const& webpData);

private:
    ThumbnailAPI();
    ~ThumbnailAPI() = default;
    
    ThumbnailAPI(const ThumbnailAPI&) = delete;
    ThumbnailAPI& operator=(const ThumbnailAPI&) = delete;

    // estado residual mantenido por compatibilidad — la logica real esta en los servicios
    bool m_serverEnabled = true;
    int m_uploadCount = 0;
};
