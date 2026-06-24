#include "ThumbnailTransportClient.hpp"
#include "../../../framework/async/CallbackFuture.hpp"
#include <Geode/loader/Loader.hpp>
#include "ThumbnailLoader.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../video/VideoNormalizer.hpp"
#include "../../../framework/HookInterceptor.hpp"
#include "../../../utils/FormatDetect.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <optional>

using namespace geode::prelude;

namespace {

using TransportThumbnailInfo = ThumbnailTransportClient::ThumbnailInfo;

std::string buildRatingCacheKey(int levelId, std::string const& username, std::string const& thumbnailId) {
    return fmt::format("{}|{}|{}", levelId, username, thumbnailId);
}

std::string buildRatingRequestKey(std::string const& cacheKey, uint64_t generation) {
    return fmt::format("{}#{}", cacheKey, generation);
}

std::string buildThumbnailRevisionToken(std::vector<TransportThumbnailInfo> const& thumbnails) {
    if (thumbnails.empty()) {
        return "empty";
    }

    std::string token;
    token.reserve(thumbnails.size() * 48);
    for (auto const& thumb : thumbnails) {
        token += std::to_string(thumb.position);
        token.push_back(':');
        token += thumb.id.empty() ? thumb.url : thumb.id;
        token.push_back(':');
        token += thumb.type;
        token.push_back(':');
        token += thumb.format;
        token.push_back(':');
        token += thumb.date;
        token.push_back(';');
    }
    return token;
}

bool parseThumbnailResponse(std::string const& response, std::vector<TransportThumbnailInfo>& thumbnails) {
    // Reject excessively large responses to prevent memory exhaustion
    constexpr size_t kMaxResponseSize = 2 * 1024 * 1024; // 2 MB
    if (response.size() > kMaxResponseSize) {
        return false;
    }

    auto res = matjson::parse(response);
    if (!res.isOk()) {
        return false;
    }

    auto json = res.unwrap();
    if (!json.contains("thumbnails") || !json["thumbnails"].isArray()) {
        thumbnails.clear();
        return true;
    }

    auto arrRes = json["thumbnails"].asArray();
    if (!arrRes.isOk()) {
        return false;
    }

    for (auto const& item : arrRes.unwrap()) {
        // Cap the number of thumbnails to prevent abuse from a rogue server
        constexpr size_t kMaxThumbnails = 200;
        if (thumbnails.size() >= kMaxThumbnails) break;

        TransportThumbnailInfo info;
        info.id = item["id"].asString().unwrapOr("");
        if (item.contains("thumbnailId") && info.id.empty()) {
            info.id = item["thumbnailId"].asString().unwrapOr("");
        }
        info.url = item["url"].asString().unwrapOr("");
        info.type = item["type"].asString().unwrapOr("");
        info.format = item["format"].asString().unwrapOr("");
        info.position = item["position"].asInt().unwrapOr(1);

        for (auto const& key : {"creator", "author", "username", "uploader", "uploaded_by", "submitted_by", "user", "owner"}) {
            if (item.contains(key)) {
                info.creator = item[key].asString().unwrapOr("Unknown");
                break;
            }
        }
        if (info.creator.empty()) info.creator = "Unknown";

        for (auto const& key : {"date", "uploaded_at", "created_at", "timestamp"}) {
            if (item.contains(key)) {
                info.date = item[key].asString().unwrapOr("Unknown");
                break;
            }
        }
        if (info.date.empty()) info.date = "Unknown";

        thumbnails.push_back(std::move(info));
    }

    return true;
}

} // namespace

// helpers

bool ThumbnailTransportClient::isGIFData(std::vector<uint8_t> const& data) {
    return data.size() >= 6 && paimon::format::isGif(data.data(), data.size());
}

cocos2d::CCTexture2D* ThumbnailTransportClient::bytesToTexture(std::vector<uint8_t> const& data) {
    if (data.empty()) return nullptr;
    log::debug("[ThumbTransport] bytesToTexture: {} bytes", data.size());

    // Delegate to ImageLoadHelper which already handles ImagePlus + stb_image fallback
    // with proper error handling and dimension validation
    auto loaded = ImageLoadHelper::loadWithSTBFromMemory(data.data(), data.size(), false /* no buffer copy needed */);
    if (loaded.success && loaded.texture) {
        loaded.texture->autorelease();
        return loaded.texture;
    }

    // Fallback: CCImage para JPEG y formatos que ImagePlus no soporta
    return webpToTexture(data);
}

cocos2d::CCTexture2D* ThumbnailTransportClient::webpToTexture(std::vector<uint8_t> const& data) {
    if (data.empty()) return nullptr;

    auto* img = new CCImage();
    if (!img->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
        log::error("[ThumbTransport] fallo al iniciar ccimage desde datos");
        img->release();
        return nullptr;
    }

    auto* tex = new CCTexture2D();
    if (!tex->initWithImage(img)) {
        tex->release();
        img->release();
        log::error("[ThumbTransport] fallo al crear textura desde imagen");
        return nullptr;
    }

    img->release();
    tex->autorelease();
    return tex;
}

cocos2d::CCTexture2D* ThumbnailTransportClient::loadFromLocal(int levelId) {
    if (!LocalThumbs::get().has(levelId)) return nullptr;
    return LocalThumbs::get().loadTexture(levelId);
}

// queries

bool ThumbnailTransportClient::hasGalleryMetadataCached(int levelId) {
    if (levelId <= 0) return false;
    std::lock_guard<std::mutex> lock(m_galleryMutex);
    auto it = m_galleryCache.find(levelId);
    return it != m_galleryCache.end() && !it->second.thumbnails.empty();
}

void ThumbnailTransportClient::getThumbnails(int levelId, ThumbnailListCallback callback, bool forceRefresh) {
    if (!callback) return;
    if (!m_serverEnabled) {
        log::debug("[ThumbTransport] getThumbnails: server disabled");
        callback(false, {});
        return;
    }
    if (levelId <= 0) {
        callback(false, {});
        return;
    }

    std::vector<ThumbnailInfo> cachedThumbnails;
    bool joinedInFlight = false;
    bool hasCachedEntry = false;

    {
        std::lock_guard<std::mutex> lock(m_galleryMutex);

        if (!forceRefresh) {
            auto cacheIt = m_galleryCache.find(levelId);
            if (cacheIt != m_galleryCache.end()) {
                cachedThumbnails = cacheIt->second.thumbnails;
                hasCachedEntry = true;
            }
        }

        if (!hasCachedEntry) {
            auto& callbacks = m_galleryInFlight[levelId];
            joinedInFlight = !callbacks.empty();
            callbacks.push_back(std::move(callback));
            // Asegura que la entrada exista para que el flush capture la
            // generación actual (0 si nunca se ha invalidado).
            (void)m_galleryGenerations[levelId];
        }
    }

    if (hasCachedEntry) {
        callback(true, cachedThumbnails);
        return;
    }

    if (joinedInFlight) {
        log::debug("[ThumbTransport] getThumbnails: joined in-flight request levelId={}", levelId);
        return;
    }

    log::debug("[ThumbTransport] getThumbnails: queued levelId={} forceRefresh={}", levelId, forceRefresh);

    // En lugar de disparar inmediatamente HttpClient::getThumbnails para 1 id,
    // encolamos en m_batchListPending y dejamos que scheduleBatchListFlush()
    // agrupe todos los ids pendientes en un único POST /api/thumbnails/list-batch.
    // Esto coalesce todas las peticiones que ocurren en la misma ventana de
    // ~50ms (típicamente: la inicialización de un LevelListLayer con N celdas).
    {
        std::lock_guard<std::mutex> lock(m_batchListMutex);
        m_batchListPending.push_back(levelId);
    }
    scheduleBatchListFlush();
}

void ThumbnailTransportClient::scheduleBatchListFlush() {
    bool expected = false;
    if (!m_batchListFlushScheduled.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return; // ya agendado
    }
    if (paimon::isRuntimeShuttingDown()) {
        m_batchListFlushScheduled.store(false, std::memory_order_release);
        return;
    }
    float delay = static_cast<float>(BATCH_LIST_FLUSH_DELAY_MS) / 1000.f;
    paimon::scheduleMainThreadDelay(delay, [this]() {
        if (paimon::isRuntimeShuttingDown()) {
            m_batchListFlushScheduled.store(false, std::memory_order_release);
            return;
        }
        flushBatchList();
    });
}

void ThumbnailTransportClient::flushBatchList() {
    std::vector<int> ids;
    bool moreLeft = false;
    {
        std::lock_guard<std::mutex> lock(m_batchListMutex);
        if (m_batchListPending.empty()) {
            m_batchListFlushScheduled.store(false, std::memory_order_release);
            return;
        }
        // Dedup preservando orden de inserción.
        std::unordered_map<int, char> seen;
        seen.reserve(m_batchListPending.size());
        for (int id : m_batchListPending) {
            if (id <= 0) continue;
            if (seen.emplace(id, 1).second) {
                ids.push_back(id);
                if (ids.size() >= BATCH_LIST_MAX_IDS) break;
            }
        }
        // Cualquier id que no entró en este flush queda para el próximo.
        if (ids.size() >= BATCH_LIST_MAX_IDS && m_batchListPending.size() > BATCH_LIST_MAX_IDS) {
            std::vector<int> remaining;
            remaining.reserve(m_batchListPending.size());
            for (int id : m_batchListPending) {
                if (seen.find(id) == seen.end()) remaining.push_back(id);
            }
            m_batchListPending = std::move(remaining);
            moreLeft = !m_batchListPending.empty();
        } else {
            m_batchListPending.clear();
        }
    }

    if (ids.empty()) {
        m_batchListFlushScheduled.store(false, std::memory_order_release);
        return;
    }

    // Capturamos las generaciones actuales para detectar invalidaciones que
    // ocurran entre la salida de la request y su respuesta.
    std::unordered_map<int, uint64_t> generations;
    {
        std::lock_guard<std::mutex> lock(m_galleryMutex);
        for (int id : ids) {
            generations[id] = m_galleryGenerations[id];
        }
    }

    log::info("[ThumbTransport] flushBatchList: dispatching {} ids in single request", ids.size());

    HttpClient::get().getThumbnailsBatch(ids,
        [this, ids, generations = std::move(generations)]
        (bool success, std::unordered_map<int, std::string> const& itemsJson) {
            for (int levelId : ids) {
                std::vector<ThumbnailListCallback> callbacks;
                std::vector<ThumbnailInfo> thumbnails;
                std::string revisionToken;
                bool callbackSuccess = false;
                bool servedCachedFallback = false;
                bool generationChanged = false;

                std::string responseJson;
                bool haveResponse = false;
                if (success) {
                    auto it = itemsJson.find(levelId);
                    if (it != itemsJson.end()) {
                        responseJson = it->second;
                        haveResponse = true;
                    }
                }

                bool parseOk = false;
                if (haveResponse) {
                    parseOk = parseThumbnailResponse(responseJson, thumbnails);
                    if (parseOk) {
                        revisionToken = buildThumbnailRevisionToken(thumbnails);
                    }
                }

                uint64_t requestGeneration = 0;
                auto genIt = generations.find(levelId);
                if (genIt != generations.end()) requestGeneration = genIt->second;

                {
                    std::lock_guard<std::mutex> lock(m_galleryMutex);

                    auto inFlightIt = m_galleryInFlight.find(levelId);
                    if (inFlightIt != m_galleryInFlight.end()) {
                        callbacks = std::move(inFlightIt->second);
                        m_galleryInFlight.erase(inFlightIt);
                    }

                    auto currentGeneration = m_galleryGenerations[levelId];
                    if (currentGeneration == requestGeneration && parseOk) {
                        GalleryMetadataEntry entry;
                        entry.thumbnails = thumbnails;
                        entry.revisionToken = revisionToken;
                        entry.fetchedAt = std::chrono::steady_clock::now();
                        m_galleryCache[levelId] = std::move(entry);
                        callbackSuccess = true;
                    } else if (currentGeneration == requestGeneration) {
                        auto cacheIt = m_galleryCache.find(levelId);
                        if (cacheIt != m_galleryCache.end()) {
                            thumbnails = cacheIt->second.thumbnails;
                            revisionToken = cacheIt->second.revisionToken;
                            callbackSuccess = true;
                            servedCachedFallback = true;
                        }
                    } else {
                        generationChanged = true;
                    }
                }

                if (callbackSuccess) {
                    ThumbnailLoader::get().updateRemoteRevision(levelId, revisionToken);
                } else {
                    log::debug("[ThumbTransport] flushBatchList: callback failed levelId={} generationChanged={}",
                        levelId, generationChanged);
                }

                if (servedCachedFallback) {
                    log::debug("[ThumbTransport] flushBatchList: cached fallback levelId={} count={}",
                        levelId, thumbnails.size());
                } else if (callbackSuccess) {
                    log::debug("[ThumbTransport] flushBatchList: fresh levelId={} count={}",
                        levelId, thumbnails.size());
                }

                for (auto& queued : callbacks) {
                    if (queued) queued(callbackSuccess, thumbnails);
                }
            }

            // Si quedaron ids fuera del cap, agendar otro pase.
            bool needsAnother = false;
            {
                std::lock_guard<std::mutex> lock(m_batchListMutex);
                needsAnother = !m_batchListPending.empty();
            }
            m_batchListFlushScheduled.store(false, std::memory_order_release);
            if (needsAnother) {
                scheduleBatchListFlush();
            }
        });

    if (moreLeft) {
        // Si esta función ya extrajo el cap pero quedó más en cola y por
        // alguna razón el callback de arriba no dispara (ej. fallo síncrono),
        // re-agendar de forma defensiva. compare_exchange en
        // scheduleBatchListFlush lo deduplica.
        m_batchListFlushScheduled.store(false, std::memory_order_release);
        scheduleBatchListFlush();
    }
}

void ThumbnailTransportClient::invalidateGalleryMetadata(int levelId) {
    if (levelId <= 0) return;

    std::lock_guard<std::mutex> lock(m_galleryMutex);
    m_galleryCache.erase(levelId);
    ++m_galleryGenerations[levelId];
}

void ThumbnailTransportClient::getThumbnailInfo(int levelId, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "Server disabled"); return; }
    HttpClient::get().getThumbnailInfo(levelId, [callback](bool s, std::string const& r) { callback(s, r); });
}

std::string ThumbnailTransportClient::getThumbnailURL(int levelId) {
    // Prefer direct CDN URL from manifest (img.flozwer.org) — 0 Worker invocations.
    // Fall back to Worker URL if manifest not yet fetched.
    auto manifest = HttpClient::get().getManifestEntry(levelId);
    if (manifest.has_value() && !manifest->cdnUrl.empty()) {
        return manifest->cdnUrl;
    }
    return HttpClient::get().getServerURL() + "/t/" + std::to_string(levelId) + ".webp";
}

// uploads

void ThumbnailTransportClient::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData,
                                               std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    // Hook interceptors: upload + validate + security
    paimon::HookContext ctx{"upload", levelId, username, "png", pngData.size(), &pngData};
    auto hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "validate";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "security-check";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }

    log::info("[ThumbTransport] subiendo miniatura nivel {} ({} bytes)", levelId, pngData.size());

    HttpClient::get().uploadThumbnail(levelId, pngData, username,
        [this, callback, levelId, username](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                // Invalidar cache completa del nivel para que se descargue fresca
                // (invalidateLevel ya limpia manifest, gallery metadata, exists cache, etc.)
                ThumbnailLoader::get().invalidateLevel(levelId);
                // Inmediatamente re-solicitar el thumbnail para pre-poblar RAM con datos frescos
                // del write-through CF cache (la UI vera el thumbnail nuevo sin delay)
                ThumbnailLoader::get().requestLoad(levelId, std::to_string(levelId), [](cocos2d::CCTexture2D*, bool){}, 0, false);
            }
            paimon::HookContext postCtx{"upload", levelId, username, "png", 0, nullptr};
            paimon::HookInterceptor::get().runPostHooks(postCtx, success);
            callback(success, message);
        });
}

void ThumbnailTransportClient::uploadGIF(int levelId, std::vector<uint8_t> const& gifData,
                                         std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    // Hook interceptors: upload + validate + security
    paimon::HookContext ctx{"upload", levelId, username, "gif", gifData.size(), &gifData};
    auto hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "validate";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "security-check";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }

    log::info("[ThumbTransport] subiendo gif nivel {} ({} bytes)", levelId, gifData.size());

    HttpClient::get().uploadGIF(levelId, gifData, username,
        [this, callback, levelId, username](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                // Invalidar cache completa del nivel para que se descargue fresca
                // (invalidateLevel ya limpia manifest, gallery metadata, exists cache, etc.)
                ThumbnailLoader::get().invalidateLevel(levelId);
                // Inmediatamente re-solicitar el thumbnail para pre-poblar RAM con datos frescos
                ThumbnailLoader::get().requestLoad(levelId, std::to_string(levelId), [](cocos2d::CCTexture2D*, bool){}, 0, true);
            }
            paimon::HookContext postCtx{"upload", levelId, username, "gif", 0, nullptr};
            paimon::HookInterceptor::get().runPostHooks(postCtx, success);
            callback(success, message);
        });
}

void ThumbnailTransportClient::uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data,
                                           std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    // Hook interceptors: upload + validate + security
    paimon::HookContext ctx{"upload", levelId, username, "mp4", mp4Data.size(), &mp4Data};
    auto hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "validate";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }
    ctx.action = "security-check";
    hookRes = paimon::HookInterceptor::get().runPreHooks(ctx);
    if (!hookRes.isAllowed()) { callback(false, hookRes.reason); return; }

    log::info("[ThumbTransport] subiendo video nivel {} ({} bytes)", levelId, mp4Data.size());

    // Normalize to canonical H.264+AAC before uploading
    auto normalRes = paimon::video::VideoNormalizer::normalizeData(
        mp4Data, fmt::format("upload_video_{}", levelId));
    auto const& uploadData = normalRes.isOk() ? normalRes.unwrap() : mp4Data;
    if (normalRes.isErr())
        log::warn("[ThumbTransport] Normalization failed: {} — uploading as-is",
                  normalRes.unwrapErr());

    HttpClient::get().uploadVideo(levelId, uploadData, username,
        [this, callback, levelId, username](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                // Invalidar cache completa del nivel para que se descargue fresca
                // (invalidateLevel ya limpia manifest, gallery metadata, exists cache, etc.)
                ThumbnailLoader::get().invalidateLevel(levelId);
                // Inmediatamente re-solicitar el thumbnail para pre-poblar RAM con datos frescos
                ThumbnailLoader::get().requestLoad(levelId, std::to_string(levelId), [](cocos2d::CCTexture2D*, bool){}, 0, false);
            }
            paimon::HookContext postCtx{"upload", levelId, username, "mp4", 0, nullptr};
            paimon::HookInterceptor::get().runPostHooks(postCtx, success);
            callback(success, message);
        });
}

// downloads

void ThumbnailTransportClient::downloadThumbnail(int levelId, DownloadCallback callback, bool isGif) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }
    log::info("[ThumbTransport] downloadThumbnail: levelId={} isGif={}", levelId, isGif);

    HttpClient::get().downloadThumbnail(levelId, isGif,
        [callback, levelId](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { log::warn("[ThumbTransport] downloadThumbnail callback: FAILED levelId={}", levelId); callback(false, nullptr); return; }
            log::info("[ThumbTransport] downloadThumbnail callback: OK levelId={} bytes={}", levelId, data.size());
            callback(success, bytesToTexture(data));
        });
}

void ThumbnailTransportClient::getThumbnail(int levelId, DownloadCallback callback) {
    // 1. local
    if (auto* tex = loadFromLocal(levelId)) { log::debug("[ThumbTransport] getThumbnail: local hit levelId={}", levelId); callback(true, tex); return; }
    // 2. servidor
    if (m_serverEnabled) {
        log::debug("[ThumbTransport] getThumbnail: fetching from server levelId={}", levelId);
        downloadThumbnail(levelId, callback);
    } else {
        callback(false, nullptr);
    }
}

void ThumbnailTransportClient::downloadFromUrl(std::string const& url, DownloadCallback callback) {
    log::debug("[ThumbTransport] downloadFromUrl: {}", url);
    HttpClient::get().downloadFromUrl(url, [callback, url](bool success, std::vector<uint8_t> const& data, int, int) {
        if (success && !data.empty()) {
            log::debug("[ThumbTransport] downloadFromUrl callback: OK bytes={}", data.size());
            callback(success, bytesToTexture(data));
        } else if (success && data.empty()) {
            // CCTextureCache hit: la textura ya existe en Cocos2d cache.
            // CCTextureCache solo se puede tocar desde el main thread.
            Loader::get()->queueInMainThread([callback, url]() {
                auto* tex = CCTextureCache::sharedTextureCache()->textureForKey(url.c_str());
                if (tex) {
                    log::debug("[ThumbTransport] downloadFromUrl callback: CCTextureCache hit url={}", url);
                    callback(true, tex);
                } else {
                    log::warn("[ThumbTransport] downloadFromUrl callback: empty data but no cache tex url={}", url);
                    callback(false, nullptr);
                }
            });
        } else {
            log::warn("[ThumbTransport] downloadFromUrl callback: FAILED url={}", url);
            callback(false, nullptr);
        }
    });
}

void ThumbnailTransportClient::downloadFromUrlData(std::string const& url, DownloadDataCallback callback) {
    HttpClient::get().downloadFromUrl(url, [callback](bool success, std::vector<uint8_t> const& data, int, int) {
        callback(success, data);
    });
}

// exists / delete

void ThumbnailTransportClient::checkExists(int levelId, ExistsCallback callback) {
    if (!m_serverEnabled) { callback(false); return; }
    log::debug("[ThumbTransport] checkExists: levelId={}", levelId);
    HttpClient::get().checkThumbnailExists(levelId, callback);
}

void ThumbnailTransportClient::deleteThumbnail(int levelId, std::string const& thumbnailId, std::string const& username,
                                               int accountID, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    log::info("[ThumbTransport] deleteThumbnail: levelId={} thumbId={} user={}", levelId, thumbnailId, username);

    std::string endpoint = fmt::format("/api/thumbnails/delete/{}", levelId);

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"levelId", levelId},
        {"thumbnailId", thumbnailId},
        {"accountID", accountID}
    });
    std::string postData = json.dump();

    HttpClient::get().postWithAuth(endpoint, postData,
        [callback, levelId](bool success, std::string const& response) {
            if (success) {
                log::info("[ThumbTransport] deleteThumbnail callback: OK levelId={}", levelId);
                ThumbnailLoader::get().invalidateLevel(levelId);
                callback(true, "miniatura borrada con exito");
            } else {
                log::warn("[ThumbTransport] deleteThumbnail callback: FAILED levelId={} resp={}", levelId, response);
                callback(false, response);
            }
        });
}

void ThumbnailTransportClient::reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds,
                                                 ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    if (levelId <= 0 || thumbnailIds.size() < 2) { callback(false, "datos invalidos para reordenar"); return; }

    log::info("[ThumbTransport] reorderThumbnails: levelId={} count={}", levelId, thumbnailIds.size());

    HttpClient::get().reorderThumbnails(levelId, thumbnailIds,
        [callback, levelId](bool success, std::string const& response) {
            if (success) {
                log::info("[ThumbTransport] reorderThumbnails callback: OK levelId={}", levelId);
                ThumbnailLoader::get().invalidateLevel(levelId);
                ThumbnailTransportClient::get().invalidateGalleryMetadata(levelId);
                callback(true, response);
            } else {
                log::warn("[ThumbTransport] reorderThumbnails callback: FAILED levelId={} resp={}", levelId, response);
                callback(false, response);
            }
        });
}

// ratings

void ThumbnailTransportClient::getRating(int levelId, std::string const& username,
                                         std::string const& thumbnailId,
                                         RatingCallback callback) {
    if (!m_serverEnabled) { callback(false, 0, 0, 0); return; }
    log::debug("[ThumbTransport] getRating: levelId={} thumbId={}", levelId, thumbnailId);

    auto cacheKey = buildRatingCacheKey(levelId, username, thumbnailId);
    uint64_t requestGeneration = 0;
    std::optional<RatingCacheEntry> cachedEntry;
    std::string requestKey;
    {
        std::lock_guard<std::mutex> lock(m_ratingMutex);

        auto cacheIt = m_ratingCache.find(cacheKey);
        if (cacheIt != m_ratingCache.end()) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - cacheIt->second.fetchedAt);
            if (age.count() <= RATING_CACHE_TTL_SECONDS) {
                cachedEntry = cacheIt->second;
            } else {
                m_ratingCache.erase(cacheIt);
            }
        }

        if (cachedEntry.has_value()) {
            requestGeneration = m_ratingGenerations[cacheKey];
        } else {
            requestGeneration = m_ratingGenerations[cacheKey];
            requestKey = buildRatingRequestKey(cacheKey, requestGeneration);
            auto& callbacks = m_ratingInFlight[requestKey];
            if (!callbacks.empty()) {
                callbacks.push_back(std::move(callback));
                return;
            }
            callbacks.push_back(std::move(callback));
        }
    }

    if (cachedEntry.has_value()) {
        callback(true, cachedEntry->average, cachedEntry->count, cachedEntry->userVote);
        return;
    }

    HttpClient::get().getRating(levelId, username, thumbnailId,
        [this, cacheKey, requestKey, requestGeneration](bool success, std::string const& response) {
            std::vector<RatingCallback> callbacks;
            RatingCacheEntry parsedEntry;
            bool parsedSuccess = false;

            if (success) {
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    parsedEntry.average = static_cast<float>(json["average"].asDouble().unwrapOr(0.0));
                    parsedEntry.count = static_cast<int>(json["count"].asInt().unwrapOr(0));
                    parsedEntry.userVote = static_cast<int>(json["userVote"].asInt().unwrapOr(0));
                    parsedEntry.fetchedAt = std::chrono::steady_clock::now();
                    parsedSuccess = true;
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_ratingMutex);
                auto inflightIt = m_ratingInFlight.find(requestKey);
                if (inflightIt != m_ratingInFlight.end()) {
                    callbacks = std::move(inflightIt->second);
                    m_ratingInFlight.erase(inflightIt);
                }
                if (parsedSuccess && m_ratingGenerations[cacheKey] == requestGeneration) {
                    m_ratingCache[cacheKey] = parsedEntry;
                }
            }

            for (auto& queued : callbacks) {
                if (queued) queued(parsedSuccess, parsedEntry.average, parsedEntry.count, parsedEntry.userVote);
            }
        });
}

void ThumbnailTransportClient::invalidateRatingCache(int levelId, std::string const& thumbnailId) {
    std::lock_guard<std::mutex> lock(m_ratingMutex);

    for (auto it = m_ratingCache.begin(); it != m_ratingCache.end();) {
        std::string prefix = fmt::format("{}|", levelId);
        if (!it->first.starts_with(prefix)) {
            ++it;
            continue;
        }

        if (!thumbnailId.empty()) {
            std::string suffix = fmt::format("|{}", thumbnailId);
            if (!it->first.ends_with(suffix)) {
                ++it;
                continue;
            }
        }

        it = m_ratingCache.erase(it);
    }
    for (auto& [key, generation] : m_ratingGenerations) {
        std::string prefix = fmt::format("{}|", levelId);
        if (!key.starts_with(prefix)) {
            continue;
        }

        if (!thumbnailId.empty()) {
            std::string suffix = fmt::format("|{}", thumbnailId);
            if (!key.ends_with(suffix)) {
                continue;
            }
        }

        ++generation;
    }
}

void ThumbnailTransportClient::submitVote(int levelId, int stars, std::string const& username,
                                          std::string const& thumbnailId, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "Server disabled"); return; }
    log::info("[ThumbTransport] submitVote: levelId={} stars={} thumbId={}", levelId, stars, thumbnailId);
    HttpClient::get().submitVote(levelId, stars, username, thumbnailId,
        [this, callback, levelId, thumbnailId](bool success, std::string const& response) {
            if (success) {
                invalidateRatingCache(levelId, thumbnailId);
            }
            callback(success, response);
        });
}

// top lists

void ThumbnailTransportClient::getTopCreators(ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    HttpClient::get().getTopCreators([callback](bool s, std::string const& r) { callback(s, r); });
}

void ThumbnailTransportClient::getTopThumbnails(ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    HttpClient::get().getTopThumbnails([callback](bool s, std::string const& r) { callback(s, r); });
}

void ThumbnailTransportClient::getUserUploads(std::string const& username, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    HttpClient::get().getUserUploads(username, [callback](bool s, std::string const& r) { callback(s, r); });
}

// arc::Future

arc::Future<ThumbnailGalleryResult> ThumbnailTransportClient::fetchThumbnailsFuture(int levelId, bool forceRefresh) {
    struct Pending {
        std::shared_ptr<arc::oneshot::Sender<ThumbnailGalleryResult>> sender;
    };

    auto [tx, rx] = arc::oneshot::channel<ThumbnailGalleryResult>();
    auto pending = std::make_shared<Pending>(Pending{
        std::make_shared<arc::oneshot::Sender<ThumbnailGalleryResult>>(std::move(tx)),
    });

    geode::Loader::get()->queueInMainThread([pending, levelId, forceRefresh]() {
        ThumbnailTransportClient::get().getThumbnails(
            levelId,
            [pending](bool success, std::vector<ThumbnailInfo> const& thumbs) {
                ThumbnailGalleryResult result{
                    .success = success,
                    .thumbnails = thumbs,
                };
                geode::Loader::get()->queueInMainThread([pending, result = std::move(result)]() mutable {
                    (void)pending->sender->send(std::move(result));
                });
            },
            forceRefresh
        );
    });

    auto received = co_await rx.recv();
    if (received.isOk()) {
        co_return std::move(received).unwrap();
    }
    co_return ThumbnailGalleryResult{};
}

arc::Future<ThumbnailTextureResult> ThumbnailTransportClient::downloadFromUrlFuture(std::string url) {
    return paimon::async_api::awaitCallback<ThumbnailTextureResult>([url = std::move(url)](auto const& cb) {
        ThumbnailTransportClient::get().downloadFromUrl(url, [cb](bool success, CCTexture2D* texture) {
            ThumbnailTextureResult result{.success = success};
            if (texture) result.texture = texture;
            cb(std::move(result));
        });
    });
}

arc::Future<ThumbnailDataResult> ThumbnailTransportClient::downloadFromUrlDataFuture(std::string url) {
    return paimon::async_api::awaitCallback<ThumbnailDataResult>([url = std::move(url)](auto const& cb) {
        ThumbnailTransportClient::get().downloadFromUrlData(url, [cb](bool success, std::vector<uint8_t> const& data) {
            cb(ThumbnailDataResult{.success = success, .data = data});
        });
    });
}

arc::Future<ThumbnailTextureResult> ThumbnailTransportClient::downloadThumbnailFuture(int levelId, bool isGif) {
    return paimon::async_api::awaitCallback<ThumbnailTextureResult>([levelId, isGif](auto const& cb) {
        ThumbnailTransportClient::get().downloadThumbnail(levelId, [cb](bool success, CCTexture2D* texture) {
            ThumbnailTextureResult result{.success = success};
            if (texture) result.texture = texture;
            cb(std::move(result));
        }, isGif);
    });
}

arc::Future<ThumbnailTextureResult> ThumbnailTransportClient::getThumbnailFuture(int levelId) {
    return paimon::async_api::awaitCallback<ThumbnailTextureResult>([levelId](auto const& cb) {
        ThumbnailTransportClient::get().getThumbnail(levelId, [cb](bool success, CCTexture2D* texture) {
            ThumbnailTextureResult result{.success = success};
            if (texture) result.texture = texture;
            cb(std::move(result));
        });
    });
}

arc::Future<bool> ThumbnailTransportClient::checkExistsFuture(int levelId) {
    return paimon::async_api::awaitCallback<bool>([levelId](auto const& cb) {
        ThumbnailTransportClient::get().checkExists(levelId, [cb](bool exists) { cb(exists); });
    });
}
