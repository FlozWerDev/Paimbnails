#pragma once

#include <Geode/utils/file.hpp>
#include <functional>

namespace pt {

    /// Callback type — receives the raw Result.
    using FilePickCallback =
        std::function<void(geode::Result<std::optional<std::filesystem::path>>)>;

    geode::utils::file::FilePickOptions::Filter imageFilter();
    geode::utils::file::FilePickOptions::Filter audioFilter();
    geode::utils::file::FilePickOptions::Filter videoFilter();
    geode::utils::file::FilePickOptions::Filter mediaFilter();
    geode::utils::file::FilePickOptions::Filter pngFilter();
    geode::utils::file::FilePickOptions::Filter gifFilter();
    // Images + Windows cursors (.cur/.ico/.ani) + .zip packs.
    geode::utils::file::FilePickOptions::Filter cursorAssetFilter();
    // Autobuild templates: our .pab plus the .tblib libraries of other builders.
    geode::utils::file::FilePickOptions::Filter buildTemplateFilter();

    // pickers (fire-and-forget, same pattern as reference mods)
    void pickImage(FilePickCallback callback);
    // Picker for the cursor gallery: accepts images, Windows cursors, and .zip.
    void pickCursorAsset(FilePickCallback callback);
    void pickGif(FilePickCallback callback);
    void pickBuildTemplate(FilePickCallback callback);
    void pickAudio(FilePickCallback callback);
    void pickVideo(FilePickCallback callback);
    void pickMedia(FilePickCallback callback);
    void saveImage(std::string const& defaultName, FilePickCallback callback);
    void pickFolder(FilePickCallback callback);
    void pickFolder(std::filesystem::path const& defaultPath, FilePickCallback callback);

}

