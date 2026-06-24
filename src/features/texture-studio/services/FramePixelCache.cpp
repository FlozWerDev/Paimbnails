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

    std::string sourceKey = geode::utils::string::pathToString(plistPath) + "\n" +
                            geode::utils::string::pathToString(pngPath);
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
        m_frames.clear();
        m_cachedKey.clear();
        m_frameCache.clear();
        m_frameOrder.clear();
        m_frameCacheBytes = 0;

        GEODE_UNWRAP_INTO(auto parsed, PlistParser::parseFile(plistPath));
        GEODE_UNWRAP_INTO(auto atlas, ImageBuffer::loadFromFile(pngPath));

        m_frames = std::move(parsed.frames);
        m_atlas = std::move(atlas);
        m_cachedKey = sourceKey;
        m_cachedPlistMtime = plistMtime;
        m_cachedPngMtime = pngMtime;
        m_haveMtimes = mtimesValid;
    }

    if (auto it = m_frameCache.find(frameKey); it != m_frameCache.end()) {
        ++m_hits;
        auto infoIt = std::find_if(m_frames.begin(), m_frames.end(),
            [&frameName](SpriteFrameInfo const& f) { return f.name == frameName; });
        if (infoIt != m_frames.end()) return Ok(FrameData{it->second, *infoIt});
    }

    ++m_misses;
    if ((m_hits + m_misses) % 256 == 0) {
        auto total = m_hits + m_misses;
        log::debug("[texture-studio] frame cache: {} hits / {} requests ({:.1f}%)",
            m_hits, total, total ? (100.0 * m_hits / total) : 0.0);
    }

    for (auto const& f : m_frames) {
        if (f.name != frameName) continue;
        auto pixels = SpritesheetReader::extractFrame(m_atlas, f);
        if (pixels.empty()) {
            return Err("frame '{}' extracted empty (bad rect?)", frameName);
        }

        std::size_t bytes = pixels.byteSize();
        m_frameCache.emplace(frameKey, pixels);
        m_frameOrder.push_back(frameKey);
        m_frameCacheBytes += bytes;
        while (m_frameCacheBytes > m_byteBudget && !m_frameOrder.empty()) {
            auto const oldKey = m_frameOrder.front();
            m_frameOrder.pop_front();
            if (auto oldIt = m_frameCache.find(oldKey); oldIt != m_frameCache.end()) {
                m_frameCacheBytes -= oldIt->second.byteSize();
                m_frameCache.erase(oldIt);
            }
        }

        return Ok(FrameData{std::move(pixels), f});
    }
    return Err("frame '{}' not found in {}", frameName, geode::utils::string::pathToString(plistPath));
}

void FramePixelCache::setByteBudget(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byteBudget = std::max(bytes, kFrameCacheByteFloor);
    // Enforce the new (possibly smaller) budget right away.
    while (m_frameCacheBytes > m_byteBudget && !m_frameOrder.empty()) {
        auto const oldKey = m_frameOrder.front();
        m_frameOrder.pop_front();
        if (auto it = m_frameCache.find(oldKey); it != m_frameCache.end()) {
            m_frameCacheBytes -= it->second.byteSize();
            m_frameCache.erase(it);
        }
    }
    log::debug("[texture-studio] frame cache budget set to {} KB", m_byteBudget / 1024);
}

std::size_t FramePixelCache::byteBudget() const {
    return m_byteBudget;
}

void FramePixelCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cachedKey.clear();
    m_haveMtimes = false;
    m_atlas = ImageBuffer();
    m_frames.clear();
    m_frames.shrink_to_fit();
    m_frameCache.clear();
    m_frameOrder.clear();
    m_frameCacheBytes = 0;
    m_hits = 0;
    m_misses = 0;
}

}  // namespace paimon::texture_studio
