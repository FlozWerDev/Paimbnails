#include "GifStampCatalog.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

namespace paimon::gifimport {

namespace {

constexpr std::uint8_t kMaskFloor = 128;
// Mas alla de esto la busqueda deja de ganar nada y solo cuesta: la biblioteca
// de GD tiene muchas siluetas repetidas y el filtrado por firma se las come.
constexpr std::size_t kMaxVariants = 3072;

std::vector<StampVariant> g_variants;
bool g_fromGame = false;

StampMask rotateMask(StampMask const& mask, int quarters) {
    if (quarters == 0) return mask;
    StampMask turned;
    turned.width = quarters == 2 ? mask.width : mask.height;
    turned.height = quarters == 2 ? mask.height : mask.width;
    turned.coverage.assign(
        static_cast<std::size_t>(turned.width) * turned.height, 0);
    for (int y = 0; y < turned.height; ++y) {
        for (int x = 0; x < turned.width; ++x) {
            int sourceX = 0;
            int sourceY = 0;
            // Un giro de +90 en el convenio del trazador —donde la y crece hacia
            // abajo— es el que se ve girar en el sentido de las agujas.
            if (quarters == 1) {
                sourceX = y;
                sourceY = mask.height - 1 - x;
            } else if (quarters == 2) {
                sourceX = mask.width - 1 - x;
                sourceY = mask.height - 1 - y;
            } else {
                sourceX = mask.width - 1 - y;
                sourceY = x;
            }
            turned.coverage[static_cast<std::size_t>(y) * turned.width + x] =
                mask.coverage[static_cast<std::size_t>(sourceY) * mask.width + sourceX];
        }
    }
    return turned;
}

StampMask mirrorMask(StampMask const& mask) {
    StampMask mirrored = mask;
    for (int y = 0; y < mask.height; ++y) {
        auto* row = mirrored.coverage.data() + static_cast<std::size_t>(y) * mask.width;
        std::reverse(row, row + mask.width);
    }
    return mirrored;
}

std::uint64_t signatureOf(StampMask const& mask) {
    std::uint64_t bits = 0;
    for (int row = 0; row < kStampSignatureSide; ++row) {
        for (int column = 0; column < kStampSignatureSide; ++column) {
            int const x = (column * 2 + 1) * mask.width / (kStampSignatureSide * 2);
            int const y = (row * 2 + 1) * mask.height / (kStampSignatureSide * 2);
            if (mask.coverage[static_cast<std::size_t>(y) * mask.width + x] < kMaskFloor) {
                continue;
            }
            bits |= std::uint64_t{1} << (row * kStampSignatureSide + column);
        }
    }
    return bits;
}

std::uint64_t maskFingerprint(StampMask const& mask) {
    std::uint64_t hash = 1469598103934665603ull;
    for (auto value : mask.coverage) {
        hash ^= value >= kMaskFloor ? 1u : 0u;
        hash *= 1099511628211ull;
    }
    return hash;
}

StampMask analyticMask(PrimitiveKind kind) {
    StampMask mask;
    mask.width = kStampMaskSide;
    mask.height = kStampMaskSide;
    mask.coverage.assign(
        static_cast<std::size_t>(kStampMaskSide) * kStampMaskSide, 0);
    for (int y = 0; y < kStampMaskSide; ++y) {
        for (int x = 0; x < kStampMaskSide; ++x) {
            float const u = (x + 0.5f) / kStampMaskSide;
            float const v = (y + 0.5f) / kStampMaskSide;
            bool inside = true;
            if (kind == PrimitiveKind::Circle) {
                float const dx = u - 0.5f;
                float const dy = v - 0.5f;
                inside = dx * dx + dy * dy <= 0.25f;
            } else if (kind == PrimitiveKind::Triangle ||
                       kind == PrimitiveKind::WideTriangle) {
                inside = u + v <= 1.f;
            }
            mask.coverage[static_cast<std::size_t>(y) * kStampMaskSide + x] =
                inside ? 255 : 0;
        }
    }
    return mask;
}

std::vector<StampVariant> buildVariants(std::vector<CatalogEntry> const& entries) {
    std::vector<StampVariant> variants;
    std::unordered_set<std::uint64_t> seen;
    for (auto const& entry : entries) {
        if (entry.mask.empty() || entry.mask.width <= 0 || entry.mask.height <= 0) {
            continue;
        }
        for (int flip = 0; flip < 2; ++flip) {
            auto const based = flip ? mirrorMask(entry.mask) : entry.mask;
            for (int quarters = 0; quarters < 4; ++quarters) {
                auto turned = rotateMask(based, quarters);
                auto const fingerprint = maskFingerprint(turned);
                if (!seen.insert(fingerprint).second) continue;

                StampVariant variant;
                variant.signature = signatureOf(turned);
                variant.filled = std::popcount(variant.signature);
                if (variant.filled == 0) continue;

                bool const swapped = quarters == 1 || quarters == 3;
                variant.stamp.objectId = entry.objectId;
                variant.stamp.baseWidth = swapped ? entry.baseHeight : entry.baseWidth;
                variant.stamp.baseHeight = swapped ? entry.baseWidth : entry.baseHeight;
                variant.stamp.rotation = static_cast<float>(quarters) * 90.f;
                variant.stamp.flipX = flip != 0;
                // El desplazamiento viaja con el arte: voltear le cambia el signo
                // en x, y cada cuarto de vuelta lo gira como al resto del dibujo.
                float const shiftX = flip ? -entry.offsetX : entry.offsetX;
                float const shiftY = entry.offsetY;
                switch (quarters) {
                    case 1:
                        variant.stamp.offsetX = -shiftY;
                        variant.stamp.offsetY = shiftX;
                        break;
                    case 2:
                        variant.stamp.offsetX = -shiftX;
                        variant.stamp.offsetY = -shiftY;
                        break;
                    case 3:
                        variant.stamp.offsetX = shiftY;
                        variant.stamp.offsetY = -shiftX;
                        break;
                    default:
                        variant.stamp.offsetX = shiftX;
                        variant.stamp.offsetY = shiftY;
                        break;
                }
                variant.stamp.mask = std::move(turned);
                variants.push_back(std::move(variant));
                if (variants.size() >= kMaxVariants) return variants;
            }
        }
    }
    return variants;
}

} // namespace

std::vector<CatalogEntry> builtinStampCatalog() {
    std::vector<CatalogEntry> entries;
    entries.push_back({211, 30.f, 30.f, 0.f, 0.f, analyticMask(PrimitiveKind::Block)});
    entries.push_back({3637, 50.f, 50.f, 0.f, 0.f, analyticMask(PrimitiveKind::Circle)});
    entries.push_back({693, 30.f, 30.f, 0.f, 0.f, analyticMask(PrimitiveKind::Triangle)});
    entries.push_back({694, 60.f, 30.f, 0.f, 0.f, analyticMask(PrimitiveKind::WideTriangle)});
    return entries;
}

StampMask analyticRadialGlowMask() {
    StampMask mask;
    mask.width = kStampMaskSide;
    mask.height = kStampMaskSide;
    mask.coverage.assign(
        static_cast<std::size_t>(kStampMaskSide) * kStampMaskSide, 0);
    float const falloff = 1.f - std::exp(-4.f);
    for (int y = 0; y < kStampMaskSide; ++y) {
        for (int x = 0; x < kStampMaskSide; ++x) {
            float const u = (x + 0.5f) / kStampMaskSide;
            float const v = (y + 0.5f) / kStampMaskSide;
            float const radius2 = 4.f * ((u - 0.5f) * (u - 0.5f) +
                (v - 0.5f) * (v - 0.5f));
            float const alpha = std::max(0.f,
                (std::exp(-4.f * radius2) - std::exp(-4.f)) / falloff);
            mask.coverage[static_cast<std::size_t>(y) * kStampMaskSide + x] =
                static_cast<std::uint8_t>(std::lround(alpha * 255.f));
        }
    }
    return mask;
}

StampMask analyticVerticalGradientMask() {
    StampMask mask;
    mask.width = kStampMaskSide;
    mask.height = kStampMaskSide;
    mask.coverage.assign(
        static_cast<std::size_t>(kStampMaskSide) * kStampMaskSide, 0);
    for (int y = 0; y < kStampMaskSide; ++y) {
        for (int x = 0; x < kStampMaskSide; ++x) {
            float const v = (y + 0.5f) / kStampMaskSide;
            mask.coverage[static_cast<std::size_t>(y) * kStampMaskSide + x] =
                static_cast<std::uint8_t>(std::lround((1.f - v) * 255.f));
        }
    }
    return mask;
}

StampMask analyticQuarterGlowMask() {
    StampMask mask;
    mask.width = kStampMaskSide;
    mask.height = kStampMaskSide;
    mask.coverage.assign(
        static_cast<std::size_t>(kStampMaskSide) * kStampMaskSide, 0);
    for (int y = 0; y < kStampMaskSide; ++y) {
        for (int x = 0; x < kStampMaskSide; ++x) {
            float const u = (x + 0.5f) / kStampMaskSide;
            float const v = (y + 0.5f) / kStampMaskSide;
            float const alpha = std::pow(std::max(0.f, 1.f -
                std::sqrt((1.f - u) * (1.f - u) + (1.f - v) * (1.f - v))), 1.3f);
            mask.coverage[static_cast<std::size_t>(y) * kStampMaskSide + x] =
                static_cast<std::uint8_t>(std::lround(alpha * 255.f));
        }
    }
    return mask;
}

void setStampCatalog(std::vector<CatalogEntry> entries) {
    g_fromGame = !entries.empty();
    g_variants = buildVariants(g_fromGame ? entries : builtinStampCatalog());
}

std::vector<StampVariant> const& stampVariants() {
    if (!g_variants.empty()) return g_variants;
    // El trazado pide la biblioteca desde varios hilos a la vez, y el repuesto se
    // arma en la primera llamada: como estatico local la inicializacion la
    // serializa el propio lenguaje, que es lo unico que hace segura esa carrera.
    static std::vector<StampVariant> const fallback = buildVariants(builtinStampCatalog());
    return fallback;
}

bool hasStampCatalog() {
    return g_fromGame;
}

} // namespace paimon::gifimport
