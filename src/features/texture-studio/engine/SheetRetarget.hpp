#pragma once

#include "../data/SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// The copy of <modid>/<file> the game reads today.
struct InstalledSheet {
    std::filesystem::path pngPath;
    std::filesystem::path plistPath;
};

// Mod and Geode sheets ship PNG-only, so the atlas the pack writes has to match
// the plist that stays installed. PackGen serves one snapshot per sheet, and a
// mod update repacks its atlas without renaming the file: every frame rect then
// points at the wrong pixels and the whole sheet renders deformed. Retargeting
// moves each tinted frame to the slot the installed plist expects.
struct RetargetOutcome {
    enum class Status {
        NotInstalled,   // nothing to compare against; ship as-is.
        LayoutMatches,  // snapshot and installed sheet agree; ship as-is.
        Retargeted,     // pngBytes rebuilt in the installed layout.
        Failed,         // installed sheet unreadable; caller should drop the sheet.
    };

    Status status = Status::NotInstalled;
    std::vector<std::uint8_t> pngBytes;
    int matchedFrames = 0;
    int missingFrames = 0;
    std::string message;
};

class SheetRetarget final {
public:
    // Empty for vanilla sheets (no modid prefix) and uninstalled mods.
    static std::optional<InstalledSheet> locate(std::string const& pngRel);

    static bool sameLayout(ParsedSpritesheet const& a, ParsedSpritesheet const& b);

    static RetargetOutcome conform(std::vector<std::uint8_t> const& processedPng,
                                   std::filesystem::path const& sourcePlist,
                                   std::string const& pngRel);

private:
    SheetRetarget() = delete;
};

}
