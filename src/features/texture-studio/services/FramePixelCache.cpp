#include "../../../utils/FilesystemPath.hpp"
#include "FramePixelCache.hpp"
#include <Geode/utils/string.hpp>

#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace paimon::texture_studio {

FramePixelCache& FramePixelCache::get() {
    static FramePixelCache instance;
    return instance;
}

geode::Result<ImageBuffer> FramePixelCache::framePixels(
    std::filesystem::path const& plistPath,
    std::filesystem::path const& pngPath,
    std::string const& frameName) {

    GEODE_UNWRAP_INTO(auto data, frameData(plistPath, pngPath, frameName));
    return Ok(std::move(data.pixels));
}

geode::Result<FramePixelCache::FrameData> FramePixelCache::frameData(
    std::filesystem::path const& plistPath,
    std::filesystem::path const& pngPath,
    std::string const& frameName) {

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string sourceKey = paimon::paths::toUtf8(plistPath) + "\n" +
                            paimon::paths::toUtf8(pngPath);
    std::string frameKey = sourceKey + "\n" + frameName;

    std::error_code plistEc;
    std::error_code pngEc;
    auto plistMtime = std::filesystem::last_write_time(plistPath, plistEc);
    auto pngMtime = std::filesystem::last_write_time(pngPath, pngEc);
    bool mtimesValid = !plistEc && !pngEc;
    bool sourceChanged = m_cachedKey != sourceKey || m_atlas.empty();
    if (!sourceChanged && mtimesValid != m_haveMtimes) {
        sourceChanged = true;
    } else if (!sourceChanged && mtimesValid && m_haveMtimes) {
        sourceChanged = plistMtime != m_cachedPlistMtime || pngMtime != m_cachedPngMtime;
    }

    if (sourceChanged) {
        m_atlas = ImageBuffer();
        setFrames({});
        m_cachedKey.clear();
        m_frameCache.clear();
        m_frameOrder.clear();
        m_frameCacheBytes = 0;

        GEODE_UNWRAP_INTO(auto parsed, PlistParser::parseFile(plistPath));
        GEODE_UNWRAP_INTO(auto atlas, ImageBuffer::loadFromFile(pngPath));

        setFrames(std::move(parsed.frames));
        m_atlas = std::move(atlas);
        m_cachedKey = sourceKey;
        m_cachedPlistMtime = plistMtime;
        m_cachedPngMtime = pngMtime;
        m_haveMtimes = mtimesValid;
    }

    if (auto it = m_frameCache.find(frameKey); it != m_frameCache.end()) {
        ++m_hits;
        return Ok(it->second);
    }

    ++m_misses;
    if ((m_hits + m_misses) % 256 == 0) {
        auto total = m_hits + m_misses;
        log::debug("[texture-studio] frame cache: {} hits / {} requests ({:.1f}%)",
            m_hits, total, total ? (100.0 * m_hits / total) : 0.0);
    }

    auto idxIt = m_frameIndex.find(frameName);
    if (idxIt == m_frameIndex.end() || idxIt->second >= m_frames.size()) {
        return Err("frame '{}' not found in {}", frameName, paimon::paths::toUtf8(plistPath));
    }
    {
        auto const& f = m_frames[idxIt->second];
        auto pixels = SpritesheetReader::extractFrame(m_atlas, f);
        if (pixels.empty()) {
            return Err("frame '{}' extracted empty (bad rect?)", frameName);
        }

        std::size_t bytes = pixels.byteSize();
        m_frameCache.emplace(frameKey, FrameData{pixels, f});
        m_frameOrder.push_back(frameKey);
        m_frameCacheBytes += bytes;
        evictWhileOverBudget();

        return Ok(FrameData{std::move(pixels), f});
    }
}

void FramePixelCache::setByteBudget(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byteBudget = std::max(bytes, kFrameCacheByteFloor);
    evictWhileOverBudget();
    log::debug("[texture-studio] frame cache budget set to {} KB", m_byteBudget / 1024);
}

std::size_t FramePixelCache::byteBudget() const {
    std::lock_guard lock(m_mutex);
    return m_byteBudget;
}

void FramePixelCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cachedKey.clear();
    m_haveMtimes = false;
    m_atlas = ImageBuffer();
    setFrames({});
    m_frames.shrink_to_fit();
    m_frameCache.clear();
    m_frameOrder.clear();
    m_frameCacheBytes = 0;
    m_hits = 0;
    m_misses = 0;
}

void FramePixelCache::evictWhileOverBudget() {
    while (m_frameCacheBytes > m_byteBudget && !m_frameOrder.empty()) {
        auto const oldKey = m_frameOrder.front();
        m_frameOrder.pop_front();
        if (auto oldIt = m_frameCache.find(oldKey); oldIt != m_frameCache.end()) {
            m_frameCacheBytes -= oldIt->second.pixels.byteSize();
            m_frameCache.erase(oldIt);
        }
    }
}

void FramePixelCache::setFrames(std::vector<SpriteFrameInfo> frames) {
    m_frames = std::move(frames);
    m_frameIndex.clear();
    for (std::size_t i = 0; i < m_frames.size(); ++i) {
        m_frameIndex.emplace(m_frames[i].name, i);
    }
}

}  // namespace paimon::texture_studio
