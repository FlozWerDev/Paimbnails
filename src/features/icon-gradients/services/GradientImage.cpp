#include "GradientImage.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <list>
#include <map>

using namespace geode::prelude;
using namespace paimon::icon_gradients;

namespace {
constexpr int kTile = 256;
constexpr size_t kTileCacheEntries = 32; // At most 8 MiB of resized source pixels.
constexpr size_t kAtlasCacheBytes = 16 * 1024 * 1024;
using Pixels = std::vector<unsigned char>;

struct TileEntry {
    std::string path;
    std::shared_ptr<Pixels> pixels;
};

// Decode and resize a source once even when it participates in several atlases.
std::shared_ptr<Pixels> loadTile(std::string const& path, int& decodes) {
    static std::list<TileEntry> cache;
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->path != path) continue;
        auto pixels = it->pixels;
        cache.splice(cache.begin(), cache, it);
        return pixels;
    }
    ++decodes;
    auto decode = [&]() -> std::shared_ptr<Pixels> {
        auto bytesResult = utils::file::readBinary(paimon::assets::pathFromUtf8(path));
        if (bytesResult.isErr()) return nullptr;
        auto bytes = std::move(bytesResult).unwrap();
        if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return nullptr;
        CCImage image;
        if (!image.initWithImageData(bytes.data(), static_cast<int>(bytes.size())) ||
            !image.getData() || !image.getWidth() || !image.getHeight() || image.getBitsPerComponent() != 8) return nullptr;
        int channels = image.hasAlpha() ? 4 : 3;
        int sourceWidth = image.getWidth(), sourceHeight = image.getHeight();
        auto source = image.getData();
        bool premultiplied = image.isPremultipliedAlpha();
        auto sample = [&](int x, int y, int channel) -> float {
            auto pixel = source + (static_cast<size_t>(y) * sourceWidth + x) * channels;
            float alpha = channels == 4 ? pixel[3] : 255.f;
            if (channel == 3) return alpha;
            return pixel[channel] * (premultiplied ? 1.f : alpha / 255.f);
        };
        auto pixels = std::make_shared<Pixels>(kTile * kTile * 4);
        for (int y = 0; y < kTile; ++y) {
            float sy = std::clamp((y + 0.5f) * sourceHeight / kTile - 0.5f, 0.f, float(sourceHeight - 1));
            int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, sourceHeight - 1);
            for (int x = 0; x < kTile; ++x) {
                float sx = std::clamp((x + 0.5f) * sourceWidth / kTile - 0.5f, 0.f, float(sourceWidth - 1));
                int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, sourceWidth - 1);
                auto dest = (y * kTile + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    float top = std::lerp(sample(x0, y0, c), sample(x1, y0, c), sx - x0);
                    float bottom = std::lerp(sample(x0, y1, c), sample(x1, y1, c), sx - x0);
                    (*pixels)[dest + c] = static_cast<unsigned char>(std::clamp(std::lerp(top, bottom, sy - y0), 0.f, 255.f));
                }
            }
        }
        return pixels;
    };
    auto pixels = decode();
    cache.push_front({path, pixels});
    if (cache.size() > kTileCacheEntries) cache.pop_back();
    return pixels;
}

struct AtlasCache {
    // Weak index also finds atlases still owned by sprites after LRU eviction.
    std::map<std::vector<std::string>, std::weak_ptr<GradientImageAtlas>> index;
    std::list<std::shared_ptr<GradientImageAtlas>> recent;
    size_t bytes = 0;

    void touch(std::shared_ptr<GradientImageAtlas> const& atlas) {
        auto found = std::find(recent.begin(), recent.end(), atlas);
        if (found != recent.end()) {
            recent.splice(recent.begin(), recent, found);
            return;
        }
        recent.push_front(atlas);
        bytes += atlas->textureBytes;
        while (bytes > kAtlasCacheBytes || recent.size() > 32) {
            bytes -= recent.back()->textureBytes;
            recent.pop_back();
        }
    }
};
}

std::shared_ptr<GradientImageAtlas> paimon::icon_gradients::getGradientImageAtlas(std::vector<SimplePoint> const& points) {
    std::vector<std::string> paths;
    for (auto const& point : points) {
        if (!point.imagePath.empty()) paths.push_back(point.imagePath);
    }
    if (paths.empty()) return nullptr;
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (paths.size() > 24) paths.resize(24);

    // Keep recently used atlases across scene changes; weak-only ownership
    // used to trigger synchronous file decoding/upload when sprites disappeared.
    static AtlasCache cache;
    if (auto found = cache.index.find(paths); found != cache.index.end()) {
        if (auto atlas = found->second.lock()) {
            cache.touch(atlas);
            return atlas;
        }
    }
    std::erase_if(cache.index, [](auto const& entry) { return entry.second.expired(); });
    auto started = std::chrono::steady_clock::now();
    int decodes = 0;
    auto atlas = std::make_shared<GradientImageAtlas>();
    std::vector<std::shared_ptr<Pixels>> tiles;
    for (auto const& path : paths) {
        if (auto tile = loadTile(path, decodes)) {
            atlas->slots.emplace(path, static_cast<int>(tiles.size()));
            tiles.push_back(std::move(tile));
        }
    }
    if (!tiles.empty()) {
        atlas->columns = std::min(4, static_cast<int>(tiles.size()));
        atlas->rows = (static_cast<int>(tiles.size()) + atlas->columns - 1) / atlas->columns;
        int width = kTile * atlas->columns, height = kTile * atlas->rows;
        Pixels pixels(static_cast<size_t>(width) * height * 4, 0);
        for (size_t slot = 0; slot < tiles.size(); ++slot) {
            for (int y = 0; y < kTile; ++y) {
                auto dest = ((slot / atlas->columns * kTile + y) * width + slot % atlas->columns * kTile) * 4;
                std::copy_n(tiles[slot]->data() + y * kTile * 4, kTile * 4, pixels.data() + dest);
            }
        }
        auto texture = new CCTexture2D();
        if (texture->initWithData(pixels.data(), kCCTexture2DPixelFormat_RGBA8888, width, height, {float(width), float(height)})) {
            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
            texture->setTexParameters(&params);
            atlas->texture = texture;
            atlas->textureBytes = pixels.size();
        } else {
            atlas->slots.clear();
        }
        texture->release();
    }
    cache.index[paths] = atlas;
    cache.touch(atlas);
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (elapsed >= 8.0) {
        log::debug("[IconGradients] Atlas build: {:.1f} ms, {} images, {} source decodes, {} KiB",
            elapsed, atlas->slots.size(), decodes, atlas->textureBytes / 1024);
    }
    return atlas;
}
