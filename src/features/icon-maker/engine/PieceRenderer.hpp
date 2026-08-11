#pragma once
// Renderiza un slot ("zona de color") de un proyecto: coloca cada pieza en el
// lienzo, le aplica su relleno y las compone en orden.

#include "../data/IconProject.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>

namespace paimon::icon_maker {

class PieceRenderer final {
public:
    // Canvas is square (anatomy canvasUhd). Missing piece files are skipped
    // with a log instead of failing the whole slot.
    static geode::Result<texture_studio::ImageBuffer> renderSlot(
        IconProject const& project,
        std::string const& slotKey,
        int canvasSize,
        std::filesystem::path const& imagesDir);

    // One placed + filled piece on a canvasSize canvas.
    static geode::Result<texture_studio::ImageBuffer> renderPiece(
        IconPiece const& piece,
        int canvasSize,
        int guideSize,
        std::filesystem::path const& imagesDir);

private:
    PieceRenderer() = delete;
};

}  // namespace paimon::icon_maker
