#include "../../../utils/FilesystemPath.hpp"
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
std::atomic<bool> s_profileImageShuttingDown{false};
std::atomic<bool> s_profileImageInitialized{false};

std::mutex& profileImagePoolMutex() {
    static auto* m = new std::mutex();
    return *m;
}

std::mutex& profileImageCompletionsMutex() {
    static auto* m = new std::mutex();
    return *m;
}

std::vector<std::weak_ptr<ProfileImageService::DownloadCallback>>& profileImageCompletions() {
    static auto* v = new std::vector<std::weak_ptr<ProfileImageService::DownloadCallback>>();
    return *v;
}

paimon::ThreadPool* profileImagePool() {
    if (s_profileImageShuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        std::lock_guard lock(profileImagePoolMutex());
        return s_profileImagePool;
    }
    std::lock_guard lock(profileImagePoolMutex());
    // no new workers once teardown started; callers treat a null pool as
    // "drop this job" instead of unwinding during exit.
    if (s_profileImageShuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return s_profileImagePool;
    }
    if (!s_profileImagePool) s_profileImagePool = new paimon::ThreadPool(2, "PaimonProfileImg");
    return s_profileImagePool;
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
    {
        std::lock_guard lock(profileImageCompletionsMutex());
        std::erase_if(profileImageCompletions(), [](auto const& item) { return item.expired(); });
        profileImageCompletions().emplace_back(completion);
    }
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
    auto* pool = profileImagePool();
    if (!pool) {
        fail();
        return;
    }
    try {
        pool->enqueue([data = std::move(data), completion, fail]() {
            if (paimon::isRuntimeShuttingDown()) return;
            try {
                // same info-probe, dim-cap and stb decode as ImageLoadHelper;
                // copyBuffer keeps the pixels alive for the downsample below.
                auto decoded = ImageLoadHelper::loadWithSTBFromMemory(data->data(), data->size(), true);
                if (!decoded.success || !decoded.buffer) {
                    fail();
                    return;
                }
                int w = decoded.width, h = decoded.height;
                std::shared_ptr<uint8_t> pixels = std::move(decoded.buffer);
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

        auto stem = paimon::paths::toUtf8(entry.path().stem());
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

bool ProfileImageService::isInitialized() {
    return s_profileImageInitialized.load(std::memory_order_acquire);
}

ProfileImageService& ProfileImageService::get() {
    // Heap-leaked: never destroy under DllMain loader lock.
    static auto* instance = []() {
        auto* service = new ProfileImageService();
        s_profileImageInitialized.store(true, std::memory_order_release);
        return service;
    }();
    return *instance;
}

ProfileImageService::ProfileImageService() {
    auto cacheDir = getProfileImgCacheDir();
    auto maxBytes = getProfileImgCacheMaxBytes();
    if (auto* pool = profileImagePool()) {
        pool->enqueue([cacheDir, maxBytes]() { pruneProfileImgCache(cacheDir, maxBytes); });
    }
}

void ProfileImageService::requestShutdown() {
    s_profileImageShuttingDown.store(true, std::memory_order_release);
    std::vector<std::weak_ptr<DownloadCallback>> completions;
    {
        std::lock_guard lock(profileImageCompletionsMutex());
        completions.swap(profileImageCompletions());
    }
    for (auto const& weak : completions) {
        if (auto completion = weak.lock()) *completion = nullptr;
    }
    std::lock_guard lock(profileImagePoolMutex());
    if (s_profileImagePool) s_profileImagePool->requestStop();
}

void ProfileImageService::waitShutdown(std::chrono::steady_clock::time_point deadline) {
    requestShutdown();
    paimon::ThreadPool* pool = nullptr;
    {
        std::lock_guard lock(profileImagePoolMutex());
        pool = s_profileImagePool;
    }
    if (pool) pool->waitShutdown(deadline);
    std::lock_guard lock(m_profileImgGifMutex);
    m_profileImgGifKeys.clear();
}

void ProfileImageService::shutdown() {
    waitShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
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


bool ProfileImageService::checkUploadPreconditions(int accountID, UploadCallback const& callback,
                                                   char const* loginMessage) {
    if (!callback || paimon::isRuntimeShuttingDown()) return false;
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        callback(false, loginMessage);
        return false;
    }
    if (accountID != accountManager->m_accountID) {
        callback(false, "La cuenta no coincide con la sesion.");
        return false;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return false; }
    return true;
}

void ProfileImageService::uploadProfile(int accountID, std::vector<uint8_t> const& pngData,
                                        std::string const& username, UploadCallback callback) {
    if (!checkUploadPreconditions(accountID, callback, "Debes estar logueado para subir miniaturas.")) return;

    HttpClient::get().uploadProfile(accountID, pngData, username,
        uploadCompletion(accountID, std::move(callback), true));
}

void ProfileImageService::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData,
                                           std::string const& username, UploadCallback callback) {
    if (!checkUploadPreconditions(accountID, callback, "Debes estar logueado para subir miniaturas.")) return;

    HttpClient::get().uploadProfileGIF(accountID, gifData, username,
        uploadCompletion(accountID, std::move(callback), true));
}

void ProfileImageService::uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data,
                                             std::string const& username, UploadCallback callback) {
    if (!checkUploadPreconditions(accountID, callback, "Debes estar logueado para subir miniaturas.")) return;

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
    if (!checkUploadPreconditions(accountID, callback, "Debes estar logueado para subir imagen de perfil.")) return;

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
            if (auto* pool = profileImagePool()) {
                pool->enqueue([cacheDir, maxBytes]() { pruneProfileImgCache(cacheDir, maxBytes); });
            }

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
