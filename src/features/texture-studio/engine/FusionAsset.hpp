#pragma once

#include "../data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::texture_studio {

// one decoded animation frame. `delayms` is the display duration.
struct FusionFrame {
    ImageBuffer image;
    int delayMs = 100;
};

// immutable shared fusion source (static or gif): owned rgba frames, read-only after build, thread-sharable.
struct FusionAsset {
    std::vector<FusionFrame> frames;
    bool animated = false;
    std::string sourceExt;  // ".png", ".gif", ...

    bool empty() const {
        return frames.empty() || frames.front().image.empty();
    }
    int width() const {
        return empty() ? 0 : frames.front().image.width();
    }
    int height() const {
        return empty() ? 0 : frames.front().image.height();
    }
    ImageBuffer const& frameAt(std::size_t i) const {
        return frames[i % frames.size()].image;
    }
    int delayAt(std::size_t i) const {
        int d = frames[i % frames.size()].delayMs;
        return d > 0 ? d : 100;
    }
    std::size_t frameCount() const { return frames.size(); }
};

class FusionAssetLoader final {
public:
    // hard caps against gif ooms; oversized frames downscale and still work.
    static constexpr int kMaxFrames     = 48;
    static constexpr int kMaxSide       = 512;
    static constexpr int kMinFrameDelay = 20;   // ms
    static constexpr int kMaxFrameDelay = 2000; // ms

    // file decode: images via imagebuffer, multi-frame gifs via gifdecoder; 1+ frames on success.
    static geode::Result<std::shared_ptr<FusionAsset>> loadFromFile(
        std::filesystem::path const& path);

    // decode from already-loaded bytes (used after a slot-local copy).
    static geode::Result<std::shared_ptr<FusionAsset>> loadFromMemory(
        std::span<std::uint8_t const> bytes,
        std::string_view extHint = {});

    // first frame only — cheap path for export / thumbnails.
    static geode::Result<ImageBuffer> loadStaticFrame(
        std::filesystem::path const& path);

private:
    FusionAssetLoader() = delete;

    static ImageBuffer maybeDownscale(ImageBuffer img);
    static std::shared_ptr<FusionAsset> fromStatic(ImageBuffer img, std::string ext);
};

}  // namespace paimon::texture_studio
