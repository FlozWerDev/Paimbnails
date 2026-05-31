#pragma once

#include <Geode/utils/file.hpp>
#include <functional>

namespace pt {

    /// Callback type – receives the raw Result exactly like Jukebox / Eclipse / Globed.
    using FilePickCallback =
        std::function<void(geode::Result<std::optional<std::filesystem::path>>)>;

    // ── filter helpers ──────────────────────────────────────────
    geode::utils::file::FilePickOptions::Filter imageFilter();
    geode::utils::file::FilePickOptions::Filter audioFilter();
    geode::utils::file::FilePickOptions::Filter videoFilter();
    geode::utils::file::FilePickOptions::Filter mediaFilter();
    geode::utils::file::FilePickOptions::Filter pngFilter();
    // Imagenes + cursores de Windows (.cur/.ico/.ani) + packs .zip.
    geode::utils::file::FilePickOptions::Filter cursorAssetFilter();

    // ── pickers (fire-and-forget, same pattern as reference mods) ──
    void pickImage(FilePickCallback callback);
    // Picker para la galeria de cursores: acepta imagenes, cursores Windows y .zip.
    void pickCursorAsset(FilePickCallback callback);
    void pickAudio(FilePickCallback callback);
    void pickVideo(FilePickCallback callback);
    void pickMedia(FilePickCallback callback);
    void saveImage(std::string const& defaultName, FilePickCallback callback);
    void pickFolder(FilePickCallback callback);
    void pickFolder(std::filesystem::path const& defaultPath, FilePickCallback callback);

}

