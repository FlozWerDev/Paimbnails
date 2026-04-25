#include "ProfileMusicManager.hpp"
#include "../../audio/services/AudioContextCoordinator.hpp"
#include "../../dynamic-songs/services/DynamicSongManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/AudioInterop.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/file.hpp>
#include <memory>
#include <cmath>
#include <thread>
#include <chrono>

using namespace geode::prelude;

namespace {
constexpr auto PROFILE_MUSIC_CACHE_MAX_AGE = std::chrono::hours(24 * 30);

std::mutex& getProfileMusicCachePruneMutex() {
    static std::mutex mutex;
    return mutex;
}

std::filesystem::path getProfileMusicCacheDirStatic() {
    return Mod::get()->getSaveDir() / "profile_music";
}

size_t getProfileMusicCacheMaxBytes() {
    return std::clamp<size_t>(
        paimon::settings::quality::diskCacheBytes(),
        256ull * 1024ull * 1024ull,
        1024ull * 1024ull * 1024ull
    );
}

void removeProfileMusicCachePair(std::filesystem::path const& audioPath) {
    std::error_code ec;
    std::filesystem::remove(audioPath, ec);
    auto metaPath = audioPath;
    metaPath.replace_extension(".meta");
    ec.clear();
    std::filesystem::remove(metaPath, ec);
}

void pruneProfileMusicCache() {
    std::lock_guard<std::mutex> lock(getProfileMusicCachePruneMutex());

    auto cacheDir = getProfileMusicCacheDirStatic();
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        return;
    }

    struct CacheEntry {
        std::filesystem::path mp3Path;
        std::filesystem::file_time_type mtime;
        uintmax_t size = 0;
    };

    std::vector<CacheEntry> entries;
    uintmax_t totalBytes = 0;
    auto now = std::filesystem::file_time_type::clock::now();

    for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec || !entry.is_regular_file() || (entry.path().extension() != ".wav" && entry.path().extension() != ".mp3")) {
            continue;
        }

        std::error_code sizeEc;
        auto mp3Size = entry.file_size(sizeEc);
        if (sizeEc) {
            continue;
        }

        auto metaPath = entry.path();
        metaPath.replace_extension(".meta");
        uintmax_t totalEntrySize = mp3Size;
        if (std::filesystem::exists(metaPath, sizeEc) && !sizeEc) {
            sizeEc.clear();
            auto metaSize = std::filesystem::file_size(metaPath, sizeEc);
            if (!sizeEc) {
                totalEntrySize += metaSize;
            }
        }

        std::error_code timeEc;
        auto mtime = entry.last_write_time(timeEc);
        if (timeEc) {
            continue;
        }

        if (now - mtime > PROFILE_MUSIC_CACHE_MAX_AGE) {
            removeProfileMusicCachePair(entry.path());
            continue;
        }

        totalBytes += totalEntrySize;
        entries.push_back({entry.path(), mtime, totalEntrySize});
    }

    auto maxBytes = getProfileMusicCacheMaxBytes();
    if (totalBytes <= maxBytes) {
        return;
    }

    std::sort(entries.begin(), entries.end(), [](CacheEntry const& lhs, CacheEntry const& rhs) {
        return lhs.mtime < rhs.mtime;
    });

    for (auto const& entry : entries) {
        if (totalBytes <= maxBytes) {
            break;
        }

        removeProfileMusicCachePair(entry.mp3Path);
        totalBytes = (entry.size > totalBytes) ? 0 : (totalBytes - entry.size);
    }
}
}

// Helper: extrae el FMOD::Channel* subyacente del ChannelGroup de musica de fondo.
static FMOD::Channel* getMainBgChannel(FMODAudioEngine* engine) {
    if (!engine) return nullptr;
    if (auto* channel = engine->getActiveMusicChannel(0)) {
        return channel;
    }
    if (!engine->m_backgroundMusicChannel) return nullptr;
    int numCh = 0;
    engine->m_backgroundMusicChannel->getNumChannels(&numCh);
    if (numCh <= 0) return nullptr;
    FMOD::Channel* ch = nullptr;
    if (engine->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK) return nullptr;
    return ch;
}

ProfileMusicManager::ProfileMusicManager() {
    std::error_code ec;
    std::filesystem::create_directories(getCacheDir(), ec);
    pruneProfileMusicCache();
}

std::filesystem::path ProfileMusicManager::getCacheDir() {
    return getProfileMusicCacheDirStatic();
}

std::filesystem::path ProfileMusicManager::getCachePath(int accountID) {
    return getCacheDir() / fmt::format("{}.wav", accountID);
}

bool ProfileMusicManager::isCached(int accountID) {
    std::error_code ec;
    return std::filesystem::exists(getCachePath(accountID), ec);
}

const ProfileMusicManager::ProfileMusicConfig* ProfileMusicManager::getCachedConfig(int accountID) const {
    auto it = m_configCache.find(accountID);
    return it != m_configCache.end() ? &it->second : nullptr;
}

void ProfileMusicManager::injectBundleConfig(int accountID, const ProfileMusicConfig& config) {
    m_configCache[accountID] = config;
    while (m_configCache.size() > MAX_CONFIG_CACHE_SIZE) {
        m_configCache.erase(m_configCache.begin());
    }
}

bool ProfileMusicManager::tryGetImmediateConfig(int accountID, ProfileMusicConfig& outConfig) {
    if (auto it = m_configCache.find(accountID); it != m_configCache.end()) {
        outConfig = it->second;
        return true;
    }

    auto metaPath = getMetaPath(accountID);
    std::error_code ec;
    if (ec || !std::filesystem::exists(metaPath, ec)) {
        return false;
    }

    auto readRes = geode::utils::file::readString(metaPath);
    if (readRes.isErr()) return false;

    std::string content = readRes.unwrap();
    // Read only first line
    auto nlPos = content.find('\n');
    if (nlPos != std::string::npos) content = content.substr(0, nlPos);
    if (content.empty()) return false;

    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t sep = content.find('|', start);
        parts.push_back(content.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
        if (sep == std::string::npos) {
            break;
        }
        start = sep + 1;
    }

    if (parts.size() != 4) {
        return false;
    }

    ProfileMusicConfig config;
    config.songID = geode::utils::numFromString<int>(parts[0]).unwrapOr(0);
    config.startMs = geode::utils::numFromString<int>(parts[1]).unwrapOr(0);
    config.endMs = geode::utils::numFromString<int>(parts[2]).unwrapOr(20000);
    config.updatedAt = parts[3];
    if (config.songID <= 0 && !config.isCustom) {
        return false;
    }

    m_configCache[accountID] = config;
    while (m_configCache.size() > MAX_CONFIG_CACHE_SIZE) {
        m_configCache.erase(m_configCache.begin());
    }

    outConfig = config;
    return true;
}

bool ProfileMusicManager::isEnabled() const {
    return Mod::get()->getSettingValue<bool>("profile-music-enabled");
}

bool ProfileMusicManager::isCrossfadeEnabled() const {
    return Mod::get()->getSettingValue<bool>("profile-music-crossfade");
}

float ProfileMusicManager::getFadeDurationMs() const {
    float seconds = static_cast<float>(Mod::get()->getSettingValue<double>("profile-music-fade-duration"));
    return seconds * 1000.0f;
}

float ProfileMusicManager::getGlobalVolume() const {
    // Usar el volumen de musica configurado en los ajustes del juego
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine) {
        return engine->m_musicVolume;
    }
    return 1.0f;
}

void ProfileMusicManager::getProfileMusicConfig(int accountID, ConfigCallback callback) {
    // Siempre consultar al servidor para obtener la config mas reciente.
    // La frescura la resuelve el backend invalidando Cloudflare al subir/borrar,
    // asi evitamos convertir cada GET en una URL unica que nunca pega al cache.
    std::string endpoint = fmt::format("/api/profile-music/{}", accountID);
    log::info("[ProfileMusic] Fetching config from: {}", endpoint);

    HttpClient::get().get(endpoint, [this, accountID, callback](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([this, accountID, callback, success, response]() {
            if (!success) {
                log::error("[ProfileMusic] Failed to fetch config for account {}: {}", accountID, response);
                callback(false, ProfileMusicConfig{});
                return;
            }

            log::info("[ProfileMusic] Received response for account {}: {}", accountID, response.substr(0, 200));

            auto parsed = matjson::parse(response);
            if (!parsed.isOk()) {
                log::error("[ProfileMusic] Failed to parse JSON for account {}", accountID);
                callback(false, ProfileMusicConfig{});
                return;
            }

            auto root = parsed.unwrap();
            if (root.contains("error") || !root.contains("songID")) {
                log::warn("[ProfileMusic] No music config found for account {}", accountID);
                callback(false, ProfileMusicConfig{});
                return;
            }

            ProfileMusicConfig config;
            config.songID = root["songID"].asInt().unwrapOr(0);
            config.startMs = root["startMs"].asInt().unwrapOr(0);
            config.endMs = root["endMs"].asInt().unwrapOr(20000);
            config.volume = static_cast<float>(root["volume"].asDouble().unwrapOr(0.7));
            config.enabled = root["enabled"].asBool().unwrapOr(true);
            config.songName = root["songName"].asString().unwrapOr("");
            config.artistName = root["artistName"].asString().unwrapOr("");
            config.updatedAt = root["updatedAt"].asString().unwrapOr("");
            config.isCustom = root["isCustom"].asBool().unwrapOr(false);

            log::info("[ProfileMusic] Config loaded for account {}: songID={}, enabled={}", accountID, config.songID, config.enabled);

            m_configCache[accountID] = config;
            while (m_configCache.size() > MAX_CONFIG_CACHE_SIZE) {
                m_configCache.erase(m_configCache.begin());
            }
            callback(true, config);
        });
    });
}

void ProfileMusicManager::uploadProfileMusic(int accountID, std::string const& username, const ProfileMusicConfig& config, UploadCallback callback) {
    // Primero descargar la cancion completa localmente
    downloadSongForPreview(config.songID, [this, accountID, username, config, callback](bool success, std::string const& localPath) {
        if (!success || localPath.empty()) {
            Loader::get()->queueInMainThread([callback]() {
                callback(false, "Could not download song. Press the Download button first.");
            });
            return;
        }

        // Extraer el fragmento de audio en un thread separado
        std::thread([this, localPath, accountID, username, config, callback]() {
            if (paimon::isRuntimeShuttingDown()) {
                return;
            }

            // Extraer solo el fragmento usando FMOD
            auto fragmentData = extractAudioFragment(localPath, config.startMs, config.endMs);

            if (fragmentData.empty()) {
                Loader::get()->queueInMainThread([callback]() {
                    if (paimon::isRuntimeShuttingDown()) {
                        return;
                    }

                    callback(false, "Could not extract audio fragment");
                });
                return;
            }

            log::info("[ProfileMusic] Extracted fragment: {} bytes ({}ms - {}ms)",
                fragmentData.size(), config.startMs, config.endMs);

            // Convertir a base64
            static char const* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string base64;
            size_t size = fragmentData.size();
            base64.reserve(((size + 2) / 3) * 4);

            for (size_t i = 0; i < size; i += 3) {
                unsigned int n = static_cast<unsigned char>(fragmentData[i]) << 16;
                if (i + 1 < size) n |= static_cast<unsigned char>(fragmentData[i + 1]) << 8;
                if (i + 2 < size) n |= static_cast<unsigned char>(fragmentData[i + 2]);

                base64 += base64Chars[(n >> 18) & 0x3F];
                base64 += base64Chars[(n >> 12) & 0x3F];
                base64 += (i + 1 < size) ? base64Chars[(n >> 6) & 0x3F] : '=';
                base64 += (i + 2 < size) ? base64Chars[n & 0x3F] : '=';
            }

            log::info("[ProfileMusic] Uploading fragment: {} bytes ({} base64 chars)",
                size, base64.size());

            // Crear payload con el fragmento ya recortado
            matjson::Value payload;
            payload["accountID"] = accountID;
            payload["username"] = username;
            payload["songID"] = config.songID;
            payload["startMs"] = config.startMs;
            payload["endMs"] = config.endMs;
            payload["volume"] = config.volume;
            payload["songName"] = config.songName;
            payload["artistName"] = config.artistName;
            payload["audioData"] = base64;

            std::string jsonData = payload.dump();

            if (paimon::isRuntimeShuttingDown()) {
                return;
            }

            Loader::get()->queueInMainThread([this, jsonData, accountID, config, callback]() {
                if (paimon::isRuntimeShuttingDown()) {
                    return;
                }

                HttpClient::get().postWithAuth("/api/profile-music/upload", jsonData, [this, accountID, config, callback](bool success, std::string const& response) {
                    if (paimon::isRuntimeShuttingDown()) {
                        return;
                    }

                    Loader::get()->queueInMainThread([this, accountID, config, callback, success, response]() {
                        if (paimon::isRuntimeShuttingDown()) {
                            return;
                        }

                        if (success) {
                            // Invalidar todo el cache para este account (RAM + disco + meta)
                            // para forzar re-descarga del nuevo fragmento
                            invalidateCache(accountID);
                            callback(true, "Music uploaded successfully");
                        } else {
                            callback(false, response);
                        }
                    });
                });
            });
        }).detach();
    });
}

std::vector<uint8_t> ProfileMusicManager::extractAudioFragment(std::string const& filePath, int startMs, int endMs) {
    std::vector<uint8_t> result;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        log::error("[ProfileMusic] FMOD not available");
        return result;
    }

    // Create a fully decoded sound sample in memory
    FMOD::Sound* sound = nullptr;
    FMOD_RESULT res = engine->m_system->createSound(filePath.c_str(), FMOD_CREATESAMPLE, nullptr, &sound);
    if (res != FMOD_OK || !sound) {
        log::error("[ProfileMusic] Cannot decode sound: FMOD error {}", static_cast<int>(res));
        return result;
    }

    // Get duration
    unsigned int totalDurationMs = 0;
    sound->getLength(&totalDurationMs, FMOD_TIMEUNIT_MS);
    if (totalDurationMs == 0) {
        sound->release();
        return result;
    }

    // Validate ranges
    if (endMs > static_cast<int>(totalDurationMs) || endMs <= 0) endMs = static_cast<int>(totalDurationMs);
    if (startMs < 0) startMs = 0;
    if (startMs >= endMs) {
        sound->release();
        return result;
    }

    // Get format info
    FMOD_SOUND_TYPE sndType;
    FMOD_SOUND_FORMAT sndFormat;
    int channels = 0, bits = 0;
    sound->getFormat(&sndType, &sndFormat, &channels, &bits);

    float frequency = 44100.f;
    sound->getDefaults(&frequency, nullptr);

    int sampleRate = static_cast<int>(frequency);
    int bytesPerSample = channels * (bits / 8);

    // Get total PCM byte count
    unsigned int totalPcmBytes = 0;
    sound->getLength(&totalPcmBytes, FMOD_TIMEUNIT_PCMBYTES);

    // Calculate byte offsets for the fragment
    unsigned int startByte = static_cast<unsigned int>(
        static_cast<double>(startMs) / totalDurationMs * totalPcmBytes);
    unsigned int endByte = static_cast<unsigned int>(
        static_cast<double>(endMs) / totalDurationMs * totalPcmBytes);

    // Align to sample boundaries
    if (bytesPerSample > 0) {
        startByte = (startByte / bytesPerSample) * bytesPerSample;
        endByte = (endByte / bytesPerSample) * bytesPerSample;
    }

    if (endByte <= startByte || endByte > totalPcmBytes) {
        sound->release();
        return result;
    }

    unsigned int fragmentBytes = endByte - startByte;

    // Lock the buffer region for direct access
    void* ptr1 = nullptr, *ptr2 = nullptr;
    unsigned int len1 = 0, len2 = 0;
    res = sound->lock(startByte, fragmentBytes, &ptr1, &ptr2, &len1, &len2);
    if (res != FMOD_OK) {
        log::error("[ProfileMusic] Cannot lock sound buffer: FMOD error {}", static_cast<int>(res));
        sound->release();
        return result;
    }

    // Copy PCM data
    std::vector<uint8_t> pcmData(fragmentBytes);
    size_t written = 0;
    if (ptr1 && len1 > 0) {
        std::memcpy(pcmData.data() + written, ptr1, len1);
        written += len1;
    }
    if (ptr2 && len2 > 0) {
        std::memcpy(pcmData.data() + written, ptr2, len2);
        written += len2;
    }
    pcmData.resize(written);

    sound->unlock(ptr1, ptr2, len1, len2);
    sound->release();

    if (pcmData.empty()) {
        log::error("[ProfileMusic] No PCM data extracted");
        return result;
    }

    // Write WAV file
    auto writeU16 = [&](uint16_t v) {
        result.push_back(static_cast<uint8_t>(v & 0xFF));
        result.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    };
    auto writeU32 = [&](uint32_t v) {
        result.push_back(static_cast<uint8_t>(v & 0xFF));
        result.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        result.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };

    uint32_t dataSize = static_cast<uint32_t>(pcmData.size());
    uint16_t numChannels = static_cast<uint16_t>(channels);
    uint16_t bitsPerSample = static_cast<uint16_t>(bits);
    uint32_t byteRate = static_cast<uint32_t>(sampleRate) * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);

    // RIFF header
    result.push_back('R'); result.push_back('I'); result.push_back('F'); result.push_back('F');
    writeU32(36 + dataSize);
    result.push_back('W'); result.push_back('A'); result.push_back('V'); result.push_back('E');

    // fmt chunk
    result.push_back('f'); result.push_back('m'); result.push_back('t'); result.push_back(' ');
    writeU32(16);
    writeU16(1);  // PCM format
    writeU16(numChannels);
    writeU32(static_cast<uint32_t>(sampleRate));
    writeU32(byteRate);
    writeU16(blockAlign);
    writeU16(bitsPerSample);

    // data chunk
    result.push_back('d'); result.push_back('a'); result.push_back('t'); result.push_back('a');
    writeU32(dataSize);

    // PCM samples
    result.insert(result.end(), pcmData.begin(), pcmData.end());

    log::info("[ProfileMusic] Created WAV fragment: {} bytes ({}ms-{}ms, {}Hz, {}ch, {}bit)",
        result.size(), startMs, endMs, sampleRate, channels, bits);

    return result;
}

void ProfileMusicManager::deleteProfileMusic(int accountID, std::string const& username, UploadCallback callback) {
    matjson::Value payload;
    payload["accountID"] = accountID;
    payload["username"] = username;

    std::string jsonData = payload.dump();

    HttpClient::get().postWithAuth("/api/profile-music/delete", jsonData, [this, accountID, callback](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([this, accountID, callback, success, response]() {
            if (success) {
                invalidateCache(accountID);
                callback(true, "Music deleted successfully");
            } else {
                callback(false, response);
            }
        });
    });
}

bool ProfileMusicManager::canUploadCustomMusic() const {
    return paimon::settings::moderation::isVerifiedModerator()
        || paimon::settings::moderation::isVerifiedAdmin()
        || paimon::settings::moderation::isVerifiedVip();
}

void ProfileMusicManager::uploadCustomProfileMusic(int accountID, std::string const& username,
    std::string const& filePath, ProfileMusicConfig const& config, UploadCallback callback) {

    if (!canUploadCustomMusic()) {
        Loader::get()->queueInMainThread([callback]() {
            callback(false, "You don't have permission to upload custom music. Requires Moderator, VIP, or Whitelist.");
        });
        return;
    }

    // Extraer el fragmento de audio en un thread separado
    std::thread([this, filePath, accountID, username, config, callback]() {
        if (paimon::isRuntimeShuttingDown()) {
            return;
        }

        auto fragmentData = extractAudioFragment(filePath, config.startMs, config.endMs);

        if (fragmentData.empty()) {
            Loader::get()->queueInMainThread([callback]() {
                if (paimon::isRuntimeShuttingDown()) return;
                callback(false, "Could not extract audio fragment from file");
            });
            return;
        }

        log::info("[ProfileMusic] Extracted custom fragment: {} bytes ({}ms - {}ms)",
            fragmentData.size(), config.startMs, config.endMs);

        // Convertir a base64
        static char const* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string base64;
        size_t size = fragmentData.size();
        base64.reserve(((size + 2) / 3) * 4);

        for (size_t i = 0; i < size; i += 3) {
            unsigned int n = static_cast<unsigned char>(fragmentData[i]) << 16;
            if (i + 1 < size) n |= static_cast<unsigned char>(fragmentData[i + 1]) << 8;
            if (i + 2 < size) n |= static_cast<unsigned char>(fragmentData[i + 2]);

            base64 += base64Chars[(n >> 18) & 0x3F];
            base64 += base64Chars[(n >> 12) & 0x3F];
            base64 += (i + 1 < size) ? base64Chars[(n >> 6) & 0x3F] : '=';
            base64 += (i + 2 < size) ? base64Chars[n & 0x3F] : '=';
        }

        log::info("[ProfileMusic] Uploading custom fragment: {} bytes ({} base64 chars)",
            size, base64.size());

        // Crear payload con el fragmento recortado
        matjson::Value payload;
        payload["accountID"] = accountID;
        payload["username"] = username;
        payload["songID"] = config.songID;
        payload["startMs"] = config.startMs;
        payload["endMs"] = config.endMs;
        payload["volume"] = config.volume;
        payload["songName"] = config.songName;
        payload["artistName"] = config.artistName;
        payload["audioData"] = base64;
        payload["isCustom"] = true;

        std::string jsonData = payload.dump();

        if (paimon::isRuntimeShuttingDown()) {
            return;
        }

        Loader::get()->queueInMainThread([this, jsonData, accountID, config, callback]() {
            if (paimon::isRuntimeShuttingDown()) return;

            HttpClient::get().postWithAuth("/api/profile-music/upload", jsonData, [this, accountID, config, callback](bool success, std::string const& response) {
                if (paimon::isRuntimeShuttingDown()) return;

                Loader::get()->queueInMainThread([this, accountID, config, callback, success, response]() {
                    if (paimon::isRuntimeShuttingDown()) return;

                    if (success) {
                        invalidateCache(accountID);
                        callback(true, "Custom music uploaded successfully");
                    } else {
                        callback(false, response);
                    }
                });
            });
        });
    }).detach();
}

void ProfileMusicManager::getLocalSongInfo(std::string const& filePath, SongInfoCallback callback) {
    std::thread([filePath, callback]() {
        if (paimon::isRuntimeShuttingDown()) return;

        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) {
            Loader::get()->queueInMainThread([callback]() {
                if (paimon::isRuntimeShuttingDown()) return;
                callback(false, "", "", 0);
            });
            return;
        }

        FMOD::Sound* sound = nullptr;
        FMOD_RESULT res = engine->m_system->createSound(filePath.c_str(), FMOD_OPENONLY | FMOD_ACCURATETIME, nullptr, &sound);
        if (res != FMOD_OK || !sound) {
            Loader::get()->queueInMainThread([callback]() {
                if (paimon::isRuntimeShuttingDown()) return;
                callback(false, "", "", 0);
            });
            return;
        }

        unsigned int lengthMs = 0;
        sound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
        sound->release();

        int durationMs = static_cast<int>(lengthMs);

        // Extract filename as song name
        std::filesystem::path p(filePath);
        std::string songName = geode::utils::string::pathToString(p.stem());
        std::string artistName = "Local File";

        Loader::get()->queueInMainThread([callback, songName, artistName, durationMs]() {
            if (paimon::isRuntimeShuttingDown()) return;
            callback(true, songName, artistName, durationMs);
        });
    }).detach();
}

void ProfileMusicManager::getWaveformPeaksForFile(std::string const& filePath, WaveformCallback callback) {
    std::thread([this, filePath, callback]() {
        if (paimon::isRuntimeShuttingDown()) return;

        int durationMs = 0;
        auto peaks = analyzeWaveform(filePath, 200, durationMs);

        if (paimon::isRuntimeShuttingDown()) return;

        Loader::get()->queueInMainThread([callback, peaks, durationMs]() {
            if (paimon::isRuntimeShuttingDown()) return;
            callback(!peaks.empty(), peaks, durationMs);
        });
    }).detach();
}

void ProfileMusicManager::downloadMusicFragment(int accountID, DownloadCallback callback) {
    // El servidor purga Cloudflare al subir/borrar, asi que no hace falta romper
    // el cache con un query param distinto en cada request.
    std::string url = fmt::format("{}/profile-music/{}", HttpClient::get().getServerURL(), accountID);

    log::info("[ProfileMusic] Downloading music from: {}", url);

    // Usar descarga binaria sin validacion de imagen (es un MP3, no una imagen)
    HttpClient::get().downloadFromUrlRaw(url, [this, accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
        if (!success || data.empty()) {
            log::error("[ProfileMusic] Failed to download music for account {}", accountID);
            Loader::get()->queueInMainThread([callback]() {
                callback(false, "");
            });
            return;
        }

        auto cachePath = getCachePath(accountID);
        std::error_code ec;
        std::filesystem::create_directories(cachePath.parent_path(), ec);

        auto writeRes = geode::utils::file::writeBinary(cachePath, data);
        if (writeRes.isOk()) {
            pruneProfileMusicCache();
            log::info("[ProfileMusic] Downloaded and cached music for account {} ({} bytes)", accountID, data.size());
            Loader::get()->queueInMainThread([callback, cachePath]() {
                callback(true, geode::utils::string::pathToString(cachePath));
            });
        } else {
            log::error("[ProfileMusic] Failed to write cache file for account {}: {}", accountID, writeRes.unwrapErr());
            Loader::get()->queueInMainThread([callback]() {
                callback(false, "");
            });
        }
    });
}

void ProfileMusicManager::playProfileMusic(int accountID) {
    log::info("[ProfileMusic] playProfileMusic called for account {}", accountID);

    if (!isEnabled()) {
        log::info("[ProfileMusic] Profile music is disabled in settings");
        return;
    }

    // No superponer musica de perfil sobre el audio del video
    if (paimon::isVideoAudioInteropActive()) {
        log::info("[ProfileMusic] Video audio is active, skipping");
        return;
    }

    // Respetar el toggle nativo de musica de menu de GD (variable 0122)
    if (GameManager::get()->getGameVariable("0122")) {
        log::info("[ProfileMusic] Menu music is toggled off (0122), skipping");
        return;
    }

    // Respetar el volumen de musica de GD
    auto engineCheck = FMODAudioEngine::sharedEngine();
    if (engineCheck && engineCheck->m_musicVolume <= 0.0f) {
        log::info("[ProfileMusic] Music volume is 0, skipping");
        return;
    }

    if (m_isPlaying && m_currentProfileID == accountID && !m_isPaused && !m_isFadingOut) {
        log::info("[ProfileMusic] Already playing music for this account");
        return;
    }

    forceStop();
    m_currentProfileID = accountID;

    // Consultar la config del servidor y delegar al overload con config
    getProfileMusicConfig(accountID, [this, accountID](bool success, ProfileMusicConfig const& config) {
        if (!success || (!config.enabled) || (config.songID <= 0 && !config.isCustom)) {
            log::info("[ProfileMusic] No valid config from server for account {}", accountID);
            return;
        }
        if (m_currentProfileID != accountID) {
            log::info("[ProfileMusic] Profile changed while fetching config, aborting");
            return;
        }
        playProfileMusicWithConfig(accountID, config);
    });
}

void ProfileMusicManager::playProfileMusic(int accountID, ProfileMusicConfig const& config) {
    log::info("[ProfileMusic] playProfileMusic (with config) called for account {}", accountID);

    if (!isEnabled()) {
        log::info("[ProfileMusic] Profile music is disabled in settings");
        return;
    }

    // No superponer musica de perfil sobre el audio del video
    if (paimon::isVideoAudioInteropActive()) {
        log::info("[ProfileMusic] Video audio is active, skipping");
        return;
    }

    // Respetar el toggle nativo de musica de menu de GD (variable 0122)
    if (GameManager::get()->getGameVariable("0122")) {
        log::info("[ProfileMusic] Menu music is toggled off (0122), skipping");
        return;
    }

    // Respetar el volumen de musica de GD
    auto engineCheck = FMODAudioEngine::sharedEngine();
    if (engineCheck && engineCheck->m_musicVolume <= 0.0f) {
        log::info("[ProfileMusic] Music volume is 0, skipping");
        return;
    }

    if (m_isPlaying && m_currentProfileID == accountID && !m_isPaused && !m_isFadingOut) {
        log::info("[ProfileMusic] Already playing music for this account");
        return;
    }

    forceStop();
    m_currentProfileID = accountID;
    playProfileMusicWithConfig(accountID, config);
}

void ProfileMusicManager::playProfileMusicWithConfig(int accountID, ProfileMusicConfig const& config) {
    auto cachePath = getCachePath(accountID);
    std::error_code cacheEc;
    bool cacheExists = std::filesystem::exists(cachePath, cacheEc) && !cacheEc;

    // Verificar si el cache en disco corresponde a la config actual del servidor
    if (cacheExists && isCacheValid(accountID, config)) {
        log::info("[ProfileMusic] Cache is valid and up-to-date, playing from cache: {}",
            geode::utils::string::pathToString(cachePath));
        playAudioFile(geode::utils::string::pathToString(cachePath), true, 0, 0);
        return;
    }

    // Cache inexistente o desactualizado — borrar y re-descargar
    if (cacheExists) {
        log::info("[ProfileMusic] Cache is stale (config changed), removing old file");
        std::filesystem::remove(cachePath, cacheEc);
    }

    log::info("[ProfileMusic] Downloading fresh music fragment...");
    downloadMusicFragment(accountID, [this, accountID, config](bool dlSuccess, std::string const& path) {
        if (dlSuccess && m_currentProfileID == accountID) {
            // Guardar .meta con la config actual para futuras validaciones
            saveMetaFile(accountID, config);
            log::info("[ProfileMusic] Download successful, playing: {}", path);
            playAudioFile(path, true, 0, 0);
        } else {
            log::error("[ProfileMusic] Download failed for account {}", accountID);
        }
    });
}

void ProfileMusicManager::playAudioFile(std::string const& path, bool loop, int startMs, int endMs) {
    auto generation = ++m_fadeGeneration;
    m_profileSessionToken = AudioContextCoordinator::get().getCurrentProfileSessionToken();
    m_playbackKind = PlaybackKind::Profile;

    // Cancelar fades y limpiar estado
    m_isFadingIn = false;
    m_isFadingOut = false;
    m_isPlaying = false;
    m_isPaused = false;

    // Limpiar efecto cueva si estaba activo
    forceRemoveCaveEffect();

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        paimon::setProfileMusicInteropActive(false);
        return;
    }

    float gameVolume = engine->m_musicVolume;
    m_bgVolumeBeforeFade = gameVolume;
    m_currentAudioPath = path;
    m_pendingStartMs = startMs;
    m_pendingEndMs = endMs;
    m_pendingLoop = loop;

    bool useCrossfade = isCrossfadeEnabled();
    bool dynamicNeedsSuspension = AudioContextCoordinator::get().shouldSuspendDynamicForProfileMusic();

    // Guardar posicion de lo que sea que este sonando para restaurar luego
    m_savedBgPosMs = engine->getMusicTimeMS(0);

    if (useCrossfade || dynamicNeedsSuspension) {
        // Dip fade: bajar volumen del main → a vol=0 cargar profile → subir
        float currentVol = 0.0f;
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->getVolume(&currentVol);
        }

        if (currentVol > 0.001f) {
            m_isFadingOut = true;
            // restoreAfter=false → al llegar a 0, cargamos la musica de perfil
            executeDipFadeOut(0, FADE_STEPS, currentVol, 0.0f, false, generation);
        } else {
            // Ya estamos en silencio — notificar dynamic y cargar directo
            AudioContextCoordinator::get().suspendDynamicForProfileMusicIfNeeded();
            loadProfileOnMainChannel(path, loop, startMs, endMs, 0.0f);
            m_isPlaying = true;
            m_isFadingIn = true;
            executeDipFadeIn(0, FADE_STEPS, 0.0f, gameVolume, generation);
        }
    } else {
        // Sin crossfade, sin dynamic: parar BG y cargar directo
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->stop();
        }
        loadProfileOnMainChannel(path, loop, startMs, endMs, gameVolume);
        m_isPlaying = true;
    }
}

void ProfileMusicManager::loadProfileOnMainChannel(const std::string& path, bool loop, int startMs, int endMs, float volume) {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    DynamicSongManager::s_selfPlayMusic = true;
    engine->playMusic(path, loop, 0.0f, 0);
    DynamicSongManager::s_selfPlayMusic = false;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(volume);
    }

    paimon::setProfileMusicInteropActive(true);
    if (m_playbackKind == PlaybackKind::Preview) {
        AudioContextCoordinator::get().claimPreviewAudio(m_profileSessionToken);
    } else {
        AudioContextCoordinator::get().claimProfileAudio(m_profileSessionToken);
    }

    // Configurar loop points y posicion via Channel*
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return;

    FMOD::Sound* sound = nullptr;
    bgCh->getCurrentSound(&sound);
    if (!sound) return;

    unsigned int lengthMs = 0;
    sound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);

    if (startMs == 0 && endMs == 0) {
        endMs = static_cast<int>(lengthMs);
    }
    if (endMs > static_cast<int>(lengthMs) || endMs <= 0) endMs = static_cast<int>(lengthMs);
    if (startMs >= endMs) startMs = 0;

    sound->setLoopPoints(
        static_cast<unsigned int>(startMs), FMOD_TIMEUNIT_MS,
        static_cast<unsigned int>(endMs), FMOD_TIMEUNIT_MS
    );

    if (startMs > 0) {
        bgCh->setPosition(static_cast<unsigned int>(startMs), FMOD_TIMEUNIT_MS);
    }

    log::info("[ProfileMusic] Loaded on main channel: {} ({}ms-{}ms, vol:{:.2f})", path, startMs, endMs, volume);
}

void ProfileMusicManager::fadeInProfileMusic(float targetVolume) {
    auto generation = ++m_fadeGeneration;
    executeDipFadeIn(0, FADE_STEPS, 0.0f, targetVolume, generation);
}

void ProfileMusicManager::fadeOutAndStop() {
    auto generation = ++m_fadeGeneration;
    m_isFadingOut = true;
    m_isFadingIn = false;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    float currentVol = 0.0f;
    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->getVolume(&currentVol);
    }

    // Actualizar volumen objetivo (por si cambio durante la reproduccion)
    m_bgVolumeBeforeFade = engine->m_musicVolume;

    log::info("[ProfileMusic] Starting dip fade-out (vol: {:.2f}, bg target: {:.2f})",
        currentVol, m_bgVolumeBeforeFade);

    // Dip fade: bajar main a 0 → cargar menu/dynamic → subir
    // restoreAfter=true: al llegar a 0 se restaura la musica anterior
    executeDipFadeOut(0, FADE_STEPS, currentVol, 0.0f, true, generation);
}

// ─── Dip Fade: fade-out del canal principal ──────────────────────────
// Al llegar a volTo (0), carga la musica correspondiente y hace fade-in.
void ProfileMusicManager::executeDipFadeOut(int step, int totalSteps,
    float volFrom, float volTo, bool restoreAfter, uint32_t generation) {
    if (generation != m_fadeGeneration) {
        return;
    }

    if (step > totalSteps || !m_isFadingOut) {
        // Fade-out terminado
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(0.0f);
        }

        if (restoreAfter) {
            // Saliendo de profile music: detener canal y devolver ownership antes de restaurar contexto
            auto sessionToken = m_profileSessionToken;
            stopOwnedAudioPlayback();
            m_isFadingOut = false;
            AudioContextCoordinator::get().restoreAfterProfileMusicStop(true, sessionToken);
            log::info("[ProfileMusic] Fade-out complete, context restored by coordinator");
        } else {
            // Entrando a profile music: notificar dynamic y cargar profile
            AudioContextCoordinator::get().suspendDynamicForProfileMusicIfNeeded();
            loadProfileOnMainChannel(m_currentAudioPath, m_pendingLoop,
                                     m_pendingStartMs, m_pendingEndMs, 0.0f);
            m_isPlaying = true;
            m_isFadingOut = false;
            m_isFadingIn = true;
            float gameVolume = engine ? engine->m_musicVolume : 1.0f;
            executeDipFadeIn(0, totalSteps, 0.0f, gameVolume, generation);
        }
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f*t*t) : (1.f - std::pow(-2.f*t+2.f, 2.f)/2.f);
    float vol = volFrom + (volTo - volFrom) * eT;

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
    }

    float stepDelay = getFadeDurationMs() / static_cast<float>(totalSteps) / 1000.f;
    int next = step + 1;

    auto lt = m_lifetimeToken;
    paimon::scheduleMainThreadDelay(stepDelay, [lt, this, next, totalSteps, volFrom, volTo, restoreAfter, generation]() {
        if (!lt || !lt->load(std::memory_order_acquire)) return;
        if (generation != m_fadeGeneration || !m_isFadingOut) return;
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_backgroundMusicChannel) return;
        executeDipFadeOut(next, totalSteps, volFrom, volTo, restoreAfter, generation);
    });
}

// ─── Dip Fade: fade-in del canal principal ───────────────────────────
void ProfileMusicManager::executeDipFadeIn(int step, int totalSteps,
    float volFrom, float volTo, uint32_t generation) {
    if (generation != m_fadeGeneration) {
        return;
    }

    if (step > totalSteps || !m_isFadingIn) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(volTo);
        }
        m_isFadingIn = false;
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f*t*t) : (1.f - std::pow(-2.f*t+2.f, 2.f)/2.f);
    float vol = volFrom + (volTo - volFrom) * eT;

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
    }

    float stepDelay = getFadeDurationMs() / static_cast<float>(totalSteps) / 1000.f;
    int next = step + 1;

    auto lt = m_lifetimeToken;
    paimon::scheduleMainThreadDelay(stepDelay, [lt, this, next, totalSteps, volFrom, volTo, generation]() {
        if (!lt || !lt->load(std::memory_order_acquire)) return;
        if (generation != m_fadeGeneration || !m_isFadingIn) return;
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_backgroundMusicChannel) return;
        executeDipFadeIn(next, totalSteps, volFrom, volTo, generation);
    });
}

void ProfileMusicManager::stopOwnedAudioPlayback() {
    ++m_fadeGeneration;
    ++m_caveGeneration;

    // Cancelar cualquier fade en curso
    m_isFadingIn = false;
    m_isFadingOut = false;
    m_caveTransitioning = false;

    // Limpiar efecto cueva si estaba activo
    forceRemoveCaveEffect();

    // Siempre parar el canal principal — incluso si los flags de ownership
    // estan desincronizados (e.g., playAudioFile no termino, callback async).
    // Si no somos dueños, al menos silenciar para que la restauracion
    // posterior (menu/dynamic) pueda tomar control limpio.
    {
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->stop();
        }
    }

    m_isPlaying = false;
    m_isPaused = false;
    m_isFadingIn = false;
    m_isFadingOut = false;
    m_currentProfileID = 0;
    m_currentAudioPath.clear();
    m_pendingStartMs = 0;
    m_pendingEndMs = 0;
    m_pendingLoop = true;
    m_savedBgPosMs = 0;
    m_bgVolumeBeforeFade = 1.0f;
    m_playbackKind = PlaybackKind::None;
    paimon::setProfileMusicInteropActive(false);
    AudioContextCoordinator::get().releaseProfileLikeAudio(m_profileSessionToken);
}

void ProfileMusicManager::stopCurrentAudio(bool restoreContext) {
    auto sessionToken = m_profileSessionToken;
    stopOwnedAudioPlayback();
    if (restoreContext) {
        AudioContextCoordinator::get().restoreAfterProfileMusicStop(true, sessionToken);
    }
}

void ProfileMusicManager::pauseProfileMusic() {
    if (m_playbackKind == PlaybackKind::Profile && m_isPlaying) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setPaused(true);
        }
        m_isPaused = true;
        log::info("[ProfileMusic] Paused");
    }
}

void ProfileMusicManager::resumeProfileMusic() {
    if (m_playbackKind == PlaybackKind::Profile && m_isPaused) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setPaused(false);
        }
        m_isPaused = false;
        log::info("[ProfileMusic] Resumed");
    }
}

void ProfileMusicManager::stopProfileMusic() {
    if (m_playbackKind == PlaybackKind::Preview) {
        stopPreview();
        return;
    }

    // Si no hay nada reproduciendose ni en fade, no tocar nada
    if (!m_isPlaying && !m_isPaused && !m_isFadingOut && !m_isFadingIn) {
        return;
    }

    // Si ya hay un fade-out en curso, forzar parada inmediata en vez de ignorar
    if (m_isFadingOut) {
        log::info("[ProfileMusic] Fade-out in progress, forcing immediate stop");
        forceStop();
        AudioContextCoordinator::get().restoreAfterProfileMusicStop(true, m_profileSessionToken);
        return;
    }

    if (m_isPlaying && isCrossfadeEnabled()) {
        // Usar fade-out suave
        log::info("[ProfileMusic] Starting fade-out transition");
        fadeOutAndStop();
    } else {
        // Sin crossfade o sin reproduccion activa: parar inmediatamente
        stopCurrentAudio(true);
        log::info("[ProfileMusic] Stopped immediately");
    }
}

void ProfileMusicManager::getSongInfo(int songID, SongInfoCallback callback) {
    auto mdm = MusicDownloadManager::sharedState();
    if (!mdm) {
        callback(false, "", "", 0);
        return;
    }

    auto songInfo = mdm->getSongInfoObject(songID);
    if (songInfo) {
        std::string name = songInfo->m_songName;
        std::string artist = songInfo->m_artistName;
        // Duration will be determined when loading the audio file
        // For now we return 0 and let the waveform analysis determine it
        callback(true, name, artist, 0);
        return;
    }

    // Request song info from GD servers
    mdm->getSongInfo(songID, true);

    auto attempts = std::make_shared<int>(0);
    auto poll = std::make_shared<geode::CopyableFunction<void()>>();
    *poll = [songID, callback, attempts, poll]() {
        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) {
            auto cb = callback; // copia local antes de destruir closure
            *poll = {};
            cb(false, "", "", 0);
            return;
        }

        auto songInfo = mdm->getSongInfoObject(songID);
        if (songInfo) {
            std::string name = songInfo->m_songName;
            std::string artist = songInfo->m_artistName;
            auto cb = callback; // copia local antes de destruir closure
            *poll = {};
            cb(true, name, artist, 0);
            return;
        }

        if (++(*attempts) >= 20) {
            auto cb = callback; // copia local antes de destruir closure
            *poll = {};
            cb(false, "", "", 0);
            return;
        }

        // MusicDownloadManager resuelve esto de forma async y no expone callback de finalizacion.
        paimon::scheduleMainThreadDelay(0.5f, [poll]() {
            if (*poll) {
                (*poll)();
            }
        });
    };
    (*poll)();
}

void ProfileMusicManager::downloadSongForPreview(int songID, DownloadCallback callback) {
    auto mdm = MusicDownloadManager::sharedState();
    if (!mdm) {
        callback(false, "");
        return;
    }

    if (mdm->isSongDownloaded(songID)) {
        std::string path = mdm->pathForSong(songID);
        callback(true, path);
        return;
    }

    mdm->downloadSong(songID);

    auto attempts = std::make_shared<int>(0);
    auto poll = std::make_shared<geode::CopyableFunction<void()>>();
    *poll = [songID, callback, attempts, poll]() {
        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) {
            auto cb = callback; // copia local antes de destruir closure
            *poll = {};
            cb(false, "");
            return;
        }

        if (mdm->isSongDownloaded(songID)) {
            std::string path = mdm->pathForSong(songID);
            auto cb = callback; // copia local antes de destruir closure
            *poll = {};
            cb(true, path);
            return;
        }

        if (++(*attempts) >= 30) {
            auto cb = callback; // copia local antes de destruir closure
            *poll = {};
            cb(false, "");
            return;
        }

        paimon::scheduleMainThreadDelay(1.0f, [poll]() {
            if (*poll) {
                (*poll)();
            }
        });
    };
    (*poll)();
}

void ProfileMusicManager::getWaveformPeaks(int songID, WaveformCallback callback) {
    downloadSongForPreview(songID, [this, callback](bool success, std::string const& path) {
        if (!success || path.empty()) {
            callback(false, {}, 0);
            return;
        }

        std::thread([this, path, callback]() {
            if (paimon::isRuntimeShuttingDown()) {
                return;
            }

            int durationMs = 0;
            auto peaks = analyzeWaveform(path, 200, durationMs);

            if (paimon::isRuntimeShuttingDown()) {
                return;
            }

            Loader::get()->queueInMainThread([callback, peaks, durationMs]() {
                if (paimon::isRuntimeShuttingDown()) {
                    return;
                }

                callback(!peaks.empty(), peaks, durationMs);
            });
        }).detach();
    });
}

std::vector<float> ProfileMusicManager::analyzeWaveform(std::string const& filePath, int numPeaks, int& outDurationMs) {
    std::vector<float> peaks(numPeaks, 0.0f);
    outDurationMs = 0;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return peaks;

    FMOD::System* system = engine->m_system;
    FMOD::Sound* sound = nullptr;

    FMOD_RESULT result = system->createSound(filePath.c_str(), FMOD_DEFAULT | FMOD_OPENONLY, nullptr, &sound);
    if (result != FMOD_OK || !sound) {
        return peaks;
    }

    // Get duration in milliseconds
    unsigned int lengthMs = 0;
    sound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    outDurationMs = static_cast<int>(lengthMs);

    unsigned int length = 0;
    sound->getLength(&length, FMOD_TIMEUNIT_PCMBYTES);

    FMOD_SOUND_FORMAT format;
    int channels, bits;
    sound->getFormat(nullptr, &format, &channels, &bits);

    if (length == 0 || bits == 0) {
        sound->release();
        return peaks;
    }

    unsigned int bytesPerSample = bits / 8;
    unsigned int totalSamples = length / (bytesPerSample * channels);
    unsigned int samplesPerPeak = totalSamples / numPeaks;

    if (samplesPerPeak == 0) samplesPerPeak = 1;

    std::vector<char> buffer(length);
    unsigned int read = 0;
    result = sound->readData(buffer.data(), length, &read);

    if (result != FMOD_OK && result != FMOD_ERR_FILE_EOF) {
        sound->release();
        return peaks;
    }

    for (int i = 0; i < numPeaks && static_cast<unsigned int>(i) * samplesPerPeak < totalSamples; i++) {
        float maxSample = 0.0f;

        unsigned int startSample = i * samplesPerPeak;
        unsigned int endSample = std::min(startSample + samplesPerPeak, totalSamples);

        for (unsigned int s = startSample; s < endSample; s++) {
            for (int c = 0; c < channels; c++) {
                unsigned int byteIndex = (s * channels + c) * bytesPerSample;
                if (byteIndex + bytesPerSample > read) continue;

                float sample = 0.0f;
                if (bits == 16) {
                    int16_t* ptr = reinterpret_cast<int16_t*>(&buffer[byteIndex]);
                    sample = std::abs(static_cast<float>(*ptr) / 32768.0f);
                } else if (bits == 8) {
                    sample = std::abs((static_cast<float>(buffer[byteIndex]) - 128.0f) / 128.0f);
                } else if (bits == 32) {
                    float* ptr = reinterpret_cast<float*>(&buffer[byteIndex]);
                    sample = std::abs(*ptr);
                }

                if (sample > maxSample) maxSample = sample;
            }
        }

        peaks[i] = maxSample;
    }

    sound->release();

    float maxPeak = 0.0f;
    for (float p : peaks) {
        if (p > maxPeak) maxPeak = p;
    }

    if (maxPeak > 0.0f) {
        for (float& p : peaks) {
            p /= maxPeak;
        }
    }

    return peaks;
}

void ProfileMusicManager::playPreview(std::string const& filePath, int startMs, int endMs) {
    forceStop();
    m_profileSessionToken = AudioContextCoordinator::get().getCurrentProfileSessionToken();
    m_playbackKind = PlaybackKind::Preview;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    // Respetar el toggle nativo de musica de menu de GD (variable 0122)
    if (GameManager::get()->getGameVariable("0122")) return;
    if (engine->m_musicVolume <= 0.0f) return;

    float gameVolume = engine->m_musicVolume;

    // Guardar posicion actual para restaurar al salir del preview
    m_savedBgPosMs = engine->getMusicTimeMS(0);

    // Cargar preview en el canal principal
    loadProfileOnMainChannel(filePath, true, startMs, endMs, gameVolume);
    m_isPlaying = true;
    m_isPaused = false;
}

void ProfileMusicManager::stopPreview() {
    if (m_playbackKind != PlaybackKind::Preview) {
        return;
    }
    stopCurrentAudio(true);
}

void ProfileMusicManager::clearCache() {
    std::error_code ec;
    auto cacheDir = getCacheDir();

    if (std::filesystem::exists(cacheDir, ec)) {
        for (auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
            std::filesystem::remove(entry.path(), ec);
        }
    }

    m_configCache.clear();
    log::info("[ProfileMusic] Cache cleared");
}

void ProfileMusicManager::invalidateCache(int accountID) {
    // Limpiar config de RAM
    m_configCache.erase(accountID);

    std::error_code ec;

    // Borrar archivo de audio cacheado
    auto cachePath = getCachePath(accountID);
    if (std::filesystem::exists(cachePath, ec)) {
        std::filesystem::remove(cachePath, ec);
    }

    // Borrar archivo .meta asociado
    auto metaPath = getMetaPath(accountID);
    if (std::filesystem::exists(metaPath, ec)) {
        std::filesystem::remove(metaPath, ec);
    }

    log::info("[ProfileMusic] Cache invalidated for account {}", accountID);
}

std::filesystem::path ProfileMusicManager::getMetaPath(int accountID) {
    return getCacheDir() / fmt::format("{}.meta", accountID);
}

void ProfileMusicManager::saveMetaFile(int accountID, ProfileMusicConfig const& config) {
    auto metaPath = getMetaPath(accountID);
    std::error_code ec;
    std::filesystem::create_directories(metaPath.parent_path(), ec);

    // Guardar songID|startMs|endMs|updatedAt para comparacion.
    // updatedAt permite detectar re-uploads de la misma cancion con el mismo rango.
    std::string content = fmt::format("{}|{}|{}|{}", config.songID, config.startMs, config.endMs, config.updatedAt);
    auto writeRes = geode::utils::file::writeString(metaPath, content);
    if (writeRes.isOk()) {
        pruneProfileMusicCache();
        log::info("[ProfileMusic] Saved meta for account {}: songID={}, {}ms-{}ms, updatedAt={}",
            accountID, config.songID, config.startMs, config.endMs, config.updatedAt);
    }
}

bool ProfileMusicManager::isCacheValid(int accountID, ProfileMusicConfig const& config) {
    auto metaPath = getMetaPath(accountID);
    std::error_code ec;
    if (!std::filesystem::exists(metaPath, ec)) {
        // No hay .meta — cache es legacy o invalido
        log::info("[ProfileMusic] No meta file for account {}, cache is invalid", accountID);
        return false;
    }

    auto metaReadRes = geode::utils::file::readString(metaPath);
    if (metaReadRes.isErr()) return false;

    std::string content = metaReadRes.unwrap();
    auto nlPos = content.find('\n');
    if (nlPos != std::string::npos) content = content.substr(0, nlPos);

    // Formato: songID|startMs|endMs|updatedAt
    std::string expected = fmt::format("{}|{}|{}|{}", config.songID, config.startMs, config.endMs, config.updatedAt);

    // Si updatedAt esta vacio (servidor no lo provee), el cache nunca sera valido
    // para forzar siempre la re-descarga — esto es intencional como fallback seguro.
    if (config.updatedAt.empty()) {
        log::info("[ProfileMusic] Server does not provide updatedAt, cache considered invalid (safe fallback)");
        return false;
    }

    bool valid = (content == expected);
    if (!valid) {
        log::info("[ProfileMusic] Cache meta mismatch for account {}: cached='{}', server='{}'",
            accountID, content, expected);
    }
    return valid;
}

void ProfileMusicManager::applyCaveEffect() {
    // Evitar reentrada: agregar/remover el mismo DSP durante transiciones
    // puede desestabilizar FMOD en cambios rapidos de layer.
    if (m_caveEffectActive || m_caveTransitioning) return;
    if (!m_isPlaying) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return;

    // Crear lowpass DSP si no existe
    if (!m_lowpassDSP) {
        FMOD_RESULT res = engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &m_lowpassDSP);
        if (res != FMOD_OK || !m_lowpassDSP) {
            log::error("[ProfileMusic] Failed to create lowpass DSP");
            return;
        }
    }

    // Empezar con cutoff alto (sin efecto) y transicionar a bajo
    m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 22000.0f);
    m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 1.0f);
    engine->m_backgroundMusicChannel->addDSP(0, m_lowpassDSP);

    // Guardar estado original via Channel*
    auto* bgCh = getMainBgChannel(engine);
    if (bgCh) {
        bgCh->getFrequency(&m_originalFrequency);
    }
    engine->m_backgroundMusicChannel->getVolume(&m_originalVolume);

    m_caveEffectActive = true;
    m_caveTransitioning = true;
    auto generation = ++m_caveGeneration;

    log::info("[ProfileMusic] Cave effect: starting smooth transition IN (freq:{:.0f}, vol:{:.2f})",
        m_originalFrequency, m_originalVolume);

    // Transicion suave: cutoff 22000→800, freq 1.0→0.92, vol 1.0→0.6
    static constexpr int CAVE_STEPS = 15;
    executeCaveTransitionStep(0, CAVE_STEPS,
        22000.0f, 800.0f,
        m_originalFrequency, m_originalFrequency * 0.92f,
        m_originalVolume, m_originalVolume * 0.6f,
        true, generation);
}

void ProfileMusicManager::forceRemoveCaveEffect() {
    // Limpiar siempre si hay un DSP pendiente, incluso si los flags ya fueron limpiados
    // (forceStop() limpia m_caveTransitioning antes de llamar aqui)
    if (!m_caveEffectActive && !m_caveTransitioning && !m_lowpassDSP) return;

    ++m_caveGeneration;
    m_caveTransitioning = false;
    m_caveEffectActive = false;

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel && m_lowpassDSP) {
        engine->m_backgroundMusicChannel->removeDSP(m_lowpassDSP);
        float targetVol = engine->m_musicVolume;
        if (targetVol <= 0.0f && m_originalVolume > 0.0f) targetVol = m_originalVolume;
        engine->m_backgroundMusicChannel->setVolume(std::max(0.0f, std::min(1.0f, targetVol)));

        auto* bgCh = getMainBgChannel(engine);
        if (bgCh) {
            float targetFreq = (m_originalFrequency > 0.0f) ? m_originalFrequency : 22050.0f;
            bgCh->setFrequency(targetFreq);
        }
    }
    if (m_lowpassDSP) {
        m_lowpassDSP->release();
        m_lowpassDSP = nullptr;
    }
    m_originalFrequency = 0.0f;
    m_originalVolume = 0.0f;
    log::info("[ProfileMusic] Cave effect force-removed");
}

void ProfileMusicManager::removeCaveEffect() {
    if (!m_caveEffectActive) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) {
        // Canal ya no existe, solo limpiar estado
        if (m_lowpassDSP) {
            m_lowpassDSP->release();
            m_lowpassDSP = nullptr;
        }
        m_caveEffectActive = false;
        m_caveTransitioning = false;
        return;
    }

    m_caveTransitioning = true;
    auto generation = ++m_caveGeneration;

    // Leer valores actuales
    float currentVol = 0.0f;
    engine->m_backgroundMusicChannel->getVolume(&currentVol);

    float currentFreq = 0.0f;
    auto* bgCh = getMainBgChannel(engine);
    if (bgCh) {
        bgCh->getFrequency(&currentFreq);
    }

    // Volumen objetivo = volumen del juego
    float targetVol = engine->m_musicVolume;
    if (targetVol <= 0.0f && m_originalVolume > 0.0f) targetVol = m_originalVolume;
    float targetFreq = (m_originalFrequency > 0.0f) ? m_originalFrequency : (currentFreq > 0.0f ? currentFreq / 0.92f : 22050.0f);

    log::info("[ProfileMusic] Cave effect: starting smooth transition OUT (freq:{:.0f}->{:.0f}, vol:{:.2f}->{:.2f})",
        currentFreq, targetFreq, currentVol, targetVol);

    static constexpr int CAVE_STEPS = 15;
    executeCaveTransitionStep(0, CAVE_STEPS,
        800.0f, 22000.0f,
        currentFreq, targetFreq,
        currentVol, targetVol,
        false, generation);
}

void ProfileMusicManager::executeCaveTransitionStep(int step, int totalSteps,
    float cutoffFrom, float cutoffTo,
    float freqFrom, float freqTo,
    float volFrom, float volTo, bool applying, uint32_t generation) {

    if (generation != m_caveGeneration) {
        return;
    }

    auto engine = FMODAudioEngine::sharedEngine();
    if (!m_caveTransitioning || !engine || !engine->m_backgroundMusicChannel) {
        // Cancelado o canal desaparecio
        if (!applying && m_lowpassDSP && engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->removeDSP(m_lowpassDSP);
        }
        m_caveTransitioning = false;
        if (!applying) m_caveEffectActive = false;
        return;
    }

    auto* bgCh = getMainBgChannel(engine);

    if (step > totalSteps) {
        m_caveTransitioning = false;
        if (!applying) {
            // Transicion OUT completada: quitar DSP
            if (m_lowpassDSP) {
                engine->m_backgroundMusicChannel->removeDSP(m_lowpassDSP);
            }
            if (bgCh) {
                bgCh->setFrequency(freqTo);
            }
            engine->m_backgroundMusicChannel->setVolume(volTo);
            m_caveEffectActive = false;
            m_originalFrequency = 0.0f;
            m_originalVolume = 0.0f;
            log::info("[ProfileMusic] Cave effect fully removed");
        } else {
            // Transicion IN completada
            if (m_lowpassDSP) {
                m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, cutoffTo);
                m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
            }
            if (bgCh) {
                bgCh->setFrequency(freqTo);
            }
            engine->m_backgroundMusicChannel->setVolume(volTo);
            log::info("[ProfileMusic] Cave effect fully applied");
        }
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    // Ease in-out cuadratica
    float eT = (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);

    float cutoff = cutoffFrom + (cutoffTo - cutoffFrom) * eT;
    float freq = freqFrom + (freqTo - freqFrom) * eT;
    float vol = volFrom + (volTo - volFrom) * eT;

    if (m_lowpassDSP) {
        m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, cutoff);
        float resonance = applying ? (1.0f + eT * 1.0f) : (2.0f - eT * 1.0f);
        m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, resonance);
    }
    if (bgCh) {
        bgCh->setFrequency(freq);
    }
    engine->m_backgroundMusicChannel->setVolume(std::max(0.0f, std::min(1.0f, vol)));

    int next = step + 1;
    float stepDelay = 400.0f / static_cast<float>(totalSteps) / 1000.f; // 400ms total transition

    auto lt = m_lifetimeToken;
    paimon::scheduleMainThreadDelay(stepDelay, [lt, this, next, totalSteps, cutoffFrom, cutoffTo,
                                                freqFrom, freqTo, volFrom, volTo, applying, generation]() {
        if (!lt || !lt->load(std::memory_order_acquire)) return;
        if (generation != m_caveGeneration) return;
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_backgroundMusicChannel) return;
        executeCaveTransitionStep(next, totalSteps, cutoffFrom, cutoffTo,
            freqFrom, freqTo, volFrom, volTo, applying, generation);
    });
}

void ProfileMusicManager::forceStop() {
    log::info("[ProfileMusic] forceStop called (fadingOut:{}, fadingIn:{}, playing:{})",
        m_isFadingOut, m_isFadingIn, m_isPlaying);
    stopOwnedAudioPlayback();
    log::info("[ProfileMusic] forceStop complete, all state cleared");
}

float ProfileMusicManager::getCurrentAmplitude() const {
    if (!m_isPlaying) return 0.f;

    auto engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return 0.f;

    // usar FMOD DSP metering pa leer la amplitud real
    FMOD::DSP* headDSP = nullptr;
    auto result = bgCh->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &headDSP);
    if (result != FMOD_OK || !headDSP) return 0.f;

    headDSP->setMeteringEnabled(false, true);

    FMOD_DSP_METERING_INFO meteringInfo = {};
    result = headDSP->getMeteringInfo(nullptr, &meteringInfo);
    if (result != FMOD_OK) return 0.f;

    float peak = 0.f;
    for (int i = 0; i < meteringInfo.numchannels; i++) {
        if (meteringInfo.peaklevel[i] > peak) peak = meteringInfo.peaklevel[i];
    }
    return peak;
}



