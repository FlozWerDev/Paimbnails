#include "ChatImageService.hpp"
#include "ChatImageCrypto.hpp"

#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/AtomicFileWrite.hpp"
#include "../../utils/FilesystemPath.hpp"
#include "../../utils/FormatDetect.hpp"
#include "../../utils/HttpClient.hpp"
#include "../../utils/ImageConverter.hpp"
#include "../../utils/ImageLoadHelper.hpp"
#include "../../utils/RgbaScaler.hpp"
#include "../community-base/services/CommunityClient.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

using namespace geode::prelude;

namespace paimon::chatimg {

namespace {

bool readFileBytes(std::filesystem::path const& path, size_t maxBytes, std::vector<uint8_t>& out, std::string& err) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        err = "File not found";
        return false;
    }
    auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < 12 || fileSize > maxBytes) {
        err = fileSize > maxBytes ? "File is too large" : "File is empty or invalid";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        err = "Could not open file";
        return false;
    }
    out.resize(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(fileSize))) {
        out.clear();
        err = "Could not read file";
        return false;
    }
    return true;
}

bool parseGifDimensions(uint8_t const* data, size_t size, int& outW, int& outH) {
    if (!data || size < 13) return false;
    if (paimon::format::detect(data, size) != paimon::format::ImageFormat::GIF) return false;
    outW = static_cast<int>(data[6]) | (static_cast<int>(data[7]) << 8);
    outH = static_cast<int>(data[8]) | (static_cast<int>(data[9]) << 8);
    if (outW <= 0 || outH <= 0) return false;

    // Require valid GIF trailer (0x3B) near the end so polyglots/corrupt streams fail closed.
    size_t tailCheckStart = (size > 17) ? (size - 17) : 13;
    for (size_t i = size; i > tailCheckStart; --i) {
        if (data[i - 1] == 0x3B) return true;
    }
    return false;
}

std::pair<int, int> fitWithinMaxSide(int w, int h, int maxSide) {
    if (w <= maxSide && h <= maxSide) return {w, h};
    if (w >= h) {
        int dstH = std::max(1, static_cast<int>(std::lround(double(h) * double(maxSide) / double(w))));
        return {maxSide, dstH};
    }
    int dstW = std::max(1, static_cast<int>(std::lround(double(w) * double(maxSide) / double(h))));
    return {dstW, maxSide};
}

bool bufferHasTransparency(uint8_t const* rgba, size_t pixelCount) {
    for (size_t i = 0; i < pixelCount; ++i) {
        if (rgba[i * 4 + 3] < 250) return true;
    }
    return false;
}

PreparedUpload encodeStaticRgba(
    uint8_t const* rgba, int w, int h,
    size_t maxBytes, int maxSide, int fallbackSide
) {
    PreparedUpload out;
    if (!rgba || w < 1 || h < 1) {
        out.error = "Invalid image dimensions";
        return out;
    }

    auto [dstW, dstH] = fitWithinMaxSide(w, h, maxSide);
    std::unique_ptr<uint8_t[]> scaledBuf;
    uint8_t const* working = rgba;
    if (dstW != w || dstH != h) {
        scaledBuf = paimon::rgba::scale(rgba, w, h, dstW, dstH);
        if (!scaledBuf) {
            out.error = "Failed to resize image";
            return out;
        }
        working = scaledBuf.get();
    }

    bool const hasAlpha = bufferHasTransparency(working, static_cast<size_t>(dstW) * dstH);

    if (!hasAlpha) {
        if (ImageConverter::rgbaToJpegBuffer(working, static_cast<uint32_t>(dstW), static_cast<uint32_t>(dstH), out.bytes, 82)
            && out.bytes.size() <= maxBytes) {
            out.ext = "jpg";
            out.mime = "image/jpeg";
            out.width = dstW;
            out.height = dstH;
            return out;
        }
        out.bytes.clear();
        if (ImageConverter::rgbaToJpegBuffer(working, static_cast<uint32_t>(dstW), static_cast<uint32_t>(dstH), out.bytes, 68)
            && out.bytes.size() <= maxBytes) {
            out.ext = "jpg";
            out.mime = "image/jpeg";
            out.width = dstW;
            out.height = dstH;
            return out;
        }
        out.bytes.clear();
        auto [smW, smH] = fitWithinMaxSide(dstW, dstH, fallbackSide);
        if (smW != dstW || smH != dstH) {
            auto smaller = paimon::rgba::scale(working, dstW, dstH, smW, smH);
            if (smaller && ImageConverter::rgbaToJpegBuffer(smaller.get(), static_cast<uint32_t>(smW), static_cast<uint32_t>(smH), out.bytes, 68)
                && out.bytes.size() <= maxBytes) {
                out.ext = "jpg";
                out.mime = "image/jpeg";
                out.width = smW;
                out.height = smH;
                return out;
            }
        }
        out.bytes.clear();
        out.error = "Image exceeds 2 MB limit even after compression";
        return out;
    }

    if (ImageConverter::rgbaToPngBuffer(working, static_cast<uint32_t>(dstW), static_cast<uint32_t>(dstH), out.bytes)
        && out.bytes.size() <= maxBytes) {
        out.ext = "png";
        out.mime = "image/png";
        out.width = dstW;
        out.height = dstH;
        return out;
    }
    out.bytes.clear();
    auto [smW, smH] = fitWithinMaxSide(dstW, dstH, fallbackSide);
    std::unique_ptr<uint8_t[]> smaller;
    uint8_t const* fallbackSrc = working;
    int fw = dstW;
    int fh = dstH;
    if (smW != dstW || smH != dstH) {
        smaller = paimon::rgba::scale(working, dstW, dstH, smW, smH);
        if (smaller) {
            fallbackSrc = smaller.get();
            fw = smW;
            fh = smH;
        }
    }
    if (ImageConverter::rgbaToPngBuffer(fallbackSrc, static_cast<uint32_t>(fw), static_cast<uint32_t>(fh), out.bytes)
        && out.bytes.size() <= maxBytes) {
        out.ext = "png";
        out.mime = "image/png";
        out.width = fw;
        out.height = fh;
        return out;
    }
    out.bytes.clear();
    if (ImageConverter::rgbaToJpegBuffer(fallbackSrc, static_cast<uint32_t>(fw), static_cast<uint32_t>(fh), out.bytes, 78)
        && out.bytes.size() <= maxBytes) {
        out.ext = "jpg";
        out.mime = "image/jpeg";
        out.width = fw;
        out.height = fh;
        return out;
    }
    out.bytes.clear();
    out.error = "Image exceeds 2 MB limit";
    return out;
}

bool ccImageToRgba(CCImage& img, std::vector<uint8_t>& outRgba, int& outW, int& outH) {
    outW = img.getWidth();
    outH = img.getHeight();
    auto* raw = img.getData();
    if (!raw || outW < 1 || outH < 1) return false;
    size_t const pixels = static_cast<size_t>(outW) * static_cast<size_t>(outH);
    outRgba.resize(pixels * 4u);
    if (img.hasAlpha()) {
        std::memcpy(outRgba.data(), raw, pixels * 4u);
    } else {
        for (size_t i = 0; i < pixels; ++i) {
            outRgba[i * 4 + 0] = raw[i * 3 + 0];
            outRgba[i * 4 + 1] = raw[i * 3 + 1];
            outRgba[i * 4 + 2] = raw[i * 3 + 2];
            outRgba[i * 4 + 3] = 255;
        }
    }
    return true;
}

void dispatchOnMain(geode::CopyableFunction<void()> fn) {
    if (paimon::isRuntimeShuttingDown()) return;
    geode::queueInMainThread([fn = std::move(fn)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        fn();
    });
}

} // namespace

namespace {
std::atomic<bool> s_chatImageServiceInitialized{false};
} // namespace

bool ChatImageService::isInitialized() {
    return s_chatImageServiceInitialized.load(std::memory_order_acquire);
}

ChatImageService& ChatImageService::get() {
    // Heap-leaked: ~ThreadPool() must never join workers under Windows Loader Lock in DllMain.
    static auto* instance = []() {
        auto* service = new ChatImageService();
        s_chatImageServiceInitialized.store(true, std::memory_order_release);
        return service;
    }();
    return *instance;
}

ChatImageService::ChatImageService() = default;

paimon::ThreadPool* ChatImageService::ensurePool() {
    if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        return m_pool;
    }
    std::lock_guard<std::mutex> lock(m_poolMutex);
    if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return m_pool;
    }
    if (!m_pool) {
        m_pool = new paimon::ThreadPool(2, "ChatImagePool");
    }
    return m_pool;
}

void ChatImageService::enqueuePoolJob(std::function<void()> job) {
    if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) return;
    if (auto* pool = ensurePool(); pool && !pool->isStopped()) {
        pool->enqueue(std::move(job));
    }
}

void ChatImageService::requestShutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_poolMutex);
    if (m_pool) m_pool->requestStop();
}

void ChatImageService::waitShutdown(std::chrono::steady_clock::time_point deadline) {
    requestShutdown();
    paimon::ThreadPool* pool = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        pool = m_pool;
    }
    if (pool) pool->waitShutdown(deadline);
}

void ChatImageService::shutdown() {
    waitShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
}

std::filesystem::path ChatImageService::cacheDir() const {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "cache" / "chat-images";
}

std::filesystem::path ChatImageService::diskPathForId(std::string const& imageId) const {
    return cacheDir() / (imageId + ".bin");
}

std::string ChatImageService::makeRamKey(ChatImageRef const& ref, int threadID) {
    return std::to_string(threadID) + ":" + ref.id + ":" + ref.sig;
}

void ChatImageService::prepareFromFile(std::filesystem::path const& path, PrepareCallback cb) {
    enqueuePoolJob([this, path, cb = std::move(cb)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        std::vector<uint8_t> raw;
        std::string err;
        constexpr size_t kMaxInputFileBytes = 24u * 1024u * 1024u;
        if (!readFileBytes(path, kMaxInputFileBytes, raw, err)) {
            dispatchOnMain([cb = std::move(cb), err = std::move(err)]() mutable {
                PreparedUpload failed;
                failed.error = err;
                cb(std::move(failed));
            });
            return;
        }
        paimon::ClipboardImageData data;
        data.encodedBytes = std::move(raw);
        data.isEncodedFile = true;
        prepareFromClipboard(std::move(data), std::move(cb));
    });
}

void ChatImageService::prepareFromClipboard(paimon::ClipboardImageData data, PrepareCallback cb) {
    enqueuePoolJob([this, data = std::move(data), cb = std::move(cb)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;

        if (!data.isEncodedFile) {
            auto prepared = encodeStaticRgba(
                data.rgba.data(), data.width, data.height,
                kMaxStaticUploadBytes, kMaxStaticDimension, kFallbackStaticDimension
            );
            dispatchOnMain([cb = std::move(cb), prepared = std::move(prepared)]() mutable {
                cb(std::move(prepared));
            });
            return;
        }

        auto fmt = paimon::format::detect(data.encodedBytes.data(), data.encodedBytes.size());
        if (fmt == paimon::format::ImageFormat::GIF) {
            PreparedUpload out;
            int gw = 0;
            int gh = 0;
            if (!parseGifDimensions(data.encodedBytes.data(), data.encodedBytes.size(), gw, gh)) {
                out.error = "Invalid GIF image";
            } else if (data.encodedBytes.size() > kMaxGifUploadBytes) {
                out.error = "GIF exceeds 4 MB limit";
            } else if (gw > kMaxGifDimension || gh > kMaxGifDimension) {
                out.error = "GIF dimensions exceed 1920x1920";
            } else {
                out.bytes = std::move(data.encodedBytes);
                out.ext = "gif";
                out.mime = "image/gif";
                out.width = gw;
                out.height = gh;
                out.isGif = true;
            }
            dispatchOnMain([cb = std::move(cb), out = std::move(out)]() mutable {
                cb(std::move(out));
            });
            return;
        }

        if (fmt == paimon::format::ImageFormat::WebP) {
            int wpW = 0;
            int wpH = 0;
            if (!paimon::format::probeWebpDimensions(data.encodedBytes.data(), data.encodedBytes.size(), wpW, wpH) ||
                wpW <= 0 || wpH <= 0 || wpW > 4096 || wpH > 4096) {
                dispatchOnMain([cb = std::move(cb)]() mutable {
                    PreparedUpload failed;
                    failed.error = "Unsupported or oversized WebP image";
                    cb(std::move(failed));
                });
                return;
            }
            dispatchOnMain([this, raw = std::move(data.encodedBytes), cb = std::move(cb)]() mutable {
                CCImage img;
                std::vector<uint8_t> rgba;
                int w = 0;
                int h = 0;
                if (!img.initWithImageData(raw.data(), static_cast<int>(raw.size())) || !ccImageToRgba(img, rgba, w, h)) {
                    PreparedUpload failed;
                    failed.error = "Unsupported or corrupt WebP image";
                    cb(std::move(failed));
                    return;
                }
                enqueuePoolJob([rgba = std::move(rgba), w, h, cb = std::move(cb)]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto out = encodeStaticRgba(
                        rgba.data(), w, h,
                        kMaxStaticUploadBytes, kMaxStaticDimension, kFallbackStaticDimension
                    );
                    dispatchOnMain([cb = std::move(cb), out = std::move(out)]() mutable {
                        cb(std::move(out));
                    });
                });
            });
            return;
        }

        int w = 0;
        int h = 0;
        ImageLoadHelper::StbDecodeError stbErr = ImageLoadHelper::StbDecodeError::None;
        auto decoded = ImageLoadHelper::decodeRGBAFromMemory(
            data.encodedBytes.data(), data.encodedBytes.size(), w, h, &stbErr
        );
        if (!decoded) {
            std::string msg = stbErr == ImageLoadHelper::StbDecodeError::InvalidDimensions
                ? "Image dimensions are too large"
                : "Unsupported image format";
            dispatchOnMain([cb = std::move(cb), msg = std::move(msg)]() mutable {
                PreparedUpload failed;
                failed.error = msg;
                cb(std::move(failed));
            });
            return;
        }

        auto out = encodeStaticRgba(
            decoded.get(), w, h,
            kMaxStaticUploadBytes, kMaxStaticDimension, kFallbackStaticDimension
        );
        dispatchOnMain([cb = std::move(cb), out = std::move(out)]() mutable {
            cb(std::move(out));
        });
    });
}

void ChatImageService::uploadPrepared(PreparedUpload const& prepared, int threadID, UploadCallback cb) {
    if (!prepared.ok()) {
        UploadResult res;
        res.error = prepared.error.empty() ? "No prepared image to upload" : prepared.error;
        cb(std::move(res));
        return;
    }
    if (threadID <= 0) {
        UploadResult res;
        res.error = "A valid group or DM thread is required";
        cb(std::move(res));
        return;
    }

    auto identity = paimon::community::currentIdentity();
    if (!identity || !paimon::community::hasAccountProof()) {
        UploadResult res;
        res.needsVerification = true;
        res.error = "Account verification required";
        cb(std::move(res));
        return;
    }

    enqueuePoolJob([this, prepared, threadID, identity = *identity, cb = std::move(cb)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        std::vector<uint8_t> encryptedBlob;
        std::string key22;
        if (!crypto::sealChatImage(
                prepared.bytes, prepared.ext, prepared.width, prepared.height, threadID,
                encryptedBlob, key22)) {
            dispatchOnMain([cb = std::move(cb)]() mutable {
                UploadResult res;
                res.error = "Failed to encrypt image";
                cb(std::move(res));
            });
            return;
        }

        dispatchOnMain([this, prepared = std::move(prepared), encryptedBlob = std::move(encryptedBlob),
                        key22 = std::move(key22), threadID, identity = std::move(identity),
                        cb = std::move(cb)]() mutable {
            std::vector<std::pair<std::string, std::string>> fields = {
                {"username", identity.username},
                {"accountID", std::to_string(identity.accountID)},
                {"threadID", std::to_string(threadID)},
                {"ext", prepared.ext},
                {"width", std::to_string(prepared.width)},
                {"height", std::to_string(prepared.height)},
            };
            if (auto vt = HttpClient::get().getViewerToken(); !vt.empty()) {
                fields.emplace_back("token", vt);
            }

            HttpClient::get().uploadMultipart(
                "/api/chat-images/upload",
                fields,
                "blob",
                "image.bin",
                encryptedBlob,
                "application/octet-stream",
                [this, blobCopy = encryptedBlob, key22 = std::move(key22),
                 expectedExt = prepared.ext, expectedW = prepared.width, expectedH = prepared.height,
                 cb = std::move(cb)](bool ok, std::string const& resp) mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    UploadResult res;
                    auto parsed = matjson::parse(resp);
                    if (ok && parsed.isOk() && parsed.unwrap().isObject()) {
                        auto const& obj = parsed.unwrap();
                        if (obj["success"].asBool().unwrapOr(false)) {
                            res.ref.id = obj["id"].asString().unwrapOr("");
                            res.ref.ext = obj["ext"].asString().unwrapOr(expectedExt);
                            res.ref.width = static_cast<int>(obj["width"].asInt().unwrapOr(expectedW));
                            res.ref.height = static_cast<int>(obj["height"].asInt().unwrapOr(expectedH));
                            res.ref.key = key22;
                            res.ref.sig = obj["sig"].asString().unwrapOr("");
                            res.token = formatChatImageToken(res.ref);
                            res.ok = !res.token.empty();
                            if (res.ok) {
                                writeDiskCacheAsync(res.ref.id, std::move(blobCopy));
                                cb(std::move(res));
                                return;
                            }
                            res.error = "Invalid server token response";
                            cb(std::move(res));
                            return;
                        }
                    }
                    res.needsVerification = paimon::community::needsVerification(resp);
                    res.error = paimon::community::describeError(resp);
                    cb(std::move(res));
                }
            );
        });
    });
}

LoadedChatImage const* ChatImageService::getCached(std::string const& ramKey) const {
    auto it = m_lruIndex.find(ramKey);
    if (it == m_lruIndex.end()) return nullptr;
    return &it->second->second;
}

void ChatImageService::putRamCache(std::string const& ramKey, LoadedChatImage const& image) {
    auto it = m_lruIndex.find(ramKey);
    if (it != m_lruIndex.end()) {
        it->second->second = image;
        m_lru.splice(m_lru.begin(), m_lru, it->second);
        return;
    }
    while (m_lru.size() >= kMaxRamEntries) {
        auto last = std::prev(m_lru.end());
        m_lruIndex.erase(last->first);
        m_lru.pop_back();
    }
    m_lru.emplace_front(ramKey, image);
    m_lruIndex[ramKey] = m_lru.begin();
}

void ChatImageService::notifyPending(std::string const& ramKey, LoadedChatImage const& image) {
    if (image.ok) {
        putRamCache(ramKey, image);
    }
    auto it = m_pending.find(ramKey);
    if (it == m_pending.end()) return;
    auto callbacks = std::move(it->second);
    m_pending.erase(it);
    for (auto& cb : callbacks) {
        if (cb) cb(image);
    }
}

void ChatImageService::loadImage(ChatImageRef const& ref, int threadID, LoadCallback cb) {
    if (threadID <= 0 || ref.id.size() != 24 || ref.ext.empty()
        || ref.key.size() != crypto::kKeyTextLen || ref.sig.size() != 16) {
        LoadedChatImage err;
        err.error = "Invalid image reference";
        if (cb) cb(std::move(err));
        return;
    }

    auto const ramKey = makeRamKey(ref, threadID);
    if (auto it = m_lruIndex.find(ramKey); it != m_lruIndex.end()) {
        m_lru.splice(m_lru.begin(), m_lru, it->second);
        if (cb) cb(it->second->second);
        return;
    }

    bool const alreadyInFlight = m_pending.contains(ramKey);
    m_pending[ramKey].push_back(std::move(cb));
    if (alreadyInFlight) return;

    auto path = diskPathForId(ref.id);
    enqueuePoolJob([this, ramKey, ref, threadID, path]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        std::vector<uint8_t> diskBlob;
        std::string ignoreErr;
        if (readFileBytes(path, kMaxGifUploadBytes + 1024u, diskBlob, ignoreErr)) {
            std::vector<uint8_t> plaintext;
            if (crypto::openChatImage(diskBlob, ref.ext, ref.width, ref.height, threadID, ref.key, plaintext)
                && paimon::format::detect(plaintext.data(), plaintext.size()) != paimon::format::ImageFormat::Unknown) {
                decodeAndCachePlaintext(ramKey, ref, std::move(plaintext));
                return;
            }
            // Fail closed on decryption mismatch or corruption; do not fall back or leak data.
            dispatchOnMain([this, ramKey]() {
                LoadedChatImage err;
                err.error = "Failed to decrypt image";
                notifyPending(ramKey, err);
            });
            return;
        }
        dispatchOnMain([this, ramKey, ref, threadID]() {
            downloadFromServer(ramKey, ref, threadID);
        });
    });
}

void ChatImageService::downloadFromServer(std::string const& ramKey, ChatImageRef const& ref, int threadID) {
    auto identity = paimon::community::currentIdentity();
    if (!identity || !paimon::community::hasAccountProof()) {
        LoadedChatImage err;
        err.needsVerification = true;
        err.error = "Account verification required";
        notifyPending(ramKey, err);
        return;
    }

    std::string const endpoint = "/api/chat-images/blob/" + ref.id
        + "?threadID=" + std::to_string(threadID)
        + "&sig=" + ref.sig
        + "&username=" + HttpClient::encodeQueryParam(identity->username)
        + "&accountID=" + std::to_string(identity->accountID);

    HttpClient::get().getRawBinaryWithAuth(
        endpoint,
        [this, ramKey, ref, threadID](bool ok, std::vector<uint8_t> const& data, int statusCode) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (statusCode == 403) {
                LoadedChatImage err;
                err.needsVerification = true;
                err.error = "Account verification required";
                notifyPending(ramKey, err);
                return;
            }
            if (!ok || data.size() < crypto::kEnvelopeOverhead + 12u) {
                LoadedChatImage err;
                err.error = "Failed to download image";
                notifyPending(ramKey, err);
                return;
            }

            std::vector<uint8_t> blobCopy = data;
            enqueuePoolJob([this, ramKey, ref, threadID, blob = std::move(blobCopy)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                std::vector<uint8_t> plaintext;
                if (!crypto::openChatImage(blob, ref.ext, ref.width, ref.height, threadID, ref.key, plaintext)
                    || paimon::format::detect(plaintext.data(), plaintext.size()) == paimon::format::ImageFormat::Unknown) {
                    dispatchOnMain([this, ramKey]() {
                        LoadedChatImage err;
                        err.error = "Failed to decrypt image";
                        notifyPending(ramKey, err);
                    });
                    return;
                }
                writeDiskCacheAsync(ref.id, std::move(blob));
                decodeAndCachePlaintext(ramKey, ref, std::move(plaintext));
            });
        }
    );
}

void ChatImageService::decodeAndCachePlaintext(
    std::string const& ramKey, ChatImageRef const& ref, std::vector<uint8_t> plaintext
) {
    auto fmt = paimon::format::detect(plaintext.data(), plaintext.size());
    if (fmt == paimon::format::ImageFormat::GIF) {
        int gw = 0;
        int gh = 0;
        if (!parseGifDimensions(plaintext.data(), plaintext.size(), gw, gh) ||
            gw > kMaxGifDimension || gh > kMaxGifDimension) {
            dispatchOnMain([this, ramKey]() {
                LoadedChatImage err;
                err.error = "Invalid or oversized GIF image";
                notifyPending(ramKey, err);
            });
            return;
        }
        dispatchOnMain([this, ramKey, bytes = std::move(plaintext), gw, gh]() mutable {
            LoadedChatImage img;
            img.ok = true;
            img.isGif = true;
            img.rawBytes = std::move(bytes);
            img.width = gw;
            img.height = gh;
            notifyPending(ramKey, img);
        });
        return;
    }

    if (fmt == paimon::format::ImageFormat::WebP) {
        dispatchOnMain([this, ramKey, ref, bytes = std::move(plaintext)]() mutable {
            finishWebpOnMain(ramKey, ref, std::move(bytes));
        });
        return;
    }

    int w = 0;
    int h = 0;
    auto decoded = ImageLoadHelper::decodeRGBAFromMemory(plaintext.data(), plaintext.size(), w, h);
    if (!decoded || w < 1 || h < 1) {
        dispatchOnMain([this, ramKey]() {
            LoadedChatImage err;
            err.error = "Failed to decode image";
            notifyPending(ramKey, err);
        });
        return;
    }

    std::vector<uint8_t> rgba(decoded.get(), decoded.get() + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    dispatchOnMain([this, ramKey, ref, bytes = std::move(plaintext), rgba = std::move(rgba), w, h]() mutable {
        finishDecodedStatic(ramKey, ref, std::move(bytes), std::move(rgba), w, h);
    });
}

void ChatImageService::finishDecodedStatic(
    std::string const& ramKey, ChatImageRef const&, std::vector<uint8_t> rawBytes,
    std::vector<uint8_t> rgba, int w, int h
) {
    auto loaded = ImageLoadHelper::createFromRGBA(rgba.data(), w, h, true);
    LoadedChatImage out;
    if (!loaded.success || !loaded.texture) {
        out.error = "Failed to upload texture";
        notifyPending(ramKey, out);
        return;
    }
    out.ok = true;
    out.isGif = false;
    out.texture = Ref<CCTexture2D>::adopt(loaded.texture);
    out.rawBytes = std::move(rawBytes);
    out.width = w;
    out.height = h;
    notifyPending(ramKey, out);
}

void ChatImageService::finishWebpOnMain(std::string const& ramKey, ChatImageRef const& ref, std::vector<uint8_t> rawBytes) {
    LoadedChatImage out;
    int probeW = 0;
    int probeH = 0;
    if (!paimon::format::probeWebpDimensions(rawBytes.data(), rawBytes.size(), probeW, probeH) ||
        probeW <= 0 || probeH <= 0 || probeW > kMaxStaticDimension || probeH > kMaxStaticDimension ||
        (ref.width > 0 && probeW != ref.width) || (ref.height > 0 && probeH != ref.height)) {
        out.error = "Invalid or oversized WebP image";
        notifyPending(ramKey, out);
        return;
    }
    CCImage img;
    if (!img.initWithImageData(rawBytes.data(), static_cast<int>(rawBytes.size()))) {
        out.error = "Failed to decode WebP image";
        notifyPending(ramKey, out);
        return;
    }
    auto tex = Ref<CCTexture2D>::adopt(new CCTexture2D());
    if (!tex || !tex->initWithImage(&img)) {
        out.error = "Failed to create WebP texture";
        notifyPending(ramKey, out);
        return;
    }
    out.ok = true;
    out.isGif = false;
    out.texture = tex;
    out.rawBytes = std::move(rawBytes);
    out.width = img.getWidth();
    out.height = img.getHeight();
    notifyPending(ramKey, out);
}

void ChatImageService::writeDiskCacheAsync(std::string const& imageId, std::vector<uint8_t> encryptedBlob) {
    auto dest = diskPathForId(imageId);
    enqueuePoolJob([this, dest = std::move(dest), bytes = std::move(encryptedBlob)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        std::lock_guard<std::mutex> lock(m_diskWriteMutex);
        bool ok = paimon::file::writeAtomically(dest, bytes);
        if (ok) {
            ++m_writesSincePrune;
            if (m_writesSincePrune >= 20) {
                m_writesSincePrune = 0;
                pruneDiskCacheLocked();
            }
        }
    });
}

void ChatImageService::pruneDiskCacheLocked() {
    auto dir = cacheDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;

    struct FileEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime;
        size_t bytes = 0;
    };
    std::vector<FileEntry> entries;
    size_t totalBytes = 0;
    auto const now = std::filesystem::file_time_type::clock::now();

    for (auto const& item : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!item.is_regular_file(ec)) continue;
        auto mtime = item.last_write_time(ec);
        auto fsize = item.file_size(ec);
        if (ec) continue;
        if (now - mtime > kMaxDiskCacheAge) {
            std::filesystem::remove(item.path(), ec);
            continue;
        }
        entries.push_back({item.path(), mtime, static_cast<size_t>(fsize)});
        totalBytes += static_cast<size_t>(fsize);
    }

    if (totalBytes <= kMaxDiskCacheBytes) return;
    std::sort(entries.begin(), entries.end(), [](FileEntry const& a, FileEntry const& b) {
        return a.mtime < b.mtime;
    });
    for (auto const& e : entries) {
        if (totalBytes <= kMaxDiskCacheBytes) break;
        if (std::filesystem::remove(e.path, ec)) {
            totalBytes -= std::min(totalBytes, e.bytes);
        }
    }
}

void ChatImageService::deleteImage(ChatImageRef const& ref, ActionCallback cb) {
    auto identity = paimon::community::currentIdentity();
    if (!identity || !paimon::community::hasAccountProof()) {
        if (cb) cb(false, "Verification required");
        return;
    }

    matjson::Value body = matjson::makeObject({
        {"username", identity->username},
        {"accountID", identity->accountID},
        {"id", ref.id}
    });
    if (auto vt = HttpClient::get().getViewerToken(); !vt.empty()) {
        body["token"] = vt;
    }

    HttpClient::get().postWithAuth(
        "/api/chat-images/delete",
        body.dump(),
        [this, id = ref.id, cb = std::move(cb)](bool ok, std::string const& resp) mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto parsed = matjson::parse(resp);
            bool const deleted = ok && parsed.isOk() && parsed.unwrap().isObject()
                && parsed.unwrap()["success"].asBool().unwrapOr(false);
            if (deleted) {
                evict(id);
                if (cb) cb(true, "");
            } else {
                if (cb) cb(false, paimon::community::describeError(resp));
            }
        }
    );
}

void ChatImageService::saveRawBytesToPath(std::vector<uint8_t> bytes, std::filesystem::path dest, ActionCallback cb) {
    enqueuePoolJob([bytes = std::move(bytes), dest = std::move(dest), cb = std::move(cb)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        bool ok = paimon::file::writeAtomically(dest, bytes);
        dispatchOnMain([ok, cb = std::move(cb)]() mutable {
            if (cb) cb(ok, ok ? std::string{} : std::string("Could not write file"));
        });
    });
}

void ChatImageService::decodeRawToRgbaAsync(std::vector<uint8_t> bytes, RgbaCallback cb) {
    if (bytes.size() < 12) {
        if (cb) cb(false, {}, 0, 0);
        return;
    }
    if (paimon::format::detect(bytes.data(), bytes.size()) == paimon::format::ImageFormat::WebP) {
        CCImage img;
        std::vector<uint8_t> rgba;
        int w = 0;
        int h = 0;
        bool const ok = img.initWithImageData(bytes.data(), static_cast<int>(bytes.size()))
            && ccImageToRgba(img, rgba, w, h);
        if (cb) cb(ok, std::move(rgba), w, h);
        return;
    }
    enqueuePoolJob([bytes = std::move(bytes), cb = std::move(cb)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        int w = 0;
        int h = 0;
        auto decoded = ImageLoadHelper::decodeRGBAFromMemory(bytes.data(), bytes.size(), w, h);
        if (!decoded || w < 1 || h < 1) {
            dispatchOnMain([cb = std::move(cb)]() mutable {
                if (cb) cb(false, {}, 0, 0);
            });
            return;
        }
        std::vector<uint8_t> rgba(decoded.get(), decoded.get() + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
        dispatchOnMain([cb = std::move(cb), rgba = std::move(rgba), w, h]() mutable {
            if (cb) cb(true, std::move(rgba), w, h);
        });
    });
}

void ChatImageService::evict(std::string const& imageId) {
    std::string const needle = ":" + imageId + ":";
    for (auto it = m_lru.begin(); it != m_lru.end();) {
        if (it->first == imageId || it->first.find(needle) != std::string::npos) {
            m_lruIndex.erase(it->first);
            it = m_lru.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->first == imageId || it->first.find(needle) != std::string::npos) {
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    auto path = diskPathForId(imageId);
    enqueuePoolJob([this, path = std::move(path)]() {
        std::lock_guard<std::mutex> lock(m_diskWriteMutex);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    });
}

void ChatImageService::clearTextureCache() {
    m_lru.clear();
    m_lruIndex.clear();
    m_pending.clear();
}

} // namespace paimon::chatimg
