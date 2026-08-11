#include "IconCompiler.hpp"

#include "FillRenderer.hpp"
#include "PieceRenderer.hpp"
#include "../data/IconAnatomy.hpp"
#include "../persist/IconPaths.hpp"
#include "../../texture-studio/data/PlistBuilder.hpp"
#include "../../texture-studio/data/RectPacker.hpp"
#include "../../texture-studio/data/SpriteFrameInfo.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

// A rendered frame trimmed out of its authoring canvas. Trim sizes are kept
// multiples of 4 so the ÷2 (hd) and ÷4 (sd) variants stay integral.
struct RenderedFrame {
    std::string name;        // final frame name, e.g. "micubo_glow_001.png"
    ts::ImageBuffer pixels;  // trimmed
    float centerOffX = 0.f;  // trimmed-center − canvas-center, +Y up (plist)
    float centerOffY = 0.f;
    int canvasSize = 0;
    bool placeholder = false;
};

int ceil4(int v) {
    return (v + 3) & ~3;
}

// Trim `canvas` to its alpha bbox expanded to multiples of 4, staying inside
// the canvas. Returns false when fully transparent.
bool trimFrame(ts::ImageBuffer const& canvas, ts::ImageBuffer& outPixels,
               float& outOffX, float& outOffY) {
    int bx = 0, by = 0, bw = 0, bh = 0;
    if (!FillRenderer::alphaBounds(canvas, bx, by, bw, bh)) return false;

    int w = canvas.width();
    int h = canvas.height();
    int tw = std::min(ceil4(bw), w);
    int th = std::min(ceil4(bh), h);

    // Center the expansion, then clamp into the canvas.
    int tx = bx - (tw - bw) / 2;
    int ty = by - (th - bh) / 2;
    tx = std::clamp(tx, 0, w - tw);
    ty = std::clamp(ty, 0, h - th);

    outPixels = canvas.subRect(tx, ty, tw, th);

    float canvasCx = static_cast<float>(w) * 0.5f;
    float canvasCy = static_cast<float>(h) * 0.5f;
    float frameCx = static_cast<float>(tx) + static_cast<float>(tw) * 0.5f;
    float frameCy = static_cast<float>(ty) + static_cast<float>(th) * 0.5f;

    outOffX = frameCx - canvasCx;
    // Pixel rows grow downward; plist offsets are +Y up.
    outOffY = canvasCy - frameCy;
    return true;
}

ts::ImageBuffer makePlaceholder() {
    // 4x4 transparent frame so required slots always exist in the sheet.
    return ts::ImageBuffer(4, 4);
}

geode::Result<CompiledQuality> writeQuality(
        std::vector<RenderedFrame> const& frames,
        float factor, int gap,
        std::filesystem::path const& pngPath,
        std::filesystem::path const& plistPath) {
    // Scale every frame for this quality; multiples of 4 guarantee integers.
    std::vector<ts::ImageBuffer> scaled;
    scaled.reserve(frames.size());
    std::vector<ts::RectPackInput> inputs;
    inputs.reserve(frames.size());

    for (std::size_t i = 0; i < frames.size(); ++i) {
        auto const& frame = frames[i];
        int sw = std::max(1, static_cast<int>(std::lround(frame.pixels.width() * factor)));
        int sh = std::max(1, static_cast<int>(std::lround(frame.pixels.height() * factor)));
        scaled.push_back(factor == 1.f
            ? frame.pixels
            : frame.pixels.resizedBilinear(sw, sh));
        inputs.push_back({std::to_string(i), sw, sh});
    }

    auto packed = ts::RectPacker::pack(std::move(inputs), {gap, 4096});
    if (packed.sheetWidth <= 0 || packed.sheetHeight <= 0) {
        return Err("empaquetado vacio");
    }

    ts::ImageBuffer atlas(packed.sheetWidth, packed.sheetHeight);
    ts::ParsedSpritesheet sheet;
    sheet.metadata.format = 3;
    sheet.metadata.sizeW = packed.sheetWidth;
    sheet.metadata.sizeH = packed.sheetHeight;
    sheet.metadata.textureFileName = geode::utils::string::pathToString(pngPath.filename());
    sheet.metadata.realTextureFileName = sheet.metadata.textureFileName;
    sheet.metadata.premultiplyAlpha = false;

    for (auto const& placement : packed.placements) {
        std::size_t idx = static_cast<std::size_t>(std::stoul(placement.id));
        auto const& frame = frames[idx];
        auto const& pixels = scaled[idx];

        atlas.blitOverwrite(placement.x, placement.y, pixels);

        ts::SpriteFrameInfo info;
        info.name = frame.name;
        info.rectX = placement.x;
        info.rectY = placement.y;
        info.rectW = pixels.width();
        info.rectH = pixels.height();
        info.spriteW = pixels.width();
        info.spriteH = pixels.height();
        info.offsetX = frame.centerOffX * factor;
        info.offsetY = frame.centerOffY * factor;
        info.sourceW = std::max(1, static_cast<int>(std::lround(frame.canvasSize * factor)));
        info.sourceH = info.sourceW;
        info.rotated = false;
        sheet.frames.push_back(std::move(info));
    }

    if (auto r = atlas.saveToPng(pngPath); !r) {
        return Err("guardar png: {}", r.unwrapErr());
    }
    if (auto r = ts::PlistBuilder::buildFile(sheet, plistPath); !r) {
        return Err("guardar plist: {}", r.unwrapErr());
    }
    return Ok(CompiledQuality{pngPath, plistPath});
}

}  // anonymous namespace

geode::Result<CompiledIcon> IconCompiler::compile(IconProject const& project) {
    auto const* def = anatomyFor(project.type);
    if (!def) return Err("Tipo de icono no soportado");
    if (project.id.empty()) return Err("Proyecto sin id");

    auto imagesDir = IconPaths::imagesDir(project.id);
    auto outputDir = IconPaths::outputDir(project.id);
    if (auto r = IconPaths::ensureSlotDirs(project.id); !r) {
        return Err(r.unwrapErr());
    }

    std::vector<RenderedFrame> frames;
    bool anyContent = false;

    int const firstPart = def->partCount > 1 ? 1 : 0;
    int const lastPart = def->partCount > 1 ? def->partCount : 0;

    for (int part = firstPart; part <= lastPart; ++part) {
        for (auto const& slot : def->slots) {
            // "extra" only exists on part 1 of multi-part modes.
            if (def->partCount > 1 && part > 1 && slot.key == "extra") continue;

            auto slotKey = slotStorageKey(part, slot.key);
            auto rendered = PieceRenderer::renderSlot(
                project, slotKey, def->canvasUhd, imagesDir);
            if (!rendered) {
                return Err("Slot '{}': {}", slotKey, rendered.unwrapErr());
            }

            RenderedFrame frame;
            frame.name = frameName(project.id, project.type, part, slot.key);
            frame.canvasSize = def->canvasUhd;

            if (trimFrame(rendered.unwrap(), frame.pixels,
                          frame.centerOffX, frame.centerOffY)) {
                anyContent = true;
            } else {
                if (slot.optional) continue;  // optional empty: omit the frame
                frame.pixels = makePlaceholder();
                frame.centerOffX = 0.f;
                frame.centerOffY = 0.f;
                frame.placeholder = true;
            }
            frames.push_back(std::move(frame));
        }
    }

    if (!anyContent) {
        return Err("El icono esta vacio: agrega al menos una capa con contenido");
    }

    CompiledIcon result;
    result.exportName = project.id;

    auto out = [&](std::string const& suffix) {
        return std::pair{
            outputDir / (project.id + suffix + ".png"),
            outputDir / (project.id + suffix + ".plist"),
        };
    };

    {
        auto [png, plist] = out("-uhd");
        auto r = writeQuality(frames, 1.f, 4, png, plist);
        if (!r) return Err("UHD: {}", r.unwrapErr());
        result.uhd = r.unwrap();
    }
    {
        auto [png, plist] = out("-hd");
        auto r = writeQuality(frames, 0.5f, 2, png, plist);
        if (!r) return Err("HD: {}", r.unwrapErr());
        result.hd = r.unwrap();
    }
    {
        auto [png, plist] = out("");
        auto r = writeQuality(frames, 0.25f, 1, png, plist);
        if (!r) return Err("SD: {}", r.unwrapErr());
        result.sd = r.unwrap();
    }

    return Ok(std::move(result));
}

}  // namespace paimon::icon_maker
