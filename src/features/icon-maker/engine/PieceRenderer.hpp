#pragma once
// renders one project slot ("color zone"): places pieces, fills, composites in order.

#include "../data/IconProject.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace paimon::icon_maker {

struct PieceRender {
    int index = -1;
    std::string pieceId;
    bool visible = true;
    texture_studio::ImageBuffer pixels;

    std::vector<std::uint8_t> mask;
    int maskSize = 0;

    // canvas side the box was measured on; editor frees `pixels` after thumbnailing.
    int canvasSize = 0;

    // canvas pixels, rows running top-down like the buffer.
    int boundsX = 0;
    int boundsY = 0;
    int boundsW = 0;
    int boundsH = 0;
    bool hasBounds = false;
};

struct SlotRender {
    texture_studio::ImageBuffer composite;
    std::vector<PieceRender> pieces;
};

class PieceRenderer final {
public:
    // canvas is square (anatomy canvasuhd). missing piece files are skipped
    // with a log instead of failing the whole slot.
    static geode::Result<texture_studio::ImageBuffer> renderSlot(
        IconProject const& project,
        std::string const& slotKey,
        int canvasSize,
        std::filesystem::path const& imagesDir);

    // hidden pieces retain thumbnails but have no composite contribution or hit mask.
    static SlotRender renderSlotDetailed(
        IconProject const& project,
        std::string const& slotKey,
        int canvasSize,
        int maskSize,
        std::filesystem::path const& imagesDir);

    // one placed + filled piece on a canvassize canvas.
    static geode::Result<texture_studio::ImageBuffer> renderPiece(
        IconPiece const& piece,
        int canvasSize,
        int guideSize,
        std::filesystem::path const& imagesDir);

private:
    PieceRenderer() = delete;
};

}  // namespace paimon::icon_maker
