#include "../../../utils/FilesystemPath.hpp"
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
#include <charconv>
#include <cmath>
#include <vector>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

// a rendered frame trimmed out of its authoring canvas. trim sizes are kept
// multiples of 4 so the /2 (hd) and /4 (sd) variants stay integral.
struct RenderedFrame {
    std::string name;        // final frame name, e.g. "micubo_glow_001.png"
    ts::ImageBuffer pixels;  // trimmed
    float centerOffX = 0.f;  // trimmed-center - canvas-center, +y up (plist)
    float centerOffY = 0.f;
    int canvasSize = 0;
    bool placeholder = false;
};

int ceil4(int v) {
    return (v + 3) & ~3;
}

// Bleed edge RGB into adjacent alpha==0 pixels (2 passes) so OpenGL linear
// filtering never interpolates transparent black halos at sprite silhouettes.
void bleedTransparentRGB(ts::ImageBuffer& img) {
    int const w = img.width();
    int const h = img.height();
    if (w <= 1 || h <= 1) return;

    std::vector<std::uint8_t> copy(img.data(), img.data() + img.byteSize());
    for (int pass = 0; pass < 2; ++pass) {
        auto* dst = img.data();
        bool changed = false;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                std::size_t const idx = (static_cast<std::size_t>(y) * w + x) * 4;
                if (copy[idx + 3] > 0) continue;

                int sr = 0, sg = 0, sb = 0, count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    int const ny = y + dy;
                    if (ny < 0 || ny >= h) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        int const nx = x + dx;
                        if (nx < 0 || nx >= w || (dx == 0 && dy == 0)) continue;
                        std::size_t const nidx = (static_cast<std::size_t>(ny) * w + nx) * 4;
                        if (copy[nidx + 3] > 0) {
                            sr += copy[nidx + 0];
                            sg += copy[nidx + 1];
                            sb += copy[nidx + 2];
                            ++count;
                        }
                    }
                }
                if (count > 0) {
                    dst[idx + 0] = static_cast<std::uint8_t>(sr / count);
                    dst[idx + 1] = static_cast<std::uint8_t>(sg / count);
                    dst[idx + 2] = static_cast<std::uint8_t>(sb / count);
                    dst[idx + 3] = 0;
                    changed = true;
                }
            }
        }
        if (!changed) break;
        std::copy(img.data(), img.data() + img.byteSize(), copy.begin());
    }
}

// trim `canvas` to its alpha bbox expanded to multiples of 4, staying inside
// the canvas. returns false when fully transparent.
bool trimFrame(ts::ImageBuffer const& canvas, ts::ImageBuffer& outPixels,
               float& outOffX, float& outOffY) {
    int bx = 0, by = 0, bw = 0, bh = 0;
    if (!FillRenderer::alphaBounds(canvas, bx, by, bw, bh)) return false;

    int w = canvas.width();
    int h = canvas.height();
    int tw = std::min(ceil4(bw), w);
    int th = std::min(ceil4(bh), h);

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
    // pixel rows grow downward; plist offsets are +y up.
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
    // scale every frame for this quality; multiples of 4 guarantee integers.
    std::vector<ts::ImageBuffer> scaled;
    scaled.reserve(frames.size());
    std::vector<ts::RectPackInput> inputs;
    inputs.reserve(frames.size());

    for (std::size_t i = 0; i < frames.size(); ++i) {
        auto const& frame = frames[i];
        int sw = std::max(1, static_cast<int>(std::lround(frame.pixels.width() * factor)));
        int sh = std::max(1, static_cast<int>(std::lround(frame.pixels.height() * factor)));
        auto buf = factor == 1.f
            ? frame.pixels
            : frame.pixels.resizedBilinear(sw, sh);
        if (!frame.placeholder) bleedTransparentRGB(buf);
        scaled.push_back(std::move(buf));
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
    sheet.metadata.textureFileName = paimon::paths::toUtf8(pngPath.filename());
    sheet.metadata.realTextureFileName = sheet.metadata.textureFileName;
    sheet.metadata.premultiplyAlpha = false;

    for (auto const& placement : packed.placements) {
        std::size_t idx = 0;
        auto const end = placement.id.data() + placement.id.size();
        auto const parsed = std::from_chars(placement.id.data(), end, idx);
        if (parsed.ec != std::errc{} || parsed.ptr != end || idx >= frames.size()) {
            return Err("indice de sprite invalido");
        }
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
    std::error_code stagingError;
    auto staging = paimon::file::StagedDirectory::create(
        IconPaths::rootDir() / "build-staging", stagingError);
    if (!staging) return Err("No se pudo preparar la compilacion: {}", stagingError.message());
    auto const& outputDir = staging->path();

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
    result.staging = std::move(staging);

    auto out = [&](std::string const& suffix) {
        return std::pair{
            outputDir / (project.id + suffix + ".png"),
            outputDir / (project.id + suffix + ".plist"),
        };
    };

    struct QualityRow {
        char const* suffix;
        float scale;
        int gap;
        char const* label;
        CompiledQuality CompiledIcon::* member;
    };
    constexpr QualityRow kQualities[] = {
        {"-uhd", 1.f,   4, "UHD", &CompiledIcon::uhd},
        {"-hd",  0.5f,  2, "HD",  &CompiledIcon::hd},
        {"",     0.25f, 1, "SD",  &CompiledIcon::sd},
    };
    for (auto const& quality : kQualities) {
        auto [png, plist] = out(quality.suffix);
        auto r = writeQuality(frames, quality.scale, quality.gap, png, plist);
        if (!r) return Err("{}: {}", quality.label, r.unwrapErr());
        result.*(quality.member) = r.unwrap();
    }

    return Ok(std::move(result));
}

}  // namespace paimon::icon_maker
