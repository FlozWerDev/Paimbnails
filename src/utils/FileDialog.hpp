#pragma once

#include <Geode/utils/file.hpp>
#include <functional>

namespace pt {

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
    // Geometry Dash level exports for the official slots.
    geode::utils::file::FilePickOptions::Filter gmdFilter();
    // Texture Studio shared packs.
    geode::utils::file::FilePickOptions::Filter jsonFilter();

    // Fire-and-forget pickers.
    void pickImage(FilePickCallback callback);
    void pickCursorAsset(FilePickCallback callback);
    void pickGif(FilePickCallback callback);
    void pickGmd(FilePickCallback callback);
    void pickJson(FilePickCallback callback);
    void pickAudio(FilePickCallback callback);
    void pickVideo(FilePickCallback callback);
    void pickMedia(FilePickCallback callback);
    void saveImage(std::string const& defaultName, FilePickCallback callback);
    void saveJson(std::string const& defaultName, FilePickCallback callback);
    void pickFolder(FilePickCallback callback);
    void pickFolder(std::filesystem::path const& defaultPath, FilePickCallback callback);
    void cancelPendingFilePick();

}
