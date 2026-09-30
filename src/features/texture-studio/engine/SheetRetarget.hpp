#pragma once

#include "../data/SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// the copy of <modid>/<file> the game reads today.
struct InstalledSheet {
    std::filesystem::path pngPath;
    std::filesystem::path plistPath;
};

// mod updates repack without renaming, so stored rects go stale; retargeting moves frames to the installed layout.
struct RetargetOutcome {
    enum class Status {
        NotInstalled,   // nothing to compare against; ship as-is.
        LayoutMatches,  // snapshot and installed sheet agree; ship as-is.
        Retargeted,     // pngbytes rebuilt in the installed layout.
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
    // empty for vanilla sheets (no modid prefix) and uninstalled mods.
    static std::optional<InstalledSheet> locate(std::string const& pngRel);

    static bool sameLayout(ParsedSpritesheet const& a, ParsedSpritesheet const& b);

    static RetargetOutcome conform(std::vector<std::uint8_t> const& processedPng,
                                   std::filesystem::path const& sourcePlist,
                                   std::string const& pngRel);

private:
    SheetRetarget() = delete;
};

}
