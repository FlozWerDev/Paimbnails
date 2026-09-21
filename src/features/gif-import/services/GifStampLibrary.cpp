#include "GifStampLibrary.hpp"

#include "GifStampCatalog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/ObjectToolbox.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_set>
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
// Umbrales de aceptacion directa por forma (radial, vertical, cuartos). Por
// encima el mejor sigue quedando como degradado antes que el repuesto.
constexpr std::array<double, 3> kSoftThresholds{0.05, 0.08, 0.08};
// Repuesto determinista: circulo con blending como glow, bloque solo si el
// circulo no existe en esta version de GD.
constexpr int kFallbackGlowCircle = 3637;
constexpr int kFallbackBlock = 211;

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

bool usableObject(GameObject* object, bool strict = true) {
    if (!object) return false;
    if (object->m_objectType != GameObjectType::Decoration) return false;
    // En la pasada de repuesto el tinte no filtra: un nativo parecido aunque no
    // acepte color sigue dibujando mejor que el repuesto, y el emisor avisa si
    // el tinte no entra.
    if (strict && !object->m_isSolidColorBlock && !object->canChangeMainColor()) {
        return false;
    }
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

    auto install = [&](int kind, int id, int quarter, StampMask mask, CCSize content) {
        auto& stamp = best[kind == 2 ? 3 : kind];
        stamp.objectId = id;
        stamp.baseWidth = quarter % 2 ? content.height : content.width;
        stamp.baseHeight = quarter % 2 ? content.width : content.height;
        stamp.rotation = quarter * 90.f;
        stamp.mask = std::move(mask);
    };

    // El tinte no cambia el alfa: lo estricto solo decide si el objeto se
    // considera, no como sale su molde.
    auto scanObject = [&](int id, bool relaxed) {
        BatchPool const pool;
        auto* object = GameObject::createWithKey(id);
        if (!usableObject(object, !relaxed)) return false;
        auto* frame = object->displayFrame();
        if (!frame) return false;
        auto* sprite = CCSprite::createWithSpriteFrame(frame);
        auto* canvas = CCRenderTexture::create(side, side, kCCTexture2DPixelFormat_RGBA8888);
        if (!sprite || !canvas) return false;
        auto const size = sprite->getContentSize();
        if (size.width < 1.f || size.height < 1.f) return false;
        sprite->setScaleX(side / size.width);
        sprite->setScaleY(side / size.height);
        sprite->setPosition({side * 0.5f, side * 0.5f});
        sprite->setBlendFunc({GL_ONE, GL_ZERO});
        canvas->beginWithClear(0.f, 0.f, 0.f, 0.f);
        sprite->visit();
        canvas->end();
        auto* image = canvas->newCCImage(true);
        if (!image) return false;
        if (!image->getData() || image->getWidth() < side || image->getHeight() < side) {
            image->release();
            return false;
        }
        StampMask original{side, side, std::vector<std::uint8_t>(side * side)};
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            original.coverage[y * side + x] = image->getData()[
                (y * image->getWidth() + x) * 4 + 3];
        }
        image->release();
        auto const content = object->getContentSize();
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
                if (scores[kind] < errors[kind]) errors[kind] = scores[kind];
                if (scores[kind] < overall[kind].score) {
                    overall[kind] = {scores[kind], id, quarter, mask, content};
                }
                if (scores[kind] >= accepted[kind]) continue;
                accepted[kind] = scores[kind];
                install(kind, id, quarter, mask, content);
            }
        }
        return true;
    };

    auto nameMatches = [](std::string const& frameName) {
        // The bindings expose the runtime key/frame map, not a fixed glow ID.
        // Match the actual alpha field, including texture packs and sprite quality.
        return frameName.find("light") != std::string::npos ||
            frameName.find("glow") != std::string::npos ||
            frameName.find("gradient") != std::string::npos ||
            frameName.find("particle") != std::string::npos;
    };
    std::unordered_set<int> considered;
    for (auto const& [id, name] : toolbox->m_allKeys) {
        std::string const frameName(name.c_str());
        if (!nameMatches(frameName)) continue;
        if (scanObject(id, false)) considered.insert(id);
    }
    bool const found =
        best[0].objectId || best[1].objectId || best[3].objectId;
    // Segunda pasada exhaustiva sobre TODA la decoracion si el filtro no
    // encontro nada: el nombre del frame no es fiable entre packs de texturas
    // y versiones de GD, y el tinte ya no filtra aqui.
    if (!found) {
        for (auto const& [id, name] : toolbox->m_allKeys) {
            (void)name;
            if (considered.count(id)) continue;
            scanObject(id, true);
        }
    }
    // El mejor se queda aunque supere el umbral, como degradado: un nativo
    // parecido sigue dibujando mejor que el repuesto analitico.
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
    // Repuesto determinista con IDs fijos cuando no hay nativo. El 2903 no
    // sirve por celda: es un quad a pantalla completa, solo dice CUANDO dispara.
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
    // Se cachea solo si quedo completa (nativos + repuesto): si GL o el
    // toolbox aun no estaban listos se reintenta en el proximo procesado,
    // como antes.
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
