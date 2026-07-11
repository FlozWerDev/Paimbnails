#include "FusionAsset.hpp"

#include "../../../utils/GIFDecoder.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

std::string lowerExt(std::filesystem::path const& path) {
    auto e = path.extension().string();
    for (char& c : e) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

std::string lowerExtHint(std::string_view hint) {
    std::string e(hint);
    for (char& c : e) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

bool looksLikeGif(std::span<std::uint8_t const> bytes) {
    return GIFDecoder::isGIF(bytes.data(), bytes.size());
}

int clampDelay(int ms) {
    if (ms <= 0) return 100;
    return std::clamp(ms, FusionAssetLoader::kMinFrameDelay,
                      FusionAssetLoader::kMaxFrameDelay);
}

}  // namespace

ImageBuffer FusionAssetLoader::maybeDownscale(ImageBuffer img) {
    if (img.empty()) return img;
    int w = img.width();
    int h = img.height();
    int maxSide = std::max(w, h);
    if (maxSide <= kMaxSide) return img;
    float scale = static_cast<float>(kMaxSide) / static_cast<float>(maxSide);
    int nw = std::max(1, static_cast<int>(std::lround(w * scale)));
    int nh = std::max(1, static_cast<int>(std::lround(h * scale)));
    return img.resizedBilinear(nw, nh);
}

std::shared_ptr<FusionAsset> FusionAssetLoader::fromStatic(ImageBuffer img,
                                                           std::string ext) {
    auto asset = std::make_shared<FusionAsset>();
    FusionFrame fr;
    fr.image = maybeDownscale(std::move(img));
    fr.delayMs = 100;
    asset->frames.push_back(std::move(fr));
    asset->animated = false;
    asset->sourceExt = std::move(ext);
    return asset;
}

geode::Result<std::shared_ptr<FusionAsset>> FusionAssetLoader::loadFromFile(
    std::filesystem::path const& path) {
    auto bytes = file::readBinary(path);
    if (!bytes) {
        return Err("FusionAsset: cannot read {}: {}",
            geode::utils::string::pathToString(path), bytes.unwrapErr());
    }
    return loadFromMemory(
        std::span<std::uint8_t const>(bytes.unwrap().data(), bytes.unwrap().size()),
        lowerExt(path));
}

geode::Result<std::shared_ptr<FusionAsset>> FusionAssetLoader::loadFromMemory(
    std::span<std::uint8_t const> bytes,
    std::string_view extHint) {
    if (bytes.empty()) {
        return Err("FusionAsset: empty input");
    }

    std::string ext = lowerExtHint(extHint);
    bool isGif = (ext == ".gif") || looksLikeGif(bytes);

    if (isGif) {
        auto gif = GIFDecoder::decode(bytes.data(), bytes.size(), kMaxFrames);
        if (gif.frames.empty() || gif.width <= 0 || gif.height <= 0) {
            return Err("FusionAsset: GIF decode failed");
        }

        auto asset = std::make_shared<FusionAsset>();
        asset->sourceExt = ".gif";
        asset->animated = gif.frames.size() > 1;
        asset->frames.reserve(gif.frames.size());

        // GIFDecoder already composites disposal into full-canvas frames.
        for (auto const& gf : gif.frames) {
            std::size_t need = static_cast<std::size_t>(gif.width) * gif.height * 4;
            if (gf.pixels.size() < need || gif.width <= 0 || gif.height <= 0) continue;

            ImageBuffer canvas(gif.width, gif.height, gf.pixels.data());
            FusionFrame fr;
            fr.image = maybeDownscale(std::move(canvas));
            fr.delayMs = clampDelay(gf.delayMs);
            if (!fr.image.empty()) {
                asset->frames.push_back(std::move(fr));
            }
            if (static_cast<int>(asset->frames.size()) >= kMaxFrames) break;
        }

        if (asset->frames.empty()) {
            return Err("FusionAsset: GIF had no usable frames");
        }
        asset->animated = asset->frames.size() > 1;
        return Ok(std::move(asset));
    }

    // Static path: stb via ImageBuffer (PNG/JPG/WebP/BMP/TGA/…).
    auto imgRes = ImageBuffer::loadFromMemory(bytes);
    if (!imgRes) {
        return Err("FusionAsset: {}", imgRes.unwrapErr());
    }
    if (ext.empty()) ext = ".png";
    return Ok(fromStatic(std::move(imgRes).unwrap(), std::move(ext)));
}

geode::Result<ImageBuffer> FusionAssetLoader::loadStaticFrame(
    std::filesystem::path const& path) {
    auto asset = loadFromFile(path);
    if (!asset) return Err(asset.unwrapErr());
    auto const& a = *asset.unwrap();
    if (a.empty()) return Err("FusionAsset: empty after load");
    return Ok(a.frames.front().image);
}

}  // namespace paimon::texture_studio
