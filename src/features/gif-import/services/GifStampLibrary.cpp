#include "GifStampLibrary.hpp"

#include "GifStampCatalog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/ObjectToolbox.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace paimon::gifimport {

namespace {

// Cada objeto se dibuja en su celda de un atlas y se lee la lamina entera de una
// vez: mil objetos son cuatro lecturas de la GPU en vez de mil, que es la
// diferencia entre un parpadeo y medio minuto.
constexpr int kCellSide = kStampMaskSide;
constexpr int kAtlasCells = 16;
constexpr int kAtlasSide = kCellSide * kAtlasCells;
constexpr int kBatch = kAtlasCells * kAtlasCells;
constexpr unsigned char kAlphaFloor = 96;
// Por debajo de esto el objeto es un contorno o una chispa: como molde solo sabe
// dejar huecos, y el trazado de pintura ya cubre ese tamano mejor.
constexpr float kMinCoverage = 0.12f;

bool g_ready = false;

// Una tanda crea cientos de objetos y sus laminas: sin piscina propia no se
// sueltan hasta el final del fotograma y el pico de memoria es el de la
// biblioteca entera a la vez.
struct BatchPool {
    BatchPool() { CCPoolManager::sharedPoolManager()->push(); }
    ~BatchPool() { CCPoolManager::sharedPoolManager()->pop(); }
};

struct Pending {
    int objectId = 0;
    CCSize content{30.f, 30.f};
};

bool usableObject(GameObject* object) {
    if (!object) return false;
    if (object->m_objectType != GameObjectType::Decoration) return false;
    if (!object->m_isSolidColorBlock && !object->canChangeMainColor()) return false;
    auto const size = object->getContentSize();
    return size.width > 4.f && size.height > 4.f &&
        size.width < 512.f && size.height < 512.f;
}

// Recorta la celda a lo que pinta y la devuelve como molde, con el corrimiento
// que hace falta para que ese recorte caiga donde el plan lo pida.
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
        // La lamina se lee con las filas de arriba abajo, asi que la fila 0 del
        // atlas se dibuja arriba del todo para que los indices cuadren.
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

} // namespace

std::vector<PlanStamp> buildSoftStampLibrary() {
    static std::vector<PlanStamp> cached;
    if (!cached.empty()) return cached;
    auto* toolbox = ObjectToolbox::sharedState();
    if (!toolbox) return {};
    constexpr int side = 32;
    std::vector<PlanStamp> best(7);
    std::array<double, 3> errors{0.012, 0.025, 0.025};
    // The bindings expose the runtime key/frame map, not a fixed glow ID.
    // Match the actual alpha field, including texture packs and sprite quality.
    for (auto const& [id, name] : toolbox->m_allKeys) {
        std::string const frameName(name.c_str());
        if (frameName.find("light") == std::string::npos &&
            frameName.find("glow") == std::string::npos &&
            frameName.find("gradient") == std::string::npos &&
            frameName.find("particle") == std::string::npos) continue;
        BatchPool const pool;
        auto* object = GameObject::createWithKey(id);
        if (!usableObject(object)) continue;
        auto* frame = object->displayFrame();
        if (!frame) continue;
        auto* sprite = CCSprite::createWithSpriteFrame(frame);
        auto* canvas = CCRenderTexture::create(side, side, kCCTexture2DPixelFormat_RGBA8888);
        if (!sprite || !canvas) continue;
        auto const size = sprite->getContentSize();
        sprite->setScaleX(side / size.width);
        sprite->setScaleY(side / size.height);
        sprite->setPosition({side * 0.5f, side * 0.5f});
        sprite->setBlendFunc({GL_ONE, GL_ZERO});
        canvas->beginWithClear(0.f, 0.f, 0.f, 0.f);
        sprite->visit();
        canvas->end();
        auto* image = canvas->newCCImage(true);
        if (!image) continue;
        if (!image->getData() || image->getWidth() < side || image->getHeight() < side) {
            image->release();
            continue;
        }
        StampMask original{side, side, std::vector<std::uint8_t>(side * side)};
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            original.coverage[y * side + x] = image->getData()[
                (y * image->getWidth() + x) * 4 + 3];
        }
        image->release();
        for (int quarter = 0; quarter < 4; ++quarter) {
            StampMask mask = original;
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                int sx = x, sy = y;
                if (quarter == 1) { sx = y; sy = side - 1 - x; }
                if (quarter == 2) { sx = side - 1 - x; sy = side - 1 - y; }
                if (quarter == 3) { sx = side - 1 - y; sy = x; }
                mask.coverage[y * side + x] = original.coverage[sy * side + sx];
            }
            double radialError = 0., verticalError = 0., quarterError = 0.;
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                float const u = (x + 0.5f) / side;
                float const v = (y + 0.5f) / side;
                float const radius2 = 4.f * ((u - 0.5f) * (u - 0.5f) +
                    (v - 0.5f) * (v - 0.5f));
                float const alpha = mask.coverage[y * side + x] / 255.f;
                float const radial = std::max(0.f, (std::exp(-4.f * radius2) -
                    std::exp(-4.f)) / (1.f - std::exp(-4.f)));
                float const corner = std::pow(std::max(0.f, 1.f -
                    std::sqrt((1.f - u) * (1.f - u) + (1.f - v) * (1.f - v))), 1.3f);
                quarterError += (alpha - corner) * (alpha - corner);
                radialError += (alpha - radial) * (alpha - radial);
                verticalError += (alpha - (1.f - v)) * (alpha - (1.f - v));
            }
            std::array<double, 3> const scores{
                radialError / (side * side), verticalError / (side * side), quarterError / (side * side)};
            for (int kind = 0; kind < 3; ++kind) {
                if (scores[kind] >= errors[kind]) continue;
                errors[kind] = scores[kind];
                auto& stamp = best[kind == 2 ? 3 : kind];
                stamp.objectId = id;
                auto const content = object->getContentSize();
                stamp.baseWidth = quarter % 2 ? content.height : content.width;
                stamp.baseHeight = quarter % 2 ? content.width : content.height;
                stamp.rotation = quarter * 90.f;
                stamp.mask = mask;
            }
        }
    }
    if (best[1].objectId) {
        best[2] = best[1];
        best[2].rotation = std::fmod(best[1].rotation + 180.f, 360.f);
        std::reverse(best[2].mask.coverage.begin(), best[2].mask.coverage.end());
    }
    // Some GD catalogs expose the radial glow as four quarter-circle pieces.
    // Assemble those native pieces around one centre, without a solid stand-in.
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
    log::info("[GifImport] Native soft shapes: round={} ({}), vert={} ({}), quarter={} ({})",
        best[0].objectId, errors[0], best[1].objectId, errors[1], best[3].objectId, errors[2]);
    // Retry if resources were not ready when the popup first opened.
    if ((best[0].objectId || best[3].objectId) && best[1].objectId) cached = best;
    return best;
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
        // displayFrame() devuelve una lamina nueva cada vez y se va con la
        // piscina, asi que la tanda se dibuja dentro del mismo ambito en el que
        // se pidieron los objetos.
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
    log::info("[GifImport] Biblioteca de moldes: {} orientaciones", variants);
    return variants;
}

} // namespace paimon::gifimport
