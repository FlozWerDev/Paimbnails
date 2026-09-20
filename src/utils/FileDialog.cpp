#include "FileDialog.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/async.hpp>

using namespace geode::prelude;
namespace gfile = geode::utils::file;

// Holder keeps pending pick alive; only one dialog at a time.
using FilePickHolder =
    geode::async::TaskHolder<Result<std::optional<std::filesystem::path>>>;

// Leaked on purpose: avoids teardown order issue with async runtime.
static FilePickHolder& s_filePickHolder = *new FilePickHolder();

namespace pt {

gfile::FilePickOptions::Filter imageFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Image Files (*.png, *.jpg, *.jpeg, *.webp, *.gif, *.bmp, *.tiff, *.tga, *.psd, *.qoi, *.jxl)";
    f.files = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp", "*.tiff", "*.tif", "*.tga", "*.psd", "*.qoi", "*.jxl"};
    return f;
}

gfile::FilePickOptions::Filter audioFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Audio Files (*.mp3, *.ogg, *.opus, *.wav, *.flac, *.m4a)";
    f.files = {"*.mp3", "*.ogg", "*.opus", "*.oga", "*.wav", "*.flac", "*.m4a"};
    return f;
}

gfile::FilePickOptions::Filter videoFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Video Files (*.mp4, *.mov, *.m4v)";
    f.files = {"*.mp4", "*.mov", "*.m4v"};
    return f;
}

gfile::FilePickOptions::Filter mediaFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Images & Videos (*.png, *.jpg, *.gif, *.webp, *.mp4, *.mov, *.avi ...)";
    f.files = {
        "*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp", "*.tiff", "*.tif",
        "*.tga", "*.psd", "*.qoi", "*.jxl",
        "*.mp4", "*.mov", "*.m4v", "*.mpg", "*.mpeg", "*.avi", "*.wmv", "*.mkv", "*.webm"
    };
    return f;
}

gfile::FilePickOptions::Filter pngFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "PNG Image (*.png)";
    f.files = {"*.png"};
    return f;
}

gfile::FilePickOptions::Filter gifFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "GIF Animation (*.gif)";
    f.files = {"*.gif"};
    return f;
}

gfile::FilePickOptions::Filter cursorAssetFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Cursors & Packs (*.png, *.gif, *.cur, *.ani, *.ico, *.zip ...)";
    f.files = {
        "*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp", "*.tiff", "*.tif",
        "*.tga", "*.psd", "*.qoi", "*.jxl",
        "*.cur", "*.ani", "*.ico",
        "*.zip"
    };
    return f;
}

gfile::FilePickOptions::Filter buildTemplateFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Autobuild Templates (*.pab, *.tblib)";
    f.files = {"*.pab", "*.tblib"};
    return f;
}

gfile::FilePickOptions::Filter gmdFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Geometry Dash Levels (*.gmd)";
    f.files = {"*.gmd"};
    return f;
}

gfile::FilePickOptions::Filter jsonFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Texture Studio Pack (*.json)";
    f.files = {"*.json"};
    return f;
}

void pickImage(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {imageFilter()}}),
        std::move(cb)
    );
}

void pickCursorAsset(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {cursorAssetFilter()}}),
        std::move(cb)
    );
}

void pickGif(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {gifFilter()}}),
        std::move(cb)
    );
}

void pickBuildTemplate(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {buildTemplateFilter()}}),
        std::move(cb)
    );
}

void pickGmd(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {gmdFilter()}}),
        std::move(cb)
    );
}

void pickJson(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {jsonFilter()}}),
        std::move(cb)
    );
}

void pickAudio(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {audioFilter()}}),
        std::move(cb)
    );
}

void pickVideo(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {videoFilter()}}),
        std::move(cb)
    );
}

void pickMedia(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {mediaFilter()}}),
        std::move(cb)
    );
}

void saveImage(std::string const& defaultName, FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::SaveFile, {std::filesystem::path(defaultName), {pngFilter()}}),
        std::move(cb)
    );
}

void saveJson(std::string const& defaultName, FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::SaveFile, {std::filesystem::path(defaultName), {jsonFilter()}}),
        std::move(cb)
    );
}

void pickFolder(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFolder, {}),
        std::move(cb)
    );
}

void pickFolder(std::filesystem::path const& defaultPath, FilePickCallback cb) {
    std::optional<std::filesystem::path> defPath;
    if (!defaultPath.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(defaultPath, ec)) {
            defPath = defaultPath;
        }
    }
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFolder, {defPath, {}}),
        std::move(cb)
    );
}

void cancelPendingFilePick() {
    s_filePickHolder.cancel();
}

} // namespace pt
