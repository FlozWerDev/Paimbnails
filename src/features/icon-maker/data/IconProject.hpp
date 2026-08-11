#pragma once

#include "FillSpec.hpp"
#include "../../texture-studio/data/ImageTransform.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace paimon::icon_maker {

struct PieceShape {
    // Frozen values.
    enum class Kind : int { Import = 0, Template = 1 };

    Kind kind = Kind::Import;

    // Import: filename inside the project's images/ dir.
    std::string file;

    // Template: vanilla icon reference; the shape is extracted lazily from
    // GD's sheets and cached in images/ under `file`.
    int templateIconId = 1;
    std::string templateFrameSuffix;  // full suffix incl. robot part, e.g. "_02_glow_001"
};

struct IconPiece {
    std::string id;    // short unique id inside the project
    std::string name;  // user-visible layer name
    bool visible = true;
    texture_studio::ImageTransform transform{};
    PieceShape shape{};
    FillSpec fill{};
};

struct IconSlotContent {
    std::vector<IconPiece> pieces;  // bottom to top
};

struct IconProject {
    int schemaVersion = 1;

    std::string id;
    std::string name;
    IconType type = IconType::Cube;

    std::int64_t createdAt = 0;
    std::int64_t modifiedAt = 0;

    bool hasBuiltOnce = false;
    std::int64_t lastBuiltAt = 0;

    // GD multiplies the player's colors onto the icon layers; with this on
    // (default) the mod re-whitens them so gradients/images show their true
    // colors. Off = classic behavior (grayscale art tinted by player colors).
    bool exactColors = true;

    // Keyed by IconAnatomy::slotStorageKey ("main", "glow", "p2.main"...).
    std::map<std::string, IconSlotContent> slots;

    std::string makePieceId() const;
};

inline std::int64_t nowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace paimon::icon_maker

template <>
struct matjson::Serialize<paimon::icon_maker::IconProject> {
    static geode::Result<paimon::icon_maker::IconProject> fromJson(matjson::Value const& value);
    static matjson::Value toJson(paimon::icon_maker::IconProject const& project);
};
