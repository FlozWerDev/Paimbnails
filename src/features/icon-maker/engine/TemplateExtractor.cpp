#include "TemplateExtractor.hpp"

#include "../../texture-studio/data/GdResourcesLocator.hpp"
#include "../../texture-studio/data/SpritesheetReader.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <system_error>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

std::string_view vanillaPrefix(IconType type) {
    switch (type) {
        case IconType::Cube:    return "player_";
        case IconType::Ship:    return "ship_";
        case IconType::Ball:    return "player_ball_";
        case IconType::Ufo:     return "bird_";
        case IconType::Wave:    return "dart_";
        case IconType::Robot:   return "robot_";
        case IconType::Spider:  return "spider_";
        case IconType::Swing:   return "swing_";
        case IconType::Jetpack: return "jetpack_";
        default:                return "player_";
    }
}

struct SheetPick {
    std::filesystem::path plist;
    std::filesystem::path png;
    int upscale = 1;  // 1 = uhd, 2 = hd, 4 = sd
};

geode::Result<SheetPick> pickSheet(std::string const& base) {
    auto iconsDir = ts::GdResourcesLocator::resourcesDir() / "icons";

    struct Candidate { char const* suffix; int upscale; };
    constexpr Candidate kCandidates[] = {
        {"-uhd", 1},
        {"-hd", 2},
        {"", 4},
    };

    std::error_code ec;
    for (auto const& c : kCandidates) {
        SheetPick pick;
        pick.plist = iconsDir / (base + c.suffix + ".plist");
        pick.png   = iconsDir / (base + c.suffix + ".png");
        pick.upscale = c.upscale;
        if (std::filesystem::exists(pick.plist, ec) &&
            std::filesystem::exists(pick.png, ec)) {
            return Ok(std::move(pick));
        }
    }
    return Err("No se encontro la hoja de '{}' en los recursos de GD", base);
}

// Embed `logical` centered into a canvasSize square, upscaling low-quality
// sources so every template lands at UHD pixel scale.
ts::ImageBuffer embedCentered(ts::ImageBuffer logical, int upscale, int canvasSize) {
    if (upscale > 1 && !logical.empty()) {
        logical = logical.resizedBilinear(
            logical.width() * upscale, logical.height() * upscale);
    }
    ts::ImageBuffer canvas(canvasSize, canvasSize);
    if (logical.empty()) return canvas;

    if (logical.width() > canvasSize || logical.height() > canvasSize) {
        // Extremely large art (shouldn't happen with vanilla icons): scale to fit.
        float s = std::min(
            static_cast<float>(canvasSize) / logical.width(),
            static_cast<float>(canvasSize) / logical.height());
        logical = logical.resizedBilinear(
            std::max(1, static_cast<int>(logical.width() * s)),
            std::max(1, static_cast<int>(logical.height() * s)));
    }

    canvas.blitOverwrite(
        (canvasSize - logical.width()) / 2,
        (canvasSize - logical.height()) / 2,
        logical);
    return canvas;
}

}  // anonymous namespace

std::string TemplateExtractor::sheetBase(IconType type, int iconId) {
    return fmt::format("{}{:02}", vanillaPrefix(type), iconId);
}

geode::Result<std::vector<TemplateFrame>> TemplateExtractor::extract(
        IconType type, int iconId, int canvasSize) {
    auto base = sheetBase(type, iconId);
    GEODE_UNWRAP_INTO(auto pick, pickSheet(base));
    GEODE_UNWRAP_INTO(auto sheet, ts::SpritesheetReader::loadFromPaths(pick.plist, pick.png));

    std::vector<TemplateFrame> out;
    for (auto const& frame : sheet.frames) {
        // "player_01_glow_001.png" -> "_glow_001"
        auto const& name = frame.info.name;
        if (name.size() <= base.size() + 4 || name.rfind(base, 0) != 0) continue;
        std::string suffix = name.substr(base.size(), name.size() - base.size() - 4);

        auto logical = ts::SpritesheetReader::composeLogicalFrame(frame.pixels, frame.info);
        TemplateFrame tf;
        tf.suffix = std::move(suffix);
        tf.pixels = embedCentered(std::move(logical), pick.upscale, canvasSize);
        out.push_back(std::move(tf));
    }

    if (out.empty()) {
        return Err("La hoja de '{}' no tiene frames utilizables", base);
    }
    return Ok(std::move(out));
}

geode::Result<ts::ImageBuffer> TemplateExtractor::extractFrame(
        IconType type, int iconId, std::string_view suffix, int canvasSize) {
    GEODE_UNWRAP_INTO(auto frames, extract(type, iconId, canvasSize));
    for (auto& frame : frames) {
        if (frame.suffix == suffix) return Ok(std::move(frame.pixels));
    }
    return Err("Frame '{}' no encontrado en {}", suffix, sheetBase(type, iconId));
}

}  // namespace paimon::icon_maker
