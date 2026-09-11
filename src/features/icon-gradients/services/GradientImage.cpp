#include "GradientImage.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

using namespace geode::prelude;
using namespace paimon::icon_gradients;

std::shared_ptr<GradientImageAtlas> paimon::icon_gradients::getGradientImageAtlas(std::vector<SimplePoint> const& points) {
    // One sampler for all 24 points, including on mobile GPUs. These dimensions
    // match pointColor in the six gradient shaders. Samples stay inside tiles.
    constexpr int tile = 256, columns = 4, rows = 6;
    constexpr int width = tile * columns, height = tile * rows;
    std::vector<std::string> paths;
    for (auto const& point : points) {
        if (!point.imagePath.empty()) paths.push_back(point.imagePath);
    }
    if (paths.empty()) return nullptr;
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (paths.size() > 24) paths.resize(24);

    // Sprites own active atlases; the cache never keeps unused image data alive.
    static std::map<std::vector<std::string>, std::weak_ptr<GradientImageAtlas>> cache;
    if (auto found = cache.find(paths); found != cache.end()) {
        if (auto atlas = found->second.lock()) return atlas;
    }
    std::erase_if(cache, [](auto const& entry) { return entry.second.expired(); });
    auto atlas = std::make_shared<GradientImageAtlas>();
    std::vector<unsigned char> pixels(width * height * 4, 0);
    for (auto const& path : paths) {
        auto bytesResult = utils::file::readBinary(paimon::assets::pathFromUtf8(path));
        if (bytesResult.isErr()) continue;
        auto bytes = std::move(bytesResult).unwrap();
        if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) continue;
        CCImage image;
        if (!image.initWithImageData(bytes.data(), static_cast<int>(bytes.size())) ||
            !image.getData() || !image.getWidth() || !image.getHeight() || image.getBitsPerComponent() != 8) continue;
        int slot = static_cast<int>(atlas->slots.size());
        int channels = image.hasAlpha() ? 4 : 3;
        int sourceWidth = image.getWidth(), sourceHeight = image.getHeight();
        auto source = image.getData();
        auto sample = [&](int x, int y, int channel) -> float {
            auto pixel = source + (static_cast<size_t>(y) * sourceWidth + x) * channels;
            float alpha = image.hasAlpha() ? pixel[3] : 255.f;
            if (channel == 3) return alpha;
            return pixel[channel] * (image.isPremultipliedAlpha() ? 1.f : alpha / 255.f);
        };
        for (int y = 0; y < tile; ++y) {
            float sy = std::clamp((y + 0.5f) * sourceHeight / tile - 0.5f, 0.f, float(sourceHeight - 1));
            int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, sourceHeight - 1);
            for (int x = 0; x < tile; ++x) {
                float sx = std::clamp((x + 0.5f) * sourceWidth / tile - 0.5f, 0.f, float(sourceWidth - 1));
                int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, sourceWidth - 1);
                auto dest = ((slot / columns * tile + y) * width + slot % columns * tile + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    float top = std::lerp(sample(x0, y0, c), sample(x1, y0, c), sx - x0);
                    float bottom = std::lerp(sample(x0, y1, c), sample(x1, y1, c), sx - x0);
                    pixels[dest + c] = static_cast<unsigned char>(std::clamp(std::lerp(top, bottom, sy - y0), 0.f, 255.f));
                }
            }
        }
        atlas->slots.emplace(path, slot);
    }
    // Cache failures too while a sprite uses this configuration; missing files
    // fall back to their point's saved color without repeated disk reads.
    if (!atlas->slots.empty()) {
        auto texture = new CCTexture2D();
        if (texture->initWithData(pixels.data(), kCCTexture2DPixelFormat_RGBA8888, width, height, {float(width), float(height)})) {
            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
            texture->setTexParameters(&params);
            atlas->texture = texture;
        } else {
            atlas->slots.clear();
        }
        texture->release();
    }
    cache[paths] = atlas;
    return atlas;
}
