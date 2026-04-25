#include "ProfileImageService.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../video/VideoNormalizer.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include <prevter.imageplus/include/events.hpp>
#include "../../../utils/ImageLoadHelper.hpp"
#include "ProfileThumbs.hpp"
#include "../../thumbnails/services/ThumbnailTransportClient.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>

using namespace geode::prelude;

namespace {
constexpr auto PROFILE_IMG_CACHE_MAX_AGE = std::chrono::hours(24 * 14);

std::mutex& getProfileImgCachePruneMutex() {
    static std::mutex mutex;
    return mutex;
}

std::filesystem::path getProfileImgCacheDir() {
    return Mod::get()->getSaveDir() / "profileimg_cache";
}

size_t getProfileImgCacheMaxBytes() {
    return std::clamp<size_t>(
        paimon::settings::quality::diskCacheBytes() / 2,
        128ull * 1024ull * 1024ull,
        512ull * 1024ull * 1024ull
    );
}

void pruneProfileImgCache() {
    std::lock_guard<std::mutex> lock(getProfileImgCachePruneMutex());

    auto cacheDir = getProfileImgCacheDir();
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        return;
    }

    struct CacheEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime;
        uintmax_t size = 0;
    };

    std::vector<CacheEntry> entries;
    uintmax_t totalBytes = 0;
    auto now = std::filesystem::file_time_type::clock::now();

    for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        std::error_code sizeEc;
        auto fileSize = entry.file_size(sizeEc);
        if (sizeEc) {
            continue;
        }

        std::error_code timeEc;
        auto mtime = entry.last_write_time(timeEc);
        if (timeEc) {
            continue;
        }

        if (now - mtime > PROFILE_IMG_CACHE_MAX_AGE) {
            std::error_code rmEc;
            std::filesystem::remove(entry.path(), rmEc);
            continue;
        }

        totalBytes += fileSize;
        entries.push_back({entry.path(), mtime, fileSize});
    }

    auto maxBytes = getProfileImgCacheMaxBytes();
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

        std::error_code rmEc;
        std::filesystem::remove(entry.path, rmEc);
        if (!rmEc) {
            totalBytes = (entry.size > totalBytes) ? 0 : (totalBytes - entry.size);
        }
    }
}

int getProfileVariantSlot(int accountID) {
    return accountID * 4;
}

std::string makeProfileGifKey(char const* prefix, int accountID) {
    return fmt::format("{}_{}", prefix, accountID);
}

std::filesystem::path getProfileImgCachePath(int accountID) {
    return getProfileImgCacheDir() /
           fmt::format("{}.dat", accountID);
}

void pruneProfileImgCacheVariants(int accountID) {
    auto cacheDir = getProfileImgCacheDir();
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        return;
    }

    auto activeName = getProfileImgCachePath(accountID).filename();
    for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        auto stem = geode::utils::string::pathToString(entry.path().stem());
        if (stem != std::to_string(accountID) && stem.rfind(std::to_string(accountID) + "_", 0) != 0) {
            continue;
        }

        if (entry.path().filename() == activeName) {
            continue;
        }

        std::error_code rmEc;
        std::filesystem::remove(entry.path(), rmEc);
    }
}
}

ProfileImageService::ProfileImageService() {
    pruneProfileImgCache();
}

std::string ProfileImageService::getProfileImgGifKey(int accountID) const {
    std::lock_guard<std::mutex> lock(m_profileImgGifMutex);
    auto it = m_profileImgGifKeys.find(getProfileVariantSlot(accountID));
    if (it == m_profileImgGifKeys.end()) return "";
    return it->second;
}

void ProfileImageService::rememberProfileImgGifKey(int accountID, std::string const& gifKey) {
    if (gifKey.empty()) return;
    std::lock_guard<std::mutex> lock(m_profileImgGifMutex);
    m_profileImgGifKeys[getProfileVariantSlot(accountID)] = gifKey;
}

void ProfileImageService::clearProfileImgGifKey(int accountID) {
    std::lock_guard<std::mutex> lock(m_profileImgGifMutex);
    m_profileImgGifKeys.erase(getProfileVariantSlot(accountID));
}

// Subidas de banner

void ProfileImageService::uploadProfile(int accountID, std::vector<uint8_t> const& pngData,
                                        std::string const& username, UploadCallback callback) {
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfile(accountID, pngData, username,
        [this, callback, accountID](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ProfileThumbs::get().deleteProfile(accountID);
            }
            callback(success, message);
        });
}

void ProfileImageService::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData,
                                           std::string const& username, UploadCallback callback) {
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfileGIF(accountID, gifData, username,
        [this, callback, accountID](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ProfileThumbs::get().deleteProfile(accountID);
            }
            callback(success, message);
        });
}

void ProfileImageService::uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data,
                                             std::string const& username, UploadCallback callback) {
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    // Normaliza a H.264+AAC antes de subir
    auto normalRes = paimon::video::VideoNormalizer::normalizeData(
        mp4Data, fmt::format("upload_profile_{}", accountID));
    auto const& uploadData = normalRes.isOk() ? normalRes.unwrap() : mp4Data;
    if (normalRes.isErr())
        log::warn("[ProfileImageService] Normalization failed: {} — uploading as-is",
                  normalRes.unwrapErr());

    HttpClient::get().uploadProfileVideo(accountID, uploadData, username,
        [this, callback, accountID](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ProfileThumbs::get().deleteProfile(accountID);
            }
            callback(success, message);
        });
}

// Descarga de banner

void ProfileImageService::downloadProfile(int accountID, std::string const& username,
                                          DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadProfile(accountID, username,
        [profileAccountID = accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }

            // Detecta MP4
            bool isMP4 = data.size() > 8 &&
                data[4]=='f' && data[5]=='t' && data[6]=='y' && data[7]=='p';

            if (isMP4) {
                std::string cacheKey = fmt::format("profile_video_{}", profileAccountID);
                auto* videoSprite = VideoThumbnailSprite::createFromData(data, cacheKey);
                if (videoSprite) {
                    // Cachea el video en ProfileThumbs
                    std::string videoKey = fmt::format("profile_video_{}", profileAccountID);
                    ProfileThumbs::get().cacheProfileGIF(profileAccountID, videoKey,
                        {255,255,255}, {255,255,255}, 0.6f);
                    callback(true, nullptr);
                } else {
                    callback(false, nullptr);
                }
                return;
            }

            // Detecta GIF o APNG
            bool isGIF = imgp::formats::isGif(data.data(), data.size())
                      || imgp::formats::isAPng(data.data(), data.size());

            if (isGIF) {
                std::string gifKey = makeProfileGifKey("profile_gif", profileAccountID);
                AnimatedGIFSprite::createAsync(data, gifKey,
                    [profileAccountID, gifKey, callback](AnimatedGIFSprite* sprite) {
                        if (sprite) {
                            ProfileThumbs::get().cacheProfileGIF(profileAccountID, gifKey,
                                {255,255,255}, {255,255,255}, 0.6f);
                            callback(true, sprite->getTexture());
                        } else {
                            callback(false, nullptr);
                        }
                    });
                return;
            }

            auto* texture = ThumbnailTransportClient::bytesToTexture(data);
            callback(texture != nullptr, texture);
        });
}

// Verificacion batch

void ProfileImageService::batchCheckProfiles(std::vector<int> const& accountIDs, BatchCheckCallback callback) {
    if (!m_serverEnabled || accountIDs.empty()) {
        callback(false, {}, {});
        return;
    }

    HttpClient::get().batchCheckProfiles(accountIDs,
        [callback](bool success, std::string const& response) {
            if (!success || response.empty()) {
                callback(false, {}, {});
                return;
            }

            auto res = matjson::parse(response);
            if (!res.isOk()) {
                callback(false, {}, {});
                return;
            }
            auto json = res.unwrap();

            // Parsea perfiles encontrados
            std::unordered_set<int> found;
            if (json.contains("found") && json["found"].isArray()) {
                for (auto const& v : json["found"].asArray().unwrap()) {
                    auto id = v.asInt();
                    if (id.isOk()) found.insert(id.unwrap());
                }
            }

            // Parsea configuraciones
            std::unordered_map<int, ProfileConfig> configs;
            if (json.contains("configs") && json["configs"].isObject()) {
                for (auto const& [key, val] : json["configs"]) {
                    int accountID = 0;
                    auto accountIDRes = geode::utils::numFromString<int>(key);
                    if (!accountIDRes) continue;
                    accountID = accountIDRes.unwrap();

                    ProfileConfig config;
                    config.hasConfig = true;
                    if (val.contains("backgroundType")) config.backgroundType = val["backgroundType"].asString().unwrapOr("gradient");
                    if (val.contains("blurIntensity"))  config.blurIntensity  = (float)val["blurIntensity"].asDouble().unwrapOr(3.0);
                    if (val.contains("darkness"))       config.darkness       = (float)val["darkness"].asDouble().unwrapOr(0.2);
                    if (val.contains("useGradient"))    config.useGradient    = val["useGradient"].asBool().unwrapOr(false);
                    if (val.contains("colorA")) {
                        auto c = val["colorA"];
                        config.colorA.r = (GLubyte)c["r"].asInt().unwrapOr(255);
                        config.colorA.g = (GLubyte)c["g"].asInt().unwrapOr(255);
                        config.colorA.b = (GLubyte)c["b"].asInt().unwrapOr(255);
                    }
                    if (val.contains("colorB")) {
                        auto c = val["colorB"];
                        config.colorB.r = (GLubyte)c["r"].asInt().unwrapOr(255);
                        config.colorB.g = (GLubyte)c["g"].asInt().unwrapOr(255);
                        config.colorB.b = (GLubyte)c["b"].asInt().unwrapOr(255);
                    }
                    if (val.contains("separatorColor")) {
                        auto c = val["separatorColor"];
                        config.separatorColor.r = (GLubyte)c["r"].asInt().unwrapOr(0);
                        config.separatorColor.g = (GLubyte)c["g"].asInt().unwrapOr(0);
                        config.separatorColor.b = (GLubyte)c["b"].asInt().unwrapOr(0);
                    }
                    if (val.contains("separatorOpacity")) config.separatorOpacity = val["separatorOpacity"].asInt().unwrapOr(50);
                    if (val.contains("widthFactor"))      config.widthFactor      = (float)val["widthFactor"].asDouble().unwrapOr(0.60);

                    // Config del fondo de comentarios
                    if (val.contains("commentBgType"))        config.commentBgType        = val["commentBgType"].asString().unwrapOr("none");
                    if (val.contains("commentBgThumbnailId")) config.commentBgThumbnailId = val["commentBgThumbnailId"].asString().unwrapOr("");
                    if (val.contains("commentBgThumbnailPos")) config.commentBgThumbnailPos = val["commentBgThumbnailPos"].asInt().unwrapOr(1);
                    if (val.contains("commentBgBannerMode"))  config.commentBgBannerMode  = val["commentBgBannerMode"].asString().unwrapOr("background");
                    if (val.contains("commentBgBlurType"))  config.commentBgBlurType  = val["commentBgBlurType"].asString().unwrapOr("gaussian");
                    if (val.contains("commentBgBlur"))       config.commentBgBlur       = (float)val["commentBgBlur"].asDouble().unwrapOr(5.0);
                    if (val.contains("commentBgDarkness"))    config.commentBgDarkness    = (float)val["commentBgDarkness"].asDouble().unwrapOr(0.35);
                    if (val.contains("commentBgSolidOpacity")) config.commentBgSolidOpacity = val["commentBgSolidOpacity"].asInt().unwrapOr(128);
                    if (val.contains("commentBgSolidColor")) {
                        auto c = val["commentBgSolidColor"];
                        config.commentBgSolidColor.r = (GLubyte)c["r"].asInt().unwrapOr(30);
                        config.commentBgSolidColor.g = (GLubyte)c["g"].asInt().unwrapOr(30);
                        config.commentBgSolidColor.b = (GLubyte)c["b"].asInt().unwrapOr(30);
                    }

                    configs[accountID] = config;
                }
            }

            log::info("[ProfileImageService] Batch check: {} found, {} configs",
                found.size(), configs.size());
            callback(true, found, configs);
        });
}

// Subidas de imagen de perfil

void ProfileImageService::uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData,
                                           std::string const& username,
                                           std::string const& contentType,
                                           UploadCallback callback) {
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir imagen de perfil.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfileImg(accountID, imgData, username, contentType,
        [this, callback](bool success, std::string const& message) {
            if (success) m_uploadCount++;
            callback(success, message);
        });
}

void ProfileImageService::uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData,
                                              std::string const& username, UploadCallback callback) {
    uploadProfileImg(accountID, gifData, username, "image/gif", callback);
}

// Descarga de imagen de perfil

void ProfileImageService::downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadProfileImg(accountID,
        [this, profileAccountID = accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) {
                clearProfileImgGifKey(profileAccountID);
                callback(false, nullptr);
                return;
            }

            // Cache en disco
            {
                auto cacheDir = getProfileImgCacheDir();
                std::error_code ec;
                std::filesystem::create_directories(cacheDir, ec);
                pruneProfileImgCacheVariants(profileAccountID);
                auto cachePath = getProfileImgCachePath(profileAccountID);
                std::ofstream cacheFile(cachePath, std::ios::binary);
                if (cacheFile) {
                    cacheFile.write(reinterpret_cast<char const*>(data.data()), data.size());
                }
                pruneProfileImgCache();
            }

            bool isMP4img = data.size() > 8 &&
                data[4]=='f' && data[5]=='t' && data[6]=='y' && data[7]=='p';
            if (isMP4img) {
                std::string videoKey = fmt::format("profileimg_video_{}", profileAccountID);
                auto* videoSprite = VideoThumbnailSprite::createFromData(data, videoKey);
                if (videoSprite) {
                    rememberProfileImgGifKey(profileAccountID, videoKey);
                    callback(true, nullptr);
                } else {
                    clearProfileImgGifKey(profileAccountID);
                    callback(false, nullptr);
                }
                return;
            }

            bool isGIF = imgp::formats::isGif(data.data(), data.size())
                      || imgp::formats::isAPng(data.data(), data.size());
            if (isGIF) {
                std::string gifKey = makeProfileGifKey("profileimg_gif", profileAccountID);
                AnimatedGIFSprite::createAsync(data, gifKey, [this, profileAccountID, gifKey, callback](AnimatedGIFSprite* sprite) {
                    if (!sprite || !sprite->getTexture()) {
                        queueInMainThread([this, profileAccountID, callback]() {
                            clearProfileImgGifKey(profileAccountID);
                            callback(false, nullptr);
                        });
                        return;
                    }
                    auto* tex = sprite->getTexture();
                    queueInMainThread([this, profileAccountID, gifKey, callback, tex]() {
                        rememberProfileImgGifKey(profileAccountID, gifKey);
                        callback(true, tex);
                    });
                });
                return;
            }

            clearProfileImgGifKey(profileAccountID);
            auto dataCopy = std::make_shared<std::vector<uint8_t>>(data);
            queueInMainThread([callback, dataCopy]() {
                auto loaded = ImageLoadHelper::loadWithSTBFromMemory(dataCopy->data(), dataCopy->size());
                if (!loaded.success || !loaded.texture) {
                    callback(false, nullptr);
                    return;
                }
                loaded.texture->autorelease();
                callback(true, loaded.texture);
            });
        }, isSelf);
}

// Perfil pendiente (moderadores)

void ProfileImageService::downloadPendingProfile(int accountID, DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    std::string url = HttpClient::get().getServerURL()
                    + "/pending_profilebackground/" + std::to_string(accountID) + "?self=1";

    HttpClient::get().downloadFromUrl(url,
        [callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }
            auto* texture = ThumbnailTransportClient::bytesToTexture(data);
            callback(texture != nullptr, texture);
        });
}

// ── profile config ──────────────────────────────────────────────────

void ProfileImageService::uploadProfileConfig(int accountID, ProfileConfig const& config,
                                              ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "Server disabled"); return; }

    matjson::Value json;
    json["backgroundType"] = config.backgroundType;
    json["blurIntensity"]  = config.blurIntensity;
    json["darkness"]       = config.darkness;
    json["useGradient"]    = config.useGradient;

    matjson::Value colorA;
    colorA["r"] = (int)config.colorA.r; colorA["g"] = (int)config.colorA.g; colorA["b"] = (int)config.colorA.b;
    json["colorA"] = colorA;

    matjson::Value colorB;
    colorB["r"] = (int)config.colorB.r; colorB["g"] = (int)config.colorB.g; colorB["b"] = (int)config.colorB.b;
    json["colorB"] = colorB;

    matjson::Value sepColor;
    sepColor["r"] = (int)config.separatorColor.r;
    sepColor["g"] = (int)config.separatorColor.g;
    sepColor["b"] = (int)config.separatorColor.b;
    json["separatorColor"] = sepColor;

    json["separatorOpacity"] = config.separatorOpacity;
    json["widthFactor"]      = config.widthFactor;

    // Comment cell background settings
    json["commentBgType"]        = config.commentBgType;
    json["commentBgThumbnailId"] = config.commentBgThumbnailId;
    json["commentBgThumbnailPos"] = config.commentBgThumbnailPos;
    json["commentBgBannerMode"]  = config.commentBgBannerMode;
    json["commentBgBlurType"]   = config.commentBgBlurType;
    json["commentBgBlur"]       = config.commentBgBlur;
    json["commentBgDarkness"]    = config.commentBgDarkness;
    json["commentBgSolidOpacity"] = config.commentBgSolidOpacity;

    matjson::Value commentBgSolidColor;
    commentBgSolidColor["r"] = (int)config.commentBgSolidColor.r;
    commentBgSolidColor["g"] = (int)config.commentBgSolidColor.g;
    commentBgSolidColor["b"] = (int)config.commentBgSolidColor.b;
    json["commentBgSolidColor"] = commentBgSolidColor;

    std::string jsonStr = json.dump(matjson::NO_INDENTATION);

    HttpClient::get().uploadProfileConfig(accountID, jsonStr,
        [callback, accountID, config](bool success, std::string const& msg) {
            if (success) {
                ProfileThumbs::get().deleteProfile(accountID);
                // Recachea la config para evitar esperar al servidor
                ProfileThumbs::get().cacheProfileConfig(accountID, config);
            }
            callback(success, msg);
        });
}

void ProfileImageService::downloadProfileConfig(int accountID,
    geode::CopyableFunction<void(bool, ProfileConfig const&)> callback) {
    if (!m_serverEnabled) { callback(false, ProfileConfig()); return; }

    HttpClient::get().downloadProfileConfig(accountID,
        [callback](bool success, std::string const& response) {
            if (!success || response.empty()) { callback(false, ProfileConfig()); return; }

            auto res = matjson::parse(response);
            if (!res.isOk()) { callback(false, ProfileConfig()); return; }
            auto json = res.unwrap();

            ProfileConfig config;
            config.hasConfig = true;

            if (json.contains("backgroundType")) config.backgroundType = json["backgroundType"].asString().unwrapOr("gradient");
            if (json.contains("blurIntensity"))  config.blurIntensity  = (float)json["blurIntensity"].asDouble().unwrapOr(3.0);
            if (json.contains("darkness"))       config.darkness       = (float)json["darkness"].asDouble().unwrapOr(0.2);
            if (json.contains("useGradient"))    config.useGradient    = json["useGradient"].asBool().unwrapOr(false);

            if (json.contains("colorA")) {
                auto c = json["colorA"];
                config.colorA.r = (GLubyte)c["r"].asInt().unwrapOr(255);
                config.colorA.g = (GLubyte)c["g"].asInt().unwrapOr(255);
                config.colorA.b = (GLubyte)c["b"].asInt().unwrapOr(255);
            }
            if (json.contains("colorB")) {
                auto c = json["colorB"];
                config.colorB.r = (GLubyte)c["r"].asInt().unwrapOr(255);
                config.colorB.g = (GLubyte)c["g"].asInt().unwrapOr(255);
                config.colorB.b = (GLubyte)c["b"].asInt().unwrapOr(255);
            }
            if (json.contains("separatorColor")) {
                auto c = json["separatorColor"];
                config.separatorColor.r = (GLubyte)c["r"].asInt().unwrapOr(0);
                config.separatorColor.g = (GLubyte)c["g"].asInt().unwrapOr(0);
                config.separatorColor.b = (GLubyte)c["b"].asInt().unwrapOr(0);
            }
            if (json.contains("separatorOpacity")) config.separatorOpacity = json["separatorOpacity"].asInt().unwrapOr(50);
            if (json.contains("widthFactor"))      config.widthFactor      = (float)json["widthFactor"].asDouble().unwrapOr(0.60);

            // Comment cell background settings
            if (json.contains("commentBgType"))        config.commentBgType        = json["commentBgType"].asString().unwrapOr("none");
            if (json.contains("commentBgThumbnailId")) config.commentBgThumbnailId = json["commentBgThumbnailId"].asString().unwrapOr("");
            if (json.contains("commentBgThumbnailPos")) config.commentBgThumbnailPos = json["commentBgThumbnailPos"].asInt().unwrapOr(1);
            if (json.contains("commentBgBannerMode"))  config.commentBgBannerMode  = json["commentBgBannerMode"].asString().unwrapOr("background");
            if (json.contains("commentBgBlurType"))  config.commentBgBlurType  = json["commentBgBlurType"].asString().unwrapOr("gaussian");
            if (json.contains("commentBgBlur"))       config.commentBgBlur       = (float)json["commentBgBlur"].asDouble().unwrapOr(5.0);
            if (json.contains("commentBgDarkness"))    config.commentBgDarkness    = (float)json["commentBgDarkness"].asDouble().unwrapOr(0.35);
            if (json.contains("commentBgSolidOpacity")) config.commentBgSolidOpacity = json["commentBgSolidOpacity"].asInt().unwrapOr(128);
            if (json.contains("commentBgSolidColor")) {
                auto c = json["commentBgSolidColor"];
                config.commentBgSolidColor.r = (GLubyte)c["r"].asInt().unwrapOr(30);
                config.commentBgSolidColor.g = (GLubyte)c["g"].asInt().unwrapOr(30);
                config.commentBgSolidColor.b = (GLubyte)c["b"].asInt().unwrapOr(30);
            }

            callback(true, config);
        });
}
