#include "ProfileImageService.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/HttpClient.hpp"
#include "ProfileImageCache.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../utils/FormatDetect.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/ThreadPool.hpp"
#include "ProfileThumbs.hpp"
#include "ProfileConfigSerialization.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <algorithm>
#include <chrono>
#include <memory>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {
constexpr auto PROFILE_IMG_CACHE_MAX_AGE = std::chrono::hours(24 * 14);

size_t getProfileImgCacheMaxBytes() {
    return std::clamp<size_t>(
        paimon::settings::quality::diskCacheBytes() / 2,
        128ull * 1024ull * 1024ull,
        512ull * 1024ull * 1024ull
    );
}

void pruneProfileImgCache(std::filesystem::path const& cacheDir, size_t maxBytes) {
    std::lock_guard lock(profileImgDiskMutex());
    if (paimon::isRuntimeShuttingDown()) return;
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

    for (std::filesystem::directory_iterator it(cacheDir, ec), end; !ec && it != end; it.increment(ec)) {
        auto const& entry = *it;
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc) || typeEc || entry.path().extension() != ".dat") continue;

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
            if (std::filesystem::remove(entry.path(), rmEc) && !rmEc) continue;
        }

        totalBytes += fileSize;
        entries.push_back({entry.path(), mtime, fileSize});
    }

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

std::string makeProfileGifKey(char const* prefix, int accountID) {
    return fmt::format("{}_{}", prefix, accountID);
}

paimon::ThreadPool* s_profileImagePool = nullptr;
std::mutex s_profileImagePoolMutex;
std::vector<std::weak_ptr<ProfileImageService::DownloadCallback>> s_profileImageCompletions;

paimon::ThreadPool& profileImagePool() {
    std::lock_guard lock(s_profileImagePoolMutex);
    if (paimon::isRuntimeShuttingDown()) throw std::runtime_error("profile image service is shutting down");
    if (!s_profileImagePool) s_profileImagePool = new paimon::ThreadPool(2, "PaimonProfileImg");
    return *s_profileImagePool;
}

int profileImageMaxDim() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return 768;
#else
    return 1024;
#endif
}

void decodeStaticProfileImageAsync(std::shared_ptr<std::vector<uint8_t>> data,
                                   ProfileImageService::DownloadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    auto completion = std::make_shared<ProfileImageService::DownloadCallback>(std::move(callback));
    std::erase_if(s_profileImageCompletions, [](auto const& item) { return item.expired(); });
    s_profileImageCompletions.emplace_back(completion);
    auto fail = [completion]() {
        queueInMainThread([completion]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto callback = std::exchange(*completion, nullptr);
            if (callback) callback(false, nullptr);
        });
    };
    if (!data || data->empty() || data->size() > 64ull * 1024 * 1024 ||
        data->size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        fail();
        return;
    }
    try {
        profileImagePool().enqueue([data = std::move(data), completion, fail]() {
            if (paimon::isRuntimeShuttingDown()) return;
            try {
                int w = 0, h = 0, channels = 0;
                auto length = static_cast<int>(data->size());
                if (!stbi_info_from_memory(data->data(), length, &w, &h, &channels) ||
                    w <= 0 || h <= 0 || w > ImageLoadHelper::kMaxImageDim || h > ImageLoadHelper::kMaxImageDim) {
                    fail();
                    return;
                }
                std::shared_ptr<unsigned char> pixels(
                    stbi_load_from_memory(data->data(), length, &w, &h, &channels, 4), stbi_image_free);
                if (!pixels || w <= 0 || h <= 0 || w > ImageLoadHelper::kMaxImageDim || h > ImageLoadHelper::kMaxImageDim) {
                    fail();
                    return;
                }
                std::shared_ptr<std::vector<uint8_t>> resized;
                auto maxDim = profileImageMaxDim();
                if (w > maxDim || h > maxDim) {
                    auto smaller = ImageLoadHelper::downsampleForCache(pixels.get(), w, h, maxDim);
                    if (smaller.pixels.empty() || smaller.width <= 0 || smaller.height <= 0) {
                        fail();
                        return;
                    }
                    w = smaller.width;
                    h = smaller.height;
                    resized = std::make_shared<std::vector<uint8_t>>(std::move(smaller.pixels));
                    pixels.reset();
                }
                if (paimon::isRuntimeShuttingDown()) return;
                queueInMainThread([pixels = std::move(pixels), resized = std::move(resized), w, h, completion]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto callback = std::exchange(*completion, nullptr);
                    if (!callback) return;
                    ImageLoadHelper::LoadedImage loaded;
                    try {
                        loaded = ImageLoadHelper::createFromRGBA(resized ? resized->data() : pixels.get(), w, h, false);
                    } catch (...) {
                        callback(false, nullptr);
                        return;
                    }
                    if (loaded.texture) loaded.texture->autorelease();
                    callback(loaded.success && loaded.texture, loaded.texture);
                });
            } catch (...) {
                fail();
            }
        });
    } catch (...) {
        fail();
    }
}

void pruneProfileImgCacheVariants(int accountID) {
    std::lock_guard lock(profileImgDiskMutex());
    auto cacheDir = getProfileImgCacheDir();
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        return;
    }

    auto activeName = getProfileImgCachePath(accountID).filename();
    for (std::filesystem::directory_iterator it(cacheDir, ec), end; !ec && it != end; it.increment(ec)) {
        auto const& entry = *it;
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc) || typeEc || entry.path().extension() != ".dat") continue;

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
    auto cacheDir = getProfileImgCacheDir();
    auto maxBytes = getProfileImgCacheMaxBytes();
    if (!paimon::isRuntimeShuttingDown()) {
        profileImagePool().enqueue([cacheDir, maxBytes]() { pruneProfileImgCache(cacheDir, maxBytes); });
    }
}

void ProfileImageService::shutdown() {
    // queued main-thread deliveries can outlive the weakref pool during exit.
    for (auto const& weak : s_profileImageCompletions) {
        if (auto completion = weak.lock()) *completion = nullptr;
    }
    s_profileImageCompletions.clear();
    paimon::ThreadPool* pool = nullptr;
    {
        std::lock_guard lock(s_profileImagePoolMutex);
        pool = s_profileImagePool;
    }
    if (pool) pool->shutdown();
    std::lock_guard lock(m_profileImgGifMutex);
    m_profileImgGifKeys.clear();
}

ProfileImageService::UploadCallback ProfileImageService::uploadCompletion(
    int accountID, UploadCallback callback, bool background) {
    return [this, accountID, callback = std::move(callback), background](bool success, std::string const& message) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (success) {
            if (background) ProfileThumbs::get().deleteProfile(accountID);
            invalidateProfileImgCache(accountID);
            invalidateProfileImgDiskCache(accountID);
            clearProfileImgGifKey(accountID);
        }
        if (callback) callback(success, message);
    };
}

std::string ProfileImageService::getProfileImgGifKey(int accountID) const {
    std::lock_guard<std::mutex> lock(m_profileImgGifMutex);
    auto it = m_profileImgGifKeys.find(accountID);
    if (it == m_profileImgGifKeys.end()) return "";
    return it->second;
}

void ProfileImageService::rememberProfileImgGifKey(int accountID, std::string const& gifKey) {
    if (gifKey.empty()) return;
    std::lock_guard<std::mutex> lock(m_profileImgGifMutex);
    m_profileImgGifKeys[accountID] = gifKey;
}

void ProfileImageService::clearProfileImgGifKey(int accountID) {
    std::lock_guard<std::mutex> lock(m_profileImgGifMutex);
    m_profileImgGifKeys.erase(accountID);
}


void ProfileImageService::uploadProfile(int accountID, std::vector<uint8_t> const& pngData,
                                        std::string const& username, UploadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfile(accountID, pngData, username,
        uploadCompletion(accountID, std::move(callback), true));
}

void ProfileImageService::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData,
                                           std::string const& username, UploadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfileGIF(accountID, gifData, username,
        uploadCompletion(accountID, std::move(callback), true));
}

void ProfileImageService::uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data,
                                             std::string const& username, UploadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfileVideo(accountID, mp4Data, username,
        uploadCompletion(accountID, std::move(callback), true));
}


void ProfileImageService::downloadProfile(int accountID, std::string const& username,
                                          DownloadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadProfile(accountID, username,
        [profileAccountID = accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!success || data.empty()) { callback(false, nullptr); return; }
            ProfileImageService::processProfileBackgroundBytes(profileAccountID, data, callback);
        });
}

void ProfileImageService::processProfileBackgroundBytes(int profileAccountID,
                                                        std::vector<uint8_t> const& data,
                                                        DownloadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    if (profileAccountID <= 0 || data.empty() || data.size() > 64ull * 1024 * 1024) { callback(false, nullptr); return; }

    bool isMP4 = paimon::format::isMp4(data.data(), data.size());

    if (isMP4) {
        std::string cacheKey = fmt::format("profile_video_{}", profileAccountID);
        auto* videoSprite = VideoThumbnailSprite::createFromData(data, cacheKey);
        if (videoSprite) {
            std::string videoKey = fmt::format("profile_video_{}", profileAccountID);
            ProfileThumbs::get().cacheProfileGIF(profileAccountID, videoKey,
                {255,255,255}, {255,255,255}, 0.6f);
            callback(true, nullptr);
        } else {
            callback(false, nullptr);
        }
        return;
    }

    bool isGIF = paimon::format::isGif(data.data(), data.size());

    if (isGIF) {
        std::string gifKey = makeProfileGifKey("profile_gif", profileAccountID);
        AnimatedGIFSprite::createAsync(data, gifKey,
            [profileAccountID, gifKey, callback](AnimatedGIFSprite* sprite) {
                if (paimon::isRuntimeShuttingDown()) return;
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

    decodeStaticProfileImageAsync(std::make_shared<std::vector<uint8_t>>(data), callback);
}


void ProfileImageService::batchCheckProfiles(std::vector<int> const& accountIDs, BatchCheckCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    if (!m_serverEnabled || accountIDs.empty()) {
        callback(false, {}, {});
        return;
    }

    HttpClient::get().batchCheckProfiles(accountIDs,
        [callback, requested = std::unordered_set<int>(accountIDs.begin(), accountIDs.end())](bool success, std::string const& response) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!success || response.empty()) {
                callback(false, {}, {});
                return;
            }

            auto res = matjson::parse(response);
            if (!res.isOk() || !res.unwrap().isObject()) {
                callback(false, {}, {});
                return;
            }
            auto const& json = res.unwrap();

            std::unordered_set<int> found;
            paimon::json::forEachInArray(json["found"], [&](matjson::Value const& v) {
                auto id = paimon::json::integerOr<int>(v);
                if (id > 0 && requested.contains(id)) found.insert(id);
            });

            std::unordered_map<int, ProfileConfig> configs;
            if (json.contains("configs") && json["configs"].isObject()) {
                for (auto const& [key, val] : json["configs"]) {
                    int accountID = 0;
                    auto accountIDRes = geode::utils::numFromString<int>(key);
                    if (!accountIDRes) continue;
                    accountID = accountIDRes.unwrap();

                    if (accountID <= 0 || !requested.contains(accountID) || !val.isObject()) continue;
                    auto config = paimon::profiles::parseConfig(val);
                    configs[accountID] = config;
                }
            }

            log::info("[ProfileImageService] Batch check: {} found, {} configs",
                found.size(), configs.size());
            callback(true, found, configs);
        });
}


void ProfileImageService::uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData,
                                           std::string const& username,
                                           std::string const& contentType,
                                           UploadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir imagen de perfil.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfileImg(accountID, imgData, username, contentType,
        uploadCompletion(accountID, std::move(callback), false));
}

void ProfileImageService::uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData,
                                              std::string const& username, UploadCallback callback) {
    uploadProfileImg(accountID, gifData, username, "image/gif", callback);
}


void ProfileImageService::downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadProfileImg(accountID,
        [this, profileAccountID = accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!success || data.empty() || data.size() > 64ull * 1024 * 1024) {
                clearProfileImgGifKey(profileAccountID);
                callback(false, nullptr);
                return;
            }

            pruneProfileImgCacheVariants(profileAccountID);
            saveProfileImgToDisk(profileAccountID, data);
            auto cacheDir = getProfileImgCacheDir();
            auto maxBytes = getProfileImgCacheMaxBytes();
            profileImagePool().enqueue([cacheDir, maxBytes]() { pruneProfileImgCache(cacheDir, maxBytes); });

            bool isMP4img = paimon::format::isMp4(data.data(), data.size());
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

            bool isGIF = paimon::format::isGif(data.data(), data.size());
            if (isGIF) {
                std::string gifKey = makeProfileGifKey("profileimg_gif", profileAccountID);
                AnimatedGIFSprite::createAsync(data, gifKey, [this, profileAccountID, gifKey, callback](AnimatedGIFSprite* sprite) {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (!sprite || !sprite->getTexture()) {
                        clearProfileImgGifKey(profileAccountID);
                        callback(false, nullptr);
                        return;
                    }
                    rememberProfileImgGifKey(profileAccountID, gifKey);
                    callback(true, sprite->getTexture());
                });
                return;
            }

            clearProfileImgGifKey(profileAccountID);
            auto dataCopy = std::make_shared<std::vector<uint8_t>>(data);
            decodeStaticProfileImageAsync(std::move(dataCopy), callback);
        }, isSelf);
}


void ProfileImageService::downloadPendingProfile(int accountID, DownloadCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    std::string url = HttpClient::get().getServerURL()
                    + "/pending_profilebackground/" + std::to_string(accountID) + "?self=1";

    HttpClient::get().downloadFromUrl(url,
        [callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!success || data.empty() || data.size() > 64ull * 1024 * 1024) { callback(false, nullptr); return; }
            decodeStaticProfileImageAsync(std::make_shared<std::vector<uint8_t>>(data), callback);
        });
}


void ProfileImageService::uploadProfileConfig(int accountID, ProfileConfig const& config,
                                              ActionCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    if (!m_serverEnabled) { callback(false, "Server disabled"); return; }

    auto json = paimon::profiles::serializeConfig(config);
    std::string jsonStr = json.dump(matjson::NO_INDENTATION);

    HttpClient::get().uploadProfileConfig(accountID, jsonStr,
        [callback, accountID, config](bool success, std::string const& msg) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (success) {
                ProfileThumbs::get().deleteProfile(accountID);
                ProfileThumbs::get().cacheProfileConfig(accountID, config);
            }
            callback(success, msg);
        });
}

void ProfileImageService::downloadProfileConfig(int accountID,
    geode::CopyableFunction<void(bool, ProfileConfig const&)> callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    if (!m_serverEnabled) { callback(false, ProfileConfig()); return; }

    HttpClient::get().downloadProfileConfig(accountID,
        [callback](bool success, std::string const& response) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!success || response.empty()) { callback(false, ProfileConfig()); return; }

            auto res = matjson::parse(response);
            if (!res.isOk()) { callback(false, ProfileConfig()); return; }
            auto const& json = res.unwrap();

            auto config = paimon::profiles::parseConfig(json);
            if (!config.hasConfig) { callback(false, config); return; }

            callback(true, config);
        });
}
