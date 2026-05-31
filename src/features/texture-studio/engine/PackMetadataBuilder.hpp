#pragma once
//
// PackMetadataBuilder.hpp - Generates the metadata JSON files that
// Texture Loader (and HappyTextures) expect at the root of a texture pack:
//
//   pack.json           — pack identity (name, id, version, author).
//   ui/colors.json      — HappyTextures color overrides (optional).
//   ui/ModsLayer.json   — appearance overrides for the mods popup.
//   ui/LoadingLayer.json — loading screen tinting.
//
// We mirror PackGen's emitted structure so packs generated here look and
// behave the same as PackGen-produced ones, modulo the source content.
//
// The output is plain matjson::Value::dump() strings; the exporter writes
// them into the zip as-is. No file IO happens at this layer.
//

#include "PackExporterTypes.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <string_view>

namespace paimon::texture_studio {

class PackMetadataBuilder final {
public:
    // Generate pack.json content. The id is derived from packName via
    // `buildPackId` so that re-exports with the same name overwrite the
    // same Texture Loader entry rather than creating duplicates.
    static std::string buildPackJson(std::string_view packName,
                                     std::string_view author);

    // Generate ui/colors.json. Returns "{}" when no toggles are active.
    static std::string buildUiColorsJson(PackExportConfig const& cfg);

    // Generate ui/ModsLayer.json. Returns "{}" when no toggles are active.
    static std::string buildModsLayerJson(PackExportConfig const& cfg);

    // Generate ui/LoadingLayer.json. Always non-empty (the loading screen
    // always gets at least the bar color tinted).
    static std::string buildLoadingLayerJson(PackExportConfig const& cfg);

    // Compute the canonical pack id for a given user-facing name. Pure
    // function, deterministic — useful for the slot system to detect
    // existing packs by id.
    static std::string buildPackId(std::string_view packName);

private:
    PackMetadataBuilder() = delete;
};

}  // namespace paimon::texture_studio
