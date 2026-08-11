#include "PieceRenderer.hpp"

#include "FillRenderer.hpp"
#include "../data/IconAnatomy.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

geode::Result<ts::ImageBuffer> PieceRenderer::renderPiece(
        IconPiece const& piece, int canvasSize, int guideSize,
        std::filesystem::path const& imagesDir) {
    if (piece.shape.file.empty()) {
        return Err("La pieza '{}' no tiene forma", piece.name);
    }

    auto loaded = ts::ImageBuffer::loadFromFile(imagesDir / piece.shape.file);
    if (!loaded) {
        return Err("Forma '{}': {}", piece.shape.file, loaded.unwrapErr());
    }

    auto transform = piece.transform;
    if (piece.shape.kind == PieceShape::Kind::Import && canvasSize > 0 && guideSize > 0) {
        transform.scale *= static_cast<float>(guideSize) / static_cast<float>(canvasSize);
    }

    auto placed = ts::SpritePreviewRenderer::renderCustomImage(
        loaded.unwrap(), canvasSize, canvasSize, transform);
    if (placed.empty()) {
        return Ok(ts::ImageBuffer(canvasSize, canvasSize));
    }

    auto filled = FillRenderer::apply(placed, piece.fill, imagesDir);
    if (!filled) return filled;

    // The contour is grown from the same silhouette and sits behind the paint.
    auto outline = FillRenderer::renderOutline(
        placed, piece.fill.outline, transform.opacity);
    if (outline.empty()) return filled;

    ts::SpritePreviewRenderer::compositeOver(outline, filled.unwrap());
    return Ok(std::move(outline));
}

geode::Result<ts::ImageBuffer> PieceRenderer::renderSlot(
        IconProject const& project, std::string const& slotKey,
        int canvasSize, std::filesystem::path const& imagesDir) {
    ts::ImageBuffer canvas(canvasSize, canvasSize);

    auto it = project.slots.find(slotKey);
    if (it == project.slots.end()) return Ok(std::move(canvas));

    auto const* anatomy = anatomyFor(project.type);
    int guideSize = anatomy ? anatomy->guideUhd : canvasSize;

    for (auto const& piece : it->second.pieces) {
        if (!piece.visible) continue;
        auto rendered = renderPiece(piece, canvasSize, guideSize, imagesDir);
        if (!rendered) {
            log::warn("[icon-maker] pieza '{}' en slot '{}' omitida: {}",
                piece.name, slotKey, rendered.unwrapErr());
            continue;
        }
        ts::SpritePreviewRenderer::compositeOver(canvas, rendered.unwrap());
    }
    return Ok(std::move(canvas));
}

}  // namespace paimon::icon_maker
