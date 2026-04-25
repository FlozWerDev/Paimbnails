#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <filesystem>
#include <cstdint>

/**
 * ProfileMusicManager - Gestiona la musica personalizada de perfiles
 * Descarga, cachea y reproduce fragmentos de audio de Newgrounds
 */
class ProfileMusicManager {
public:
    // Configuracion de musica del perfil
    struct ProfileMusicConfig {
        int songID = 0;           // ID de la cancion en Newgrounds
        int startMs = 0;          // Milisegundo de inicio
        int endMs = 20000;        // Milisegundo de fin (max 20 segundos)
        float volume = 0.7f;      // Volumen (0.0 - 1.0)
        bool enabled = true;      // Si esta habilitada
        std::string songName;     // Nombre de la cancion
        std::string artistName;   // Nombre del artista
        std::string updatedAt;    // Timestamp de ultima actualizacion (del servidor, para cache validation)
        bool isCustom = false;    // Si es una cancion custom subida por archivo
    };

    // Callbacks — Geode v5: CopyableFunction
    using ConfigCallback = geode::CopyableFunction<void(bool success, const ProfileMusicConfig& config)>;
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, std::string const& localPath)>;
    using WaveformCallback = geode::CopyableFunction<void(bool success, std::vector<float> const& peaks, int durationMs)>;
    using SongInfoCallback = geode::CopyableFunction<void(bool success, std::string const& name, std::string const& artist, int durationMs)>;

    static ProfileMusicManager& get() {
        static ProfileMusicManager instance;
        return instance;
    }

    // === CONFIGURACIoN ===

    /**
     * Obtiene la configuracion de musica de un perfil desde el servidor
     */
    void getProfileMusicConfig(int accountID, ConfigCallback callback);

    /**
     * Sube la configuracion de musica del perfil al servidor
     * El servidor descargara y cortara el audio de Newgrounds
     */
    void uploadProfileMusic(int accountID, std::string const& username, const ProfileMusicConfig& config, UploadCallback callback);

    /**
     * Sube una cancion custom desde archivo local al servidor
     * Recorta el fragmento segun startMs/endMs y codifica en base64
     * Requiere permisos de moderador, VIP o whitelist
     */
    void uploadCustomProfileMusic(int accountID, std::string const& username, std::string const& filePath, const ProfileMusicConfig& config, UploadCallback callback);

    /**
     * Verifica si el usuario actual tiene permisos para subir musica custom
     * (Moderador, VIP o Whitelist)
     */
    bool canUploadCustomMusic() const;

    /**
     * Elimina la musica del perfil
     */
    void deleteProfileMusic(int accountID, std::string const& username, UploadCallback callback);

    // === REPRODUCCIoN ===

    /**
     * Reproduce la musica del perfil de un usuario
     * Descarga el fragmento si no esta en cache
     */
    void playProfileMusic(int accountID);

    /**
     * Reproduce la musica del perfil usando una config ya obtenida del servidor.
     * Evita una segunda consulta al servidor si ya se tiene la config.
     */
    void playProfileMusic(int accountID, ProfileMusicConfig const& config);

    /**
     * Pausa la musica del perfil actual
     */
    void pauseProfileMusic();

    /**
     * Reanuda la musica pausada
     */
    void resumeProfileMusic();

    /**
     * Detiene completamente la musica del perfil
     */
    void stopProfileMusic();

    /**
     * Verifica si hay musica reproduciendose
     */
    bool isPlaying() const { return m_isPlaying; }

    /**
     * Verifica si la musica esta pausada
     */
    bool isPaused() const { return m_isPaused; }

    /**
     * Verifica si hay un fade-out en curso
     */
    bool isFadingOut() const { return m_isFadingOut; }

    /**
     * Verifica si el efecto cueva esta activo o en transicion.
     */
    bool hasCaveEffect() const { return m_caveEffectActive || m_caveTransitioning; }

    /**
     * Obtiene el accountID del perfil que esta sonando
     */
    int getCurrentPlayingProfile() const { return m_currentProfileID; }

    /**
     * Obtiene la amplitud actual del canal de musica del perfil (0.0 - 1.0)
     * Para usar en efectos visuales (pulso de brillo, etc)
     */
    float getCurrentAmplitude() const;

    /**
     * Aplica efecto "cueva" a la musica del perfil: lowpass filter + pitch mas lento.
     * Usado cuando se abre InfoLayer (comentarios) para distinguirlo del perfil.
     * Transicion suave con fade gradual del DSP.
     */
    void applyCaveEffect();

    /**
     * Quita el efecto "cueva" y restaura la reproduccion normal.
     * Transicion suave con fade gradual del DSP.
     */
    void removeCaveEffect();

    /**
     * Fuerza la eliminacion inmediata del efecto cueva (sin transicion).
     * Usar al salir de comentarios para evitar que el efecto quede colgado.
     */
    void forceRemoveCaveEffect();

    /**
     * Fuerza la detencion inmediata de toda la reproduccion.
     * Ignora fade-out en curso y limpia todo el estado.
     * Usar cuando se necesita garantizar que no hay audio activo.
     */
    void forceStop();

    // === WAVEFORM / VISUALIZACIoN ===

    /**
     * Obtiene los picos de audio de una cancion de Newgrounds para visualizacion
     * Descarga la cancion temporalmente y analiza su waveform
     */
    void getWaveformPeaks(int songID, WaveformCallback callback);

    /**
     * Obtiene informacion de una cancion de Newgrounds (nombre, artista, duracion)
     */
    void getSongInfo(int songID, SongInfoCallback callback);

    /**
     * Obtiene informacion de un archivo de audio local (duracion)
     */
    void getLocalSongInfo(std::string const& filePath, SongInfoCallback callback);

    /**
     * Obtiene los picos de audio de un archivo local para visualizacion
     */
    void getWaveformPeaksForFile(std::string const& filePath, WaveformCallback callback);

    /**
     * Descarga una cancion de Newgrounds para preview
     */
    void downloadSongForPreview(int songID, DownloadCallback callback);

    /**
     * Reproduce un preview de la cancion desde un punto especifico
     */
    void playPreview(std::string const& filePath, int startMs, int endMs);

    /**
     * Detiene el preview
     */
    void stopPreview();

    // === CACHE ===

    /**
     * Verifica si el fragmento de musica de un perfil esta en cache
     */
    bool isCached(int accountID);

    /**
     * Devuelve la config cacheada en RAM para un accountID, o nullptr si no existe.
     * Permite reproduccion optimista sin esperar al servidor.
     */
    const ProfileMusicConfig* getCachedConfig(int accountID) const;

    bool tryGetImmediateConfig(int accountID, ProfileMusicConfig& outConfig);

    // Inyectar config obtenida del bundle para evitar request individual
    void injectBundleConfig(int accountID, const ProfileMusicConfig& config);

    /**
     * Obtiene la ruta del archivo cacheado
     */
    std::filesystem::path getCachePath(int accountID);

    /**
     * Limpia el cache de musica de perfiles
     */
    void clearCache();

    /**
     * Invalida el cache (RAM + disco) para un accountID especifico.
     * Borra el archivo .mp3 cacheado, su .meta asociado,
     * y la entrada en m_configCache. Fuerza re-descarga la proxima vez.
     */
    void invalidateCache(int accountID);

    // === SETTINGS ===

    /**
     * Verifica si la musica de perfiles esta habilitada globalmente
     */
    bool isEnabled() const;

    /**
     * Obtiene el volumen de musica del juego (para aplicar a la musica de perfil)
     */
    float getGlobalVolume() const;

private:
    enum class PlaybackKind {
        None,
        Profile,
        Preview,
    };

    ProfileMusicManager();
    ~ProfileMusicManager() {
        m_lifetimeToken->store(false, std::memory_order_release);
    }

    ProfileMusicManager(const ProfileMusicManager&) = delete;
    ProfileMusicManager& operator=(const ProfileMusicManager&) = delete;

    // Estado de reproduccion
    bool m_isPlaying = false;
    bool m_isPaused = false;
    int m_currentProfileID = 0;
    uint32_t m_profileSessionToken = 0;
    std::string m_currentAudioPath;
    PlaybackKind m_playbackKind = PlaybackKind::None;

    // Parametros pendientes para carga tras dip fade
    int m_pendingStartMs = 0;
    int m_pendingEndMs = 0;
    bool m_pendingLoop = true;

    // Dip fade (solo canal principal, sin canales temporales)
    static constexpr int FADE_STEPS = 20;
    bool m_isFadingIn = false;
    bool m_isFadingOut = false;
    uint32_t m_fadeGeneration = 0;
    std::shared_ptr<std::atomic<bool>> m_lifetimeToken = std::make_shared<std::atomic<bool>>(true);
    float m_bgVolumeBeforeFade = 1.0f;
    unsigned int m_savedBgPosMs = 0;

    bool isCrossfadeEnabled() const;
    float getFadeDurationMs() const;

    void fadeInProfileMusic(float targetVolume);
    void fadeOutAndStop();
    void executeDipFadeOut(int step, int totalSteps, float volFrom, float volTo, bool restoreAfter, uint32_t generation);
    void executeDipFadeIn(int step, int totalSteps, float volFrom, float volTo, uint32_t generation);

    // Cache de configuraciones
    std::map<int, ProfileMusicConfig> m_configCache;
    static constexpr size_t MAX_CONFIG_CACHE_SIZE = 256;

    // Path del directorio de cache
    std::filesystem::path getCacheDir();

    // Path del archivo .meta asociado a un cache de audio
    std::filesystem::path getMetaPath(int accountID);

    // Guarda metadata de la config junto al archivo cacheado para detectar cambios
    void saveMetaFile(int accountID, ProfileMusicConfig const& config);

    // Verifica si el archivo cacheado corresponde a la config actual del servidor
    bool isCacheValid(int accountID, ProfileMusicConfig const& config);

    // Descarga el fragmento de audio del servidor
    void downloadMusicFragment(int accountID, DownloadCallback callback);

    // Analiza waveform de un archivo de audio y devuelve duracion
    std::vector<float> analyzeWaveform(std::string const& filePath, int numPeaks, int& outDurationMs);

    // Extrae un fragmento de audio como WAV
    std::vector<uint8_t> extractAudioFragment(std::string const& filePath, int startMs, int endMs);

    // Helpers
    void loadProfileOnMainChannel(const std::string& path, bool loop, int startMs, int endMs, float volume);
    void playAudioFile(std::string const& path, bool loop, int startMs = 0, int endMs = 0);
    void playProfileMusicWithConfig(int accountID, ProfileMusicConfig const& config);
    void stopOwnedAudioPlayback();
    void stopCurrentAudio(bool restoreContext = true);

    // Efecto cueva (lowpass + pitch)
    FMOD::DSP* m_lowpassDSP = nullptr;
    bool m_caveEffectActive = false;
    bool m_caveTransitioning = false;
    uint32_t m_caveGeneration = 0;
    float m_originalFrequency = 0.0f;
    float m_originalVolume = 0.0f;
    void executeCaveTransitionStep(int step, int totalSteps, float cutoffFrom, float cutoffTo,
                                    float freqFrom, float freqTo, float volFrom, float volTo, bool applying,
                                    uint32_t generation);
};



