#pragma once
//
// PackExporter.hpp - Top-level pack export pipeline. Iterates over the
// selected sheets in PackExportConfig, runs SheetTinter (and optionally
// MediumPort) on each, accumulates results, and writes a .zip that
// Texture Loader can directly install.
//
// Output zip layout (mirrors PackGen):
//
//   pack.json
//   ui/colors.json
//   ui/ModsLayer.json
//   ui/LoadingLayer.json
//   GJ_GameSheet01-uhd.png
//   GJ_GameSheet01-uhd.plist
//   GJ_GameSheet01-hd.png        (when includeMediumPort)
//   GJ_GameSheet01-hd.plist
//   ...
//   game_bg_custom.png           (only when a custom bg was supplied)
//
// Errors:
//   - Per-sheet errors are recorded in PackExportResult.sheetResults but
//     do NOT abort the whole export; users get a partial pack.
//   - Top-level errors (cannot create dir, cannot write zip) abort
//     immediately with the corresponding Err.
//
// The function is synchronous and CPU-bound. For ~1500 frames across 5
// sheets it finishes in under ~5 seconds on desktop. UI callers should
// drive it from a coroutine / off-thread task to keep the main thread
// responsive (handled in Phase 5).
//

#include "PackExporterTypes.hpp"

#include <Geode/Geode.hpp>

#include <functional>

namespace paimon::texture_studio {

class PackExporter final {
public:
    // Optional progress callback. Signature:
    //   (sheetIndex, totalSheets, currentSheetName) -> void
    // Called once per sheet just before it begins processing, plus once
    // at the very end with sheetIndex == totalSheets and an empty name.
    using ProgressCallback = std::function<void(int /*idx*/, int /*total*/, std::string const& /*name*/)>;

    // Generate a pack zip according to the config and write it to
    // `outputZipPath`. Parent directories are created as needed. The
    // returned PackExportResult captures per-sheet success/failure so
    // the UI can surface partial results.
    static geode::Result<PackExportResult> exportPack(
        PackExportConfig const& cfg,
        std::filesystem::path const& outputZipPath,
        ProgressCallback progress = {});

private:
    PackExporter() = delete;
};

}  // namespace paimon::texture_studio
