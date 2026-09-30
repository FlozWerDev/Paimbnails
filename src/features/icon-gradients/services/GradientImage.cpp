#include "GradientImage.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <list>
#include <map>
#include <tuple>

using namespace geode::prelude;
using namespace paimon::icon_gradients;

namespace {
constexpr int kTile = 256;
constexpr size_t kTileCacheEntries = 32; // at most 8 mib of resized source pixels.
constexpr size_t kAtlasCacheBytes = 16 * 1024 * 1024;
using Pixels = std::vector<unsigned char>;
using Clock = std::chrono::steady_clock;

struct SourceStamp {
    std::string path;
    uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
    bool readable = false;

    bool operator==(SourceStamp const&) const = default;
    bool operator<(SourceStamp const& other) const {
        return std::tie(path, size, modified, readable) <
            std::tie(other.path, other.size, other.modified, other.readable);
    }
};

struct TileEntry {
    SourceStamp source;
    std::shared_ptr<Pixels> pixels;
    Clock::time_point loaded;
};

struct SourceEntry {
    SourceStamp stamp;
    Clock::time_point checked;
};

struct AtlasEntry {
    std::weak_ptr<GradientImageAtlas> atlas;
    Clock::time_point built;
    bool complete = false;
};

struct AtlasCache {
    std::list<SourceEntry> sources;
    std::list<TileEntry> tiles;
    // sprites can still own an atlas after lru eviction.
    std::map<std::vector<SourceStamp>, AtlasEntry> index;
    std::list<std::shared_ptr<GradientImageAtlas>> recent;
    size_t bytes = 0;
    bool stopped = false;

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

    SourceStamp source(std::string const& path, Clock::time_point now) {
        auto found = std::find_if(sources.begin(), sources.end(), [&](auto const& entry) {
            return entry.stamp.path == path;
        });
        // avoid a filesystem stat per icon while rendering shared gradients.
        if (found != sources.end() && now - found->checked < std::chrono::seconds(1)) {
            sources.splice(sources.begin(), sources, found);
            return sources.front().stamp;
        }
        SourceStamp stamp{path};
        auto nativePath = paimon::assets::pathFromUtf8(path);
        std::error_code ec;
        if (std::filesystem::is_regular_file(nativePath, ec) && !ec) {
            auto size = std::filesystem::file_size(nativePath, ec);
            if (!ec) {
                auto modified = std::filesystem::last_write_time(nativePath, ec);
                if (!ec) {
                    stamp.size = size;
                    stamp.modified = modified;
                    stamp.readable = true;
                }
            }
        }
        if (found != sources.end()) sources.erase(found);
        sources.push_front({stamp, now});
        if (sources.size() > 128) sources.pop_back();
        return stamp;
    }
};

AtlasCache& atlasCache() {
    static AtlasCache cache;
    return cache;
}

std::shared_ptr<Pixels> loadTile(SourceStamp const& source, int& decodes, Clock::time_point now) {
    auto& cache = atlasCache().tiles;
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->source.path != source.path) continue;
        if (it->source != source || (!it->pixels && now - it->loaded >= std::chrono::seconds(2))) {
            cache.erase(it);
            break;
        }
        auto pixels = it->pixels;
        cache.splice(cache.begin(), cache, it);
        return pixels;
    }
    ++decodes;
    auto decode = [&]() -> std::shared_ptr<Pixels> {
        if (!source.readable) return nullptr;
        auto bytes = ImageLoadHelper::readBinaryFile(paimon::assets::pathFromUtf8(source.path), 64);
        if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return nullptr;
        int width = 0, height = 0, channels = 0;
        if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) &&
            (width <= 0 || height <= 0 || width > ImageLoadHelper::kMaxImageDim || height > ImageLoadHelper::kMaxImageDim)) return nullptr;
        CCImage image;
        if (!image.initWithImageData(bytes.data(), static_cast<int>(bytes.size())) ||
            !image.getData() || !image.getWidth() || !image.getHeight() || image.getBitsPerComponent() != 8 ||
            image.getWidth() > ImageLoadHelper::kMaxImageDim || image.getHeight() > ImageLoadHelper::kMaxImageDim) return nullptr;
        channels = image.hasAlpha() ? 4 : 3;
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
    cache.push_front({source, pixels, now});
    if (cache.size() > kTileCacheEntries) cache.pop_back();
    return pixels;
}

}

std::shared_ptr<GradientImageAtlas> paimon::icon_gradients::getGradientImageAtlas(std::vector<SimplePoint> const& points) {
    if (paimon::isRuntimeShuttingDown()) return nullptr;
    auto& cache = atlasCache();
    if (cache.stopped) return nullptr;
    std::vector<std::string> paths;
    for (auto const& point : points) {
        if (!point.imagePath.empty()) paths.push_back(point.imagePath);
    }
    if (paths.empty()) return nullptr;
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (paths.size() > 24) paths.resize(24);

    auto started = Clock::now();
    std::vector<SourceStamp> sources;
    sources.reserve(paths.size());
    for (auto const& path : paths) sources.push_back(cache.source(path, started));
    if (auto found = cache.index.find(sources); found != cache.index.end()) {
        if (auto atlas = found->second.atlas.lock(); atlas &&
            (found->second.complete || started - found->second.built < std::chrono::seconds(2))) {
            cache.touch(atlas);
            return atlas;
        }
    }
    std::erase_if(cache.index, [](auto const& entry) { return entry.second.atlas.expired(); });
    int decodes = 0;
    auto atlas = std::make_shared<GradientImageAtlas>();
    std::vector<std::shared_ptr<Pixels>> tiles;
    for (auto const& source : sources) {
        if (auto tile = loadTile(source, decodes, started)) {
            atlas->slots.emplace(source.path, static_cast<int>(tiles.size()));
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
        auto texture = Ref<CCTexture2D>::adopt(new CCTexture2D());
        if (texture->initWithData(pixels.data(), kCCTexture2DPixelFormat_RGBA8888, width, height, {float(width), float(height)})) {
            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
            texture->setTexParameters(&params);
            atlas->texture = texture;
            atlas->textureBytes = pixels.size();
        } else {
            atlas->slots.clear();
        }
    }
    cache.index[sources] = {atlas, started, atlas->texture && atlas->slots.size() == sources.size()};
    cache.touch(atlas);
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (elapsed >= 8.0) {
        log::debug("[IconGradients] Atlas build: {:.1f} ms, {} images, {} source decodes, {} KiB",
            elapsed, atlas->slots.size(), decodes, atlas->textureBytes / 1024);
    }
    return atlas;
}

void paimon::icon_gradients::shutdownGradientImageCache() {
    auto& cache = atlasCache();
    cache.stopped = true;
    cache.index.clear();
    cache.recent.clear();
    cache.tiles.clear();
    cache.sources.clear();
    cache.bytes = 0;
}
