#include "GifShapeRaster.hpp"

#include "GifStampCatalog.hpp"

#include <limits>

namespace paimon::gifimport {

namespace {

// Un poco de holgura para que el tramo nunca se coma una muestra que el test de
// dentro/fuera si aceptaria. Es medio pixel de la rejilla mas fina que se usa,
// asi que no cuesta nada y quita cualquier duda con los bordes.
constexpr float kSpanSlack = 0.002f;

// El preview y la auditoria fina trabajan a 8 muestras por celda. Validar los
// contornos con solo cuatro dejaba pasar una esquina de hasta un cuarto de celda
// que luego aparecia como pico al ampliar.
constexpr int kFitSamples = 8;

} // namespace

ShapeXform xformOf(Primitive const& object) {
    float const angle = object.rotation * kPi / 180.f;
    ShapeXform shape;
    shape.x = object.x;
    shape.y = object.y;
    shape.width = object.width;
    shape.height = object.height;
    shape.cosine = std::cos(angle);
    shape.sine = std::sin(angle);
    shape.extentX = std::abs(shape.cosine) * object.width * 0.5f +
        std::abs(shape.sine) * object.height * 0.5f;
    shape.extentY = std::abs(shape.sine) * object.width * 0.5f +
        std::abs(shape.cosine) * object.height * 0.5f;
    shape.kind = object.kind;
    // Mientras se traza, el `stamp` de una figura es su sitio en la biblioteca;
    // el plan solo se queda con los que usa y los reindexa al final, y a partir
    // de ahi hay que resolverlos con la lista del plan.
    if (object.kind == PrimitiveKind::Stamp) {
        auto const& variants = stampVariants();
        if (object.stamp < variants.size()) {
            shape.mask = &variants[object.stamp].stamp.mask;
        }
    }
    return shape;
}

ShapeXform xformOf(Primitive const& object, std::vector<PlanStamp> const& stamps) {
    auto shape = xformOf(object);
    if (object.kind == PrimitiveKind::Stamp) {
        shape.mask = object.stamp < stamps.size() ? &stamps[object.stamp].mask : nullptr;
    }
    return shape;
}

std::vector<ShapeXform> xformsOf(std::vector<Primitive> const& objects) {
    std::vector<ShapeXform> shapes;
    shapes.reserve(objects.size());
    for (auto const& object : objects) shapes.push_back(xformOf(object));
    return shapes;
}

std::array<int, 4> xformBox(ShapeXform const& shape, int width, int height) {
    return {
        std::max(0, static_cast<int>(std::floor(shape.x - shape.extentX))),
        std::max(0, static_cast<int>(std::floor(shape.y - shape.extentY))),
        std::min(width - 1, static_cast<int>(std::ceil(shape.x + shape.extentX))),
        std::min(height - 1, static_cast<int>(std::ceil(shape.y + shape.extentY)))
    };
}

// Las cuatro familias de figura son convexas, asi que una fila entra y sale una
// sola vez: para las de lados rectos vale con cruzar la fila contra ellos, y
// para el circulo con resolver la cuadratica de la elipse girada.
bool xformSpan(ShapeXform const& shape, float y, float& fromX, float& toX) {
    if (shape.width <= 0.f || shape.height <= 0.f) return false;
    float const dy = y - shape.y;
    if (std::abs(dy) > shape.extentY) return false;

    if (shape.kind == PrimitiveKind::Circle) {
        float const halfWidth = shape.width * 0.5f;
        float const halfHeight = shape.height * 0.5f;
        float const inverseX = 1.f / (halfWidth * halfWidth);
        float const inverseY = 1.f / (halfHeight * halfHeight);
        float const quadratic = shape.cosine * shape.cosine * inverseX +
            shape.sine * shape.sine * inverseY;
        float const linear = 2.f * dy * shape.cosine * shape.sine * (inverseX - inverseY);
        float const constant = dy * dy *
            (shape.sine * shape.sine * inverseX + shape.cosine * shape.cosine * inverseY) - 1.f;
        float const discriminant = linear * linear - 4.f * quadratic * constant;
        if (discriminant < 0.f) return false;
        float const root = std::sqrt(discriminant);
        fromX = shape.x + (-linear - root) / (2.f * quadratic) - kSpanSlack;
        toX = shape.x + (-linear + root) / (2.f * quadratic) + kSpanSlack;
        return true;
    }

    float const halfWidth = shape.width * 0.5f;
    float const halfHeight = shape.height * 0.5f;
    std::array<Point, 4> corners{};
    int count = 0;
    auto corner = [&](float localX, float localY) {
        corners[static_cast<std::size_t>(count++)] = {
            shape.x + localX * shape.cosine - localY * shape.sine,
            shape.y + localX * shape.sine + localY * shape.cosine
        };
    };
    corner(-halfWidth, -halfHeight);
    corner(halfWidth, -halfHeight);
    if (shape.kind == PrimitiveKind::Triangle || shape.kind == PrimitiveKind::WideTriangle) {
        corner(-halfWidth, halfHeight);
    } else {
        corner(halfWidth, halfHeight);
        corner(-halfWidth, halfHeight);
    }

    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    for (int index = 0; index < count; ++index) {
        auto const& first = corners[static_cast<std::size_t>(index)];
        auto const& second = corners[static_cast<std::size_t>((index + 1) % count)];
        if (first.y == second.y) {
            if (first.y != y) continue;
            low = std::min({low, first.x, second.x});
            high = std::max({high, first.x, second.x});
            continue;
        }
        if (y < std::min(first.y, second.y) || y > std::max(first.y, second.y)) continue;
        float const along = (y - first.y) / (second.y - first.y);
        float const crossing = first.x + along * (second.x - first.x);
        low = std::min(low, crossing);
        high = std::max(high, crossing);
    }
    if (low > high) return false;
    fromX = low - kSpanSlack;
    toX = high + kSpanSlack;
    return true;
}

bool shapeStaysInside(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            if (permitted[static_cast<std::size_t>(y) * width + x]) continue;
            for (int sampleY = 0; sampleY < kFitSamples; ++sampleY) {
                for (int sampleX = 0; sampleX < kFitSamples; ++sampleX) {
                    if (placed.contains(
                            static_cast<float>(x) + (sampleX + 0.5f) / kFitSamples,
                            static_cast<float>(y) + (sampleY + 0.5f) / kFitSamples)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

float shapeSpill(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    int covered = 0;
    int spilled = 0;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            bool const allowed = permitted[static_cast<std::size_t>(y) * width + x] != 0;
            for (int sampleY = 0; sampleY < kFitSamples; ++sampleY) {
                for (int sampleX = 0; sampleX < kFitSamples; ++sampleX) {
                    if (!placed.contains(
                            static_cast<float>(x) + (sampleX + 0.5f) / kFitSamples,
                            static_cast<float>(y) + (sampleY + 0.5f) / kFitSamples)) {
                        continue;
                    }
                    ++covered;
                    spilled += !allowed;
                }
            }
        }
    }
    return covered > 0 ? static_cast<float>(spilled) / covered : 0.f;
}

} // namespace paimon::gifimport
