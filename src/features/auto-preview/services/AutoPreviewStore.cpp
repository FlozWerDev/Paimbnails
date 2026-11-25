#include "../../../utils/FilesystemPath.hpp"
#include "AutoPreviewStore.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <fstream>
#include <vector>
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include <cstring>

using namespace geode::prelude;

namespace {
#pragma pack(push, 1)
struct RGBHeader {
    uint32_t width;
    uint32_t height;
};
#pragma pack(pop)
constexpr uint64_t kMaxRgbPixels = 16ull * 1024 * 1024;
} // namespace

namespace paimon::autopreview {

AutoPreviewStore& AutoPreviewStore::get() {
    static auto* inst = new AutoPreviewStore();
    return *inst;
}

std::filesystem::path AutoPreviewStore::dir() const {
    // pure path math (no stat/mkdir syscalls): writeAtomically() creates
    // parents on save; readers tolerate a missing directory.
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "auto-previews";
}

std::filesystem::path AutoPreviewStore::pathFor(int32_t levelID) const {
    return dir() / (std::to_string(levelID) + ".rgb");
}

void AutoPreviewStore::ensureScanned() {
    if (m_scanned) return;
    m_scanned = true;
    auto d = dir();
    std::error_code ec;
    if (!std::filesystem::exists(d, ec)) return;
    for (auto const& entry : std::filesystem::directory_iterator(d, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".rgb") continue;
        auto stem = paimon::paths::toUtf8(entry.path().stem());
        if (auto res = geode::utils::numFromString<int32_t>(stem); res.isOk()) {
            m_present.insert(res.unwrap());
        }
    }
    log::info("[AutoPreview] store scan found {} generated previews", m_present.size());
}

bool AutoPreviewStore::has(int32_t levelID) {
    if (levelID <= 0) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    ensureScanned();
    return m_present.count(levelID) > 0;
}

bool AutoPreviewStore::save(int32_t levelID, uint8_t const* rgba, uint32_t width, uint32_t height) {
    if (levelID <= 0 || !rgba || width == 0 || height == 0) return false;
    if (width > 16384 || height > 16384 ||
        static_cast<uint64_t>(width) * height > kMaxRgbPixels) return false;

    size_t const pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> encoded(sizeof(RGBHeader) + pixelCount * 3);
    RGBHeader head{width, height};
    std::memcpy(encoded.data(), &head, sizeof(head));
    auto* rgb = encoded.data() + sizeof(head);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }

    // serialize publication with clearAll and the presence index. The shared
    // writer uses a unique temporary file, including for simultaneous saves.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!paimon::file::writeAtomically(pathFor(levelID),
                std::span<uint8_t const>(encoded.data(), encoded.size()))) {
            log::error("[AutoPreview] atomic write failed for level {}", levelID);
            return false;
        }
        ensureScanned();
        m_present.insert(levelID);
        // workers only mark IDs; the next main-thread lookup releases the GL texture.
        m_staleTextures.insert(levelID);
    }
    log::info("[AutoPreview] saved generated preview for level {} ({}x{})", levelID, width, height);
    return true;
}

cocos2d::CCTexture2D* AutoPreviewStore::cachedTexture(int32_t levelID) {
    bool stale;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        stale = m_staleTextures.erase(levelID) != 0;
    }
    std::lock_guard<std::mutex> lock(m_texMutex);
    auto it = m_texCache.find(levelID);
    if (it == m_texCache.end()) return nullptr;
    if (stale) {
        it->second->release();
        m_texCache.erase(it);
        std::erase(m_texLru, levelID);
        return nullptr;
    }
    auto lruIt = std::find(m_texLru.begin(), m_texLru.end(), levelID);
    if (lruIt != m_texLru.end()) m_texLru.erase(lruIt);
    m_texLru.push_back(levelID);
    return it->second;
}

void AutoPreviewStore::cacheTexture(int32_t levelID, cocos2d::CCTexture2D* tex) {
    if (!tex) return;
    std::lock_guard<std::mutex> lock(m_texMutex);
    auto it = m_texCache.find(levelID);
    if (it != m_texCache.end()) {
        if (it->second == tex) return;
        it->second->release();
        m_texCache.erase(it);
        auto lruIt = std::find(m_texLru.begin(), m_texLru.end(), levelID);
        if (lruIt != m_texLru.end()) m_texLru.erase(lruIt);
    }
    tex->retain();
    m_texCache[levelID] = tex;
    m_texLru.push_back(levelID);
    while (m_texLru.size() > MAX_TEX_CACHE) {
        int32_t evict = m_texLru.front();
        m_texLru.pop_front();
        auto eIt = m_texCache.find(evict);
        if (eIt != m_texCache.end()) {
            eIt->second->release();
            m_texCache.erase(eIt);
        }
    }
}

cocos2d::CCTexture2D* AutoPreviewStore::loadTexture(int32_t levelID) {
    if (levelID <= 0) return nullptr;
    if (auto* cached = cachedTexture(levelID)) return cached;
    if (!has(levelID)) return nullptr;

    std::ifstream in(pathFor(levelID), std::ios::binary);
    if (!in) return nullptr;

    RGBHeader head{};
    in.read(reinterpret_cast<char*>(&head), sizeof(head));
    uint64_t const pixels = static_cast<uint64_t>(head.width) * head.height;
    if (!in || head.width == 0 || head.height == 0 || head.width > 16384 || head.height > 16384 ||
        pixels > kMaxRgbPixels) {
        return nullptr;
    }

    size_t const pixelCount = static_cast<size_t>(pixels);
    auto const start = in.tellg();
    in.seekg(0, std::ios::end);
    auto const remaining = in.tellg() - start;
    in.seekg(start);
    if (!in || remaining < static_cast<std::streamoff>(pixelCount * 3)) return nullptr;

    auto rgb = std::make_unique<uint8_t[]>(pixelCount * 3);
    in.read(reinterpret_cast<char*>(rgb.get()), static_cast<std::streamsize>(pixelCount * 3));
    if (!in) return nullptr;

    auto rgba = std::make_unique<uint8_t[]>(pixelCount * 4);
    ImageConverter::rgbToRgbaFast(rgb.get(), rgba.get(), pixelCount);

    // previews can exceed the shared 4096 cap, so the helper limit follows
    // this store's own 16384 bound (validated above).
    auto img = ImageLoadHelper::createFromRGBA(
        rgba.get(), static_cast<int>(head.width), static_cast<int>(head.height), false, 16384);
    if (!img.success || !img.texture) return nullptr;
    img.texture->autorelease();
    cacheTexture(levelID, img.texture);
    return img.texture;
}

void AutoPreviewStore::clearRamCache() {
    std::lock_guard<std::mutex> lock(m_texMutex);
    for (auto& [id, tex] : m_texCache) if (tex) tex->release();
    m_texCache.clear();
    m_texLru.clear();
}

void AutoPreviewStore::clearAll() {
    {
        std::lock_guard<std::mutex> lock(m_texMutex);
        for (auto& [id, tex] : m_texCache) if (tex) tex->release();
        m_texCache.clear();
        m_texLru.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::error_code ec;
        auto d = dir();
        if (std::filesystem::exists(d, ec)) {
            for (auto const& entry : std::filesystem::directory_iterator(d, ec)) {
                if (ec) break;
                if (entry.is_regular_file() && entry.path().extension() == ".rgb") {
                    std::error_code rmEc;
                    std::filesystem::remove(entry.path(), rmEc);
                }
            }
        }
        m_present.clear();
        m_staleTextures.clear();
        m_attempted.clear();
        m_scanned = true;
        m_generatedThisSession = 0;
    }
    log::info("[AutoPreview] cleared all generated previews");
}

bool AutoPreviewStore::wasAttempted(int32_t levelID) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_attempted.count(levelID) > 0;
}

void AutoPreviewStore::markAttempted(int32_t levelID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_attempted.insert(levelID);
}

} // namespace paimon::autopreview
