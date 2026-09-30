#include "GifStampLibrary.hpp"

#include "GifStampCatalog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/ObjectToolbox.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace paimon::gifimport {

namespace {

// each object draws into one atlas cell and the whole sheet reads at once:
// a thousand objects cost four gpu reads instead of a thousand (a blink vs half a minute).
constexpr int kCellSide = kStampMaskSide;
constexpr int kAtlasCells = 16;
constexpr int kAtlasSide = kCellSide * kAtlasCells;
constexpr int kBatch = kAtlasCells * kAtlasCells;
constexpr unsigned char kAlphaFloor = 96;
// below this the object is trim or sparkle: as a mold it only leaves holes,
// and paint tracing already covers that size better.
constexpr float kMinCoverage = 0.12f;
// direct-accept thresholds per shape (radial, vertical, quarters). past them
// the best still stays as fallback before the spare.
constexpr std::array<double, 3> kSoftThresholds{0.05, 0.08, 0.08};
// deterministic spare: circle-with-blending as glow, block only when this gd
// build has no circle.
constexpr int kFallbackGlowCircle = 3637;
constexpr int kFallbackBlock = 211;

bool g_ready = false;

// one batch spawns hundreds of objects and sheets: without its own pool they
// release at frame end and peak memory is the whole library at once.
struct BatchPool {
    BatchPool() { CCPoolManager::sharedPoolManager()->push(); }
    ~BatchPool() { CCPoolManager::sharedPoolManager()->pop(); }
};

struct Pending {
    int objectId = 0;
    CCSize content{30.f, 30.f};
};

bool usableObject(GameObject* object, bool strict = true) {
    if (!object) return false;
    if (object->m_objectType != GameObjectType::Decoration) return false;
    // spare pass ignores tint: a close native that takes no color still draws
    // better than the spare, and the emitter warns when tint won't apply.
    if (strict && !object->m_isSolidColorBlock && !object->canChangeMainColor()) {
        return false;
    }
    auto const size = object->getContentSize();
    return size.width > 4.f && size.height > 4.f &&
        size.width < 512.f && size.height < 512.f;
}

// trims the cell to what paints and returns it as a mold, with the shift
// landing that trim where the plan asks.
bool cutStamp(
    unsigned char const* pixels,
    int stride,
    int cellX,
    int cellY,
    Pending const& pending,
    CatalogEntry& entry
) {
    int minX = kCellSide;
    int minY = kCellSide;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < kCellSide; ++y) {
        for (int x = 0; x < kCellSide; ++x) {
            std::size_t const index =
                (static_cast<std::size_t>(cellY + y) * stride + cellX + x) * 4;
            if (pixels[index + 3] < kAlphaFloor) continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < minX || maxY < minY) return false;

    int const width = maxX - minX + 1;
    int const height = maxY - minY + 1;
    int covered = 0;
    entry.mask.width = width;
    entry.mask.height = height;
    entry.mask.coverage.assign(static_cast<std::size_t>(width) * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::size_t const index =
                (static_cast<std::size_t>(cellY + minY + y) * stride + cellX + minX + x) * 4;
            auto const alpha = pixels[index + 3];
            entry.mask.coverage[static_cast<std::size_t>(y) * width + x] = alpha;
            covered += alpha >= kAlphaFloor;
        }
    }
    if (covered < static_cast<int>(width * height * kMinCoverage)) return false;

    entry.objectId = pending.objectId;
    entry.baseWidth = pending.content.width * width / kCellSide;
    entry.baseHeight = pending.content.height * height / kCellSide;
    float const centerX = (minX + maxX + 1) * 0.5f;
    float const centerY = (minY + maxY + 1) * 0.5f;
    entry.offsetX = (kCellSide * 0.5f - centerX) / width;
    entry.offsetY = (kCellSide * 0.5f - centerY) / height;
    return true;
}

void drawBatch(
    std::vector<Pending> const& batch,
    std::vector<CCSpriteFrame*> const& frames,
    std::vector<CatalogEntry>& entries
) {
    auto* canvas = CCRenderTexture::create(
        kAtlasSide, kAtlasSide, kCCTexture2DPixelFormat_RGBA8888);
    if (!canvas) return;
    canvas->beginWithClear(0.f, 0.f, 0.f, 0.f);
    for (std::size_t slot = 0; slot < batch.size(); ++slot) {
        auto* sprite = CCSprite::createWithSpriteFrame(frames[slot]);
        if (!sprite) continue;
        auto const content = sprite->getContentSize();
        if (content.width < 1.f || content.height < 1.f) continue;
        int const column = static_cast<int>(slot) % kAtlasCells;
        int const row = static_cast<int>(slot) / kAtlasCells;
        sprite->setAnchorPoint({0.5f, 0.5f});
        sprite->setScaleX(kCellSide / content.width);
        sprite->setScaleY(kCellSide / content.height);
        // sheets read top-down, so atlas row 0 draws at the very top to keep indices straight.
        sprite->setPosition({
            (column + 0.5f) * kCellSide,
            kAtlasSide - (row + 0.5f) * kCellSide
        });
        sprite->visit();
    }
    canvas->end();

    auto* image = canvas->newCCImage(true);
    if (!image) return;
    auto const* pixels = image->getData();
    int const stride = image->getWidth();
    if (pixels && stride >= kAtlasSide && image->getHeight() >= kAtlasSide) {
        for (std::size_t slot = 0; slot < batch.size(); ++slot) {
            CatalogEntry entry;
            int const column = static_cast<int>(slot) % kAtlasCells;
            int const row = static_cast<int>(slot) / kAtlasCells;
            if (cutStamp(
                    pixels, stride, column * kCellSide, row * kCellSide,
                    batch[slot], entry)) {
                entries.push_back(std::move(entry));
            }
        }
    }
    image->release();
}

struct SoftMaskSample {
    int objectId = 0;
    CCSize content{30.f, 30.f};
    StampMask mask;
};

std::vector<SoftMaskSample> drawSoftBatch(
    std::vector<int> const& objectIds,
    std::size_t begin,
    std::size_t end,
    bool relaxed
) {
    constexpr int side = 32;
    constexpr int atlasCells = 8;
    constexpr int atlasSide = side * atlasCells;
    BatchPool const pool;
    auto* canvas = CCRenderTexture::create(
        atlasSide, atlasSide, kCCTexture2DPixelFormat_RGBA8888);
    if (!canvas) return {};

    struct DrawnObject {
        int id = 0;
        CCSize content{30.f, 30.f};
    };
    std::vector<DrawnObject> drawn;
    drawn.reserve(end - begin);
    canvas->beginWithClear(0.f, 0.f, 0.f, 0.f);
    for (std::size_t index = begin; index < end; ++index) {
        int const id = objectIds[index];
        auto* object = GameObject::createWithKey(id);
        if (!usableObject(object, !relaxed)) continue;
        auto* frame = object->displayFrame();
        auto* sprite = frame ? CCSprite::createWithSpriteFrame(frame) : nullptr;
        if (!sprite) continue;
        auto const spriteSize = sprite->getContentSize();
        if (spriteSize.width < 1.f || spriteSize.height < 1.f) continue;

        std::size_t const slot = drawn.size();
        int const column = static_cast<int>(slot) % atlasCells;
        int const row = static_cast<int>(slot) / atlasCells;
        sprite->setAnchorPoint({0.5f, 0.5f});
        sprite->setScaleX(side / spriteSize.width);
        sprite->setScaleY(side / spriteSize.height);
        sprite->setPosition({
            (column + 0.5f) * side,
            atlasSide - (row + 0.5f) * side
        });
        sprite->setBlendFunc({GL_ONE, GL_ZERO});
        sprite->visit();
        drawn.push_back({id, object->getContentSize()});
    }
    canvas->end();

    auto* image = canvas->newCCImage(true);
    if (!image) return {};
    auto const* pixels = image->getData();
    int const stride = image->getWidth();
    if (!pixels || stride < atlasSide ||
        image->getHeight() < atlasSide) {
        image->release();
        return {};
    }

    std::vector<SoftMaskSample> samples;
    samples.reserve(drawn.size());
    for (std::size_t slot = 0; slot < drawn.size(); ++slot) {
        int const column = static_cast<int>(slot) % atlasCells;
        int const row = static_cast<int>(slot) / atlasCells;
        StampMask mask{side, side, std::vector<std::uint8_t>(side * side)};
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            std::size_t const pixel =
                (static_cast<std::size_t>(row * side + y) * stride +
                    column * side + x) * 4;
            mask.coverage[static_cast<std::size_t>(y) * side + x] =
                pixels[pixel + 3];
        }
        samples.push_back({drawn[slot].id, drawn[slot].content, std::move(mask)});
    }
    image->release();
    return samples;
}

} // namespace

SoftStampLibrary buildSoftStampLibrary() {
    static SoftStampLibrary cached;
    if (!cached.stamps.empty()) return cached;
    SoftStampLibrary library;
    auto* toolbox = ObjectToolbox::sharedState();
    if (!toolbox) return library;
    constexpr int side = 32;
    library.stamps.assign(7, PlanStamp{});
    auto& best = library.stamps;
    auto& errors = library.errors;
    std::array<double, 3> accepted{kSoftThresholds[0], kSoftThresholds[1], kSoftThresholds[2]};
    struct Overall {
        double score = 1.0;
        int id = 0;
        int quarter = 0;
        StampMask mask;
        CCSize content{30.f, 30.f};
    };
    std::array<Overall, 3> overall;
    std::array<float, side * side> radialTarget{};
    std::array<float, side * side> verticalTarget{};
    std::array<float, side * side> quarterTarget{};
    float const radialEdge = std::exp(-4.f);
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        std::size_t const index = static_cast<std::size_t>(y) * side + x;
        float const u = (x + 0.5f) / side;
        float const v = (y + 0.5f) / side;
        float const radius2 = 4.f * ((u - 0.5f) * (u - 0.5f) +
            (v - 0.5f) * (v - 0.5f));
        radialTarget[index] = std::max(0.f, (std::exp(-4.f * radius2) -
            radialEdge) / (1.f - radialEdge));
        verticalTarget[index] = 1.f - v;
        quarterTarget[index] = std::pow(std::max(0.f, 1.f -
            std::sqrt((1.f - u) * (1.f - u) + (1.f - v) * (1.f - v))), 1.3f);
    }

    auto install = [&](int kind, int id, int quarter, StampMask mask, CCSize content) {
        auto& stamp = best[kind == 2 ? 3 : kind];
        stamp.objectId = id;
        stamp.baseWidth = quarter % 2 ? content.height : content.width;
        stamp.baseHeight = quarter % 2 ? content.width : content.height;
        stamp.rotation = quarter * 90.f;
        stamp.mask = std::move(mask);
    };

    auto scoreSample = [&](SoftMaskSample const& sample) {
        for (int quarter = 0; quarter < 4; ++quarter) {
            StampMask mask = sample.mask;
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                int sx = x, sy = y;
                if (quarter == 1) { sx = y; sy = side - 1 - x; }
                if (quarter == 2) { sx = side - 1 - x; sy = side - 1 - y; }
                if (quarter == 3) { sx = side - 1 - y; sy = x; }
                mask.coverage[y * side + x] = sample.mask.coverage[sy * side + sx];
            }
            double radialError = 0., verticalError = 0., quarterError = 0.;
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * side + x;
                float const alpha = mask.coverage[index] / 255.f;
                float const radialDelta = alpha - radialTarget[index];
                float const verticalDelta = alpha - verticalTarget[index];
                float const quarterDelta = alpha - quarterTarget[index];
                quarterError += quarterDelta * quarterDelta;
                radialError += radialDelta * radialDelta;
                verticalError += verticalDelta * verticalDelta;
            }
            std::array<double, 3> const scores{
                radialError / (side * side), verticalError / (side * side), quarterError / (side * side)};
            for (int kind = 0; kind < 3; ++kind) {
                if (scores[kind] < errors[kind]) errors[kind] = scores[kind];
                if (scores[kind] < overall[kind].score) {
                    overall[kind] = {
                        scores[kind], sample.objectId, quarter, mask, sample.content};
                }
                if (scores[kind] >= accepted[kind]) continue;
                accepted[kind] = scores[kind];
                install(kind, sample.objectId, quarter, mask, sample.content);
            }
        }
    };

    auto nameMatches = [](std::string const& frameName) {
        // the bindings expose the runtime key/frame map, not a fixed glow id.
        // match the actual alpha field, including texture packs and sprite quality.
        return frameName.find("light") != std::string::npos ||
            frameName.find("glow") != std::string::npos ||
            frameName.find("gradient") != std::string::npos ||
            frameName.find("particle") != std::string::npos;
    };
    std::unordered_set<int> considered;
    std::vector<int> namedObjects;
    std::vector<int> allObjects;
    for (auto const& [id, name] : toolbox->m_allKeys) {
        allObjects.push_back(id);
        std::string const frameName(name.c_str());
        if (nameMatches(frameName)) namedObjects.push_back(id);
    }
    auto scanBatches = [&](std::vector<int> const& objectIds, bool relaxed) {
        constexpr std::size_t batchSize = 64;
        for (std::size_t begin = 0; begin < objectIds.size(); begin += batchSize) {
            auto const end = std::min(begin + batchSize, objectIds.size());
            for (auto const& sample : drawSoftBatch(objectIds, begin, end, relaxed)) {
                if (!relaxed) considered.insert(sample.objectId);
                scoreSample(sample);
            }
        }
    };
    scanBatches(namedObjects, false);
    bool const found =
        best[0].objectId || best[1].objectId || best[3].objectId;
    // exhaustive second pass over all decoration when the filter found nothing:
    // frame names lie across texture packs and gd versions, and tint stops filtering here.
    if (!found) {
        std::vector<int> remaining;
        remaining.reserve(allObjects.size() - std::min(allObjects.size(), considered.size()));
        for (int id : allObjects) if (!considered.count(id)) remaining.push_back(id);
        scanBatches(remaining, true);
    }
    // best stays past the threshold as fallback: a close native still draws
    // better than the analytic spare.
    for (int kind = 0; kind < 3; ++kind) {
        int const slot = kind == 2 ? 3 : kind;
        if (best[slot].objectId || !overall[kind].id) continue;
        install(kind, overall[kind].id, overall[kind].quarter,
            std::move(overall[kind].mask), overall[kind].content);
    }
    if (best[1].objectId) {
        best[2] = best[1];
        best[2].rotation = std::fmod(best[1].rotation + 180.f, 360.f);
        std::reverse(best[2].mask.coverage.begin(), best[2].mask.coverage.end());
    }
    // some gd catalogs expose the radial glow as four quarter-circle pieces.
    // assemble those native pieces around one centre, without a solid stand-in.
    if (best[3].objectId) for (int q = 1; q < 4; ++q) {
        best[3 + q] = best[3];
        auto& stamp = best[3 + q];
        stamp.rotation = std::fmod(best[3].rotation + q * 90.f, 360.f);
        if (q % 2) std::swap(stamp.baseWidth, stamp.baseHeight);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            int sx = x, sy = y;
            if (q == 1) { sx = y; sy = side - 1 - x; }
            if (q == 2) { sx = side - 1 - x; sy = side - 1 - y; }
            if (q == 3) { sx = side - 1 - y; sy = x; }
            stamp.mask.coverage[y * side + x] = best[3].mask.coverage[sy * side + sx];
        }
    }
    // deterministic spare with fixed ids when no native. 2903 is useless per
    // cell: a full-screen quad, only telling when it fires.
    {
        int fallbackId = 0;
        CCSize fallbackSize{50.f, 50.f};
        for (int candidate : {kFallbackGlowCircle, kFallbackBlock}) {
            BatchPool const pool;
            auto* object = GameObject::createWithKey(candidate);
            auto* frame = object ? object->displayFrame() : nullptr;
            if (!frame) continue;
            fallbackId = candidate;
            auto const size = object->getContentSize();
            fallbackSize = size.width > 4.f && size.width < 512.f &&
                    size.height > 4.f && size.height < 512.f
                ? size
                : (candidate == kFallbackGlowCircle ? CCSize{50.f, 50.f}
                                                   : CCSize{30.f, 30.f});
            break;
        }
        if (fallbackId) {
            auto makeFallback = [&](int slot, StampMask mask, float rotation) {
                auto& stamp = best[slot];
                if (stamp.objectId) return;
                stamp.objectId = fallbackId;
                bool const swapped =
                    std::abs(std::fmod(std::abs(rotation), 180.f) - 90.f) < 0.5f;
                stamp.baseWidth = swapped ? fallbackSize.height : fallbackSize.width;
                stamp.baseHeight = swapped ? fallbackSize.width : fallbackSize.height;
                stamp.rotation = rotation;
                stamp.analyticFallback = true;
                stamp.mask = std::move(mask);
            };
            makeFallback(0, analyticRadialGlowMask(), 0.f);
            auto ramp = analyticVerticalGradientMask();
            auto mirrored = ramp;
            std::reverse(mirrored.coverage.begin(), mirrored.coverage.end());
            makeFallback(1, std::move(ramp), 0.f);
            makeFallback(2, std::move(mirrored), 180.f);
            auto quarter = analyticQuarterGlowMask();
            for (int q = 0; q < 4; ++q) {
                StampMask rotated{side, side, std::vector<std::uint8_t>(side * side)};
                for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                    int sx = x, sy = y;
                    if (q == 1) { sx = y; sy = side - 1 - x; }
                    if (q == 2) { sx = side - 1 - x; sy = side - 1 - y; }
                    if (q == 3) { sx = side - 1 - y; sy = x; }
                    rotated.coverage[y * side + x] = quarter.coverage[sy * side + sx];
                }
                makeFallback(3 + q, std::move(rotated), q * 90.f);
            }
        }
    }
    bool fallbackUsed = false;
    for (auto const& stamp : best) fallbackUsed = fallbackUsed || stamp.analyticFallback;
    log::info("[GifImport] Native soft shapes: round={} ({}), vert={} ({}), quarter={} ({}){}",
        best[0].objectId, errors[0], best[1].objectId, errors[1], best[3].objectId, errors[2],
        fallbackUsed ? " +repuesto analitico" : "");
    // caches only when complete (natives + spare): unready gl/toolbox retries
    // on the next process, as before.
    bool complete = best.size() == 7;
    for (auto const& stamp : best) complete = complete && stamp.objectId > 0;
    if (complete) cached = library;
    return library;
}

bool stampLibraryReady() {
    return g_ready;
}

std::size_t buildStampLibrary() {
    if (g_ready) return stampVariants().size();
    auto* toolbox = ObjectToolbox::sharedState();
    if (!toolbox) return 0;

    std::vector<int> ids;
    ids.reserve(toolbox->m_allKeys.size());
    for (auto const& key : toolbox->m_allKeys) ids.push_back(key.first);

    std::vector<CatalogEntry> entries;
    for (std::size_t start = 0; start < ids.size(); start += kBatch) {
        // displayframe() returns a fresh sheet each call, owned by the pool, so
        // the batch draws in the same scope the objects came from.
        BatchPool const pool;
        std::vector<Pending> batch;
        std::vector<CCSpriteFrame*> frames;
        std::size_t const end = std::min(start + kBatch, ids.size());
        for (std::size_t index = start; index < end; ++index) {
            auto* object = GameObject::createWithKey(ids[index]);
            if (!usableObject(object)) continue;
            auto* frame = object->displayFrame();
            if (!frame) continue;
            batch.push_back({ids[index], object->getContentSize()});
            frames.push_back(frame);
        }
        if (!batch.empty()) drawBatch(batch, frames, entries);
    }

    g_ready = true;
    setStampCatalog(std::move(entries));
    auto const variants = stampVariants().size();
    log::info("[GifImport] Mold library: {} orientations", variants);
    return variants;
}

} // namespace paimon::gifimport
