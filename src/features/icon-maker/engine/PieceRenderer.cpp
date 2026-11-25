#include "PieceRenderer.hpp"

#include "FillRenderer.hpp"
#include "ShapeGenerator.hpp"
#include "../data/IconAnatomy.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

// squash direction is free (renderCustomImage refits either way); shrinking keeps the buffer small.
ts::ImageBuffer applyAxisScale(ts::ImageBuffer source, float scaleX, float scaleY) {
    float const longest = std::max(scaleX, scaleY);
    if (longest <= 0.f) return source;

    float const fx = scaleX / longest;
    float const fy = scaleY / longest;
    if (std::fabs(fx - 1.f) < 1e-3f && std::fabs(fy - 1.f) < 1e-3f) return source;

    int const w = std::max(8, static_cast<int>(std::lround(source.width() * fx)));
    int const h = std::max(8, static_cast<int>(std::lround(source.height() * fy)));
    return source.resizedBilinear(w, h);
}

std::vector<std::uint8_t> alphaMask(ts::ImageBuffer const& pixels, int maskSize) {
    // "small" is a windows headers macro; never name a local that.
    auto scaled = pixels.resizedBilinear(maskSize, maskSize);
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(maskSize) * maskSize, 0);
    auto const* src = scaled.data();
    for (std::size_t i = 0; i < mask.size(); ++i) {
        mask[i] = src[i * ts::ImageBuffer::kBytesPerPixel + 3];
    }
    return mask;
}

}  // namespace

geode::Result<ts::ImageBuffer> PieceRenderer::renderPiece(
        IconPiece const& piece, int canvasSize, float guideRatio,
        float canvasScale, std::filesystem::path const& imagesDir) {
    ts::ImageBuffer source;
    bool scaleToGuide = false;

    if (piece.shape.kind == PieceShape::Kind::Primitive ||
        piece.shape.kind == PieceShape::Kind::Decal) {
        // Rasterize procedural SDF shapes at crisp high resolution for this canvas.
        int const genSide = std::clamp(
            static_cast<int>(std::lround(static_cast<float>(canvasSize) * std::max(guideRatio, 0.5f) * 1.5f)),
            96, 360);
        source = ShapeGenerator::rasterize(piece.shape, genSide);
        scaleToGuide = true;
    } else {
        if (piece.shape.file.empty()) {
            return Err("La pieza '{}' no tiene forma", piece.name);
        }
        auto loaded = ts::ImageBuffer::loadFromFile(imagesDir / piece.shape.file);
        if (!loaded) {
            return Err("Forma '{}': {}", piece.shape.file, loaded.unwrapErr());
        }
        source = loaded.unwrap();
        scaleToGuide = (piece.shape.kind == PieceShape::Kind::Import);
    }

    auto shape = applyAxisScale(std::move(source), piece.scaleX, piece.scaleY);
    auto transform = piece.transform;
    if (scaleToGuide && guideRatio > 0.f) {
        transform.scale *= guideRatio;
    }

    auto placed = ts::SpritePreviewRenderer::renderCustomImage(
        shape, canvasSize, canvasSize, transform);
    if (placed.empty()) {
        return Ok(ts::ImageBuffer(canvasSize, canvasSize));
    }

    auto filled = FillRenderer::apply(placed, piece.fill, imagesDir, canvasScale);
    if (!filled) return filled;

    // Layer stack: outer shadow/glow (back) -> outline (middle) -> painted fill (front).
    auto shadow = FillRenderer::renderShadow(
        placed, piece.fill.shadow, transform.opacity, canvasScale);
    auto outline = FillRenderer::renderOutline(
        placed, piece.fill.outline, transform.opacity, canvasScale);

    if (shadow.empty() && outline.empty()) {
        return filled;
    }

    ts::ImageBuffer combined(canvasSize, canvasSize);
    if (!shadow.empty())  ts::SpritePreviewRenderer::compositeOver(combined, shadow);
    if (!outline.empty()) ts::SpritePreviewRenderer::compositeOver(combined, outline);
    ts::SpritePreviewRenderer::compositeOver(combined, filled.unwrap());
    return Ok(std::move(combined));
}

geode::Result<ts::ImageBuffer> PieceRenderer::renderSlot(
        IconProject const& project, std::string const& slotKey,
        int canvasSize, std::filesystem::path const& imagesDir) {
    ts::ImageBuffer canvas(canvasSize, canvasSize);

    auto it = project.slots.find(slotKey);
    if (it == project.slots.end()) return Ok(std::move(canvas));

    auto const* anatomy = anatomyFor(project.type);
    int const refCanvas = anatomy && anatomy->canvasUhd > 0 ? anatomy->canvasUhd : 240;
    int const refGuide = anatomy && anatomy->guideUhd > 0 ? anatomy->guideUhd : 120;
    float const guideRatio = static_cast<float>(refGuide) / static_cast<float>(refCanvas);
    float const canvasScale = static_cast<float>(canvasSize) / static_cast<float>(refCanvas);

    for (auto const& piece : it->second.pieces) {
        if (!piece.visible) continue;
        auto rendered = renderPiece(piece, canvasSize, guideRatio, canvasScale, imagesDir);
        if (!rendered) {
            log::warn("[icon-maker] pieza '{}' en slot '{}' omitida: {}",
                piece.name, slotKey, rendered.unwrapErr());
            continue;
        }
        FillRenderer::compositeBlend(canvas, rendered.unwrap(), piece.fill.blendMode);
    }
    return Ok(std::move(canvas));
}

SlotRender PieceRenderer::renderSlotDetailed(
        IconProject const& project, std::string const& slotKey,
        int canvasSize, int maskSize, std::filesystem::path const& imagesDir) {
    SlotRender out;
    out.composite = ts::ImageBuffer(canvasSize, canvasSize);

    auto it = project.slots.find(slotKey);
    if (it == project.slots.end()) return out;

    auto const* anatomy = anatomyFor(project.type);
    int const refCanvas = anatomy && anatomy->canvasUhd > 0 ? anatomy->canvasUhd : 240;
    int const refGuide = anatomy && anatomy->guideUhd > 0 ? anatomy->guideUhd : 120;
    float const guideRatio = static_cast<float>(refGuide) / static_cast<float>(refCanvas);
    float const canvasScale = static_cast<float>(canvasSize) / static_cast<float>(refCanvas);

    auto const& pieces = it->second.pieces;
    out.pieces.reserve(pieces.size());

    for (std::size_t i = 0; i < pieces.size(); ++i) {
        auto const& piece = pieces[i];
        auto rendered = renderPiece(piece, canvasSize, guideRatio, canvasScale, imagesDir);
        if (!rendered) {
            log::warn("[icon-maker] pieza '{}' en slot '{}' omitida: {}",
                piece.name, slotKey, rendered.unwrapErr());
            continue;
        }

        PieceRender entry;
        entry.index = static_cast<int>(i);
        entry.pieceId = piece.id;
        entry.visible = piece.visible;
        entry.pixels = rendered.unwrap();
        entry.canvasSize = canvasSize;
        entry.hasBounds = FillRenderer::alphaBounds(entry.pixels,
            entry.boundsX, entry.boundsY, entry.boundsW, entry.boundsH);

        if (piece.visible) {
            entry.mask = alphaMask(entry.pixels, maskSize);
            entry.maskSize = maskSize;
            FillRenderer::compositeBlend(out.composite, entry.pixels, piece.fill.blendMode);
        }
        out.pieces.push_back(std::move(entry));
    }
    return out;
}

}  // namespace paimon::icon_maker
