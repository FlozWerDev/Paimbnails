#pragma once

#include "../data/ImageBuffer.hpp"
#include "../data/SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::texture_studio {

class FramePixelCache final {
public:
    struct FrameData {
        ImageBuffer pixels;
        SpriteFrameInfo info;
    };

    static FramePixelCache& get();

    geode::Result<FrameData> frameData(std::filesystem::path const& plistPath,
                                      std::filesystem::path const& pngPath,
                                      std::string const& frameName);

    geode::Result<ImageBuffer> framePixels(std::filesystem::path const& plistPath,
                                           std::filesystem::path const& pngPath,
                                           std::string const& frameName);

    // clamped to a 1 mb floor so a misconfiguration can't disable caching.
    void setByteBudget(std::size_t bytes);
    std::size_t byteBudget() const;

    void clear();

private:
    FramePixelCache() = default;

    // callers already hold m_mutex.
    void evictWhileOverBudget();
    void setFrames(std::vector<SpriteFrameInfo> frames);

    static constexpr std::size_t kFrameCacheByteBudget = 16u * 1024u * 1024u;
    static constexpr std::size_t kFrameCacheByteFloor  = 1u * 1024u * 1024u;

    mutable std::mutex m_mutex;
    std::string m_cachedKey;
    std::filesystem::file_time_type m_cachedPlistMtime{};
    std::filesystem::file_time_type m_cachedPngMtime{};
    bool m_haveMtimes = false;
    ImageBuffer m_atlas;
    std::vector<SpriteFrameInfo> m_frames;
    std::unordered_map<std::string, std::size_t> m_frameIndex;

    std::unordered_map<std::string, FrameData> m_frameCache;
    std::deque<std::string>                      m_frameOrder;
    std::size_t                                  m_frameCacheBytes = 0;
    std::size_t   m_byteBudget = kFrameCacheByteBudget;
    std::uint64_t m_hits = 0;
    std::uint64_t m_misses = 0;
};

}  // namespace paimon::texture_studio
