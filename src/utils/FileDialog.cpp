#include "FileDialog.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/async.hpp>

using namespace geode::prelude;
namespace gfile = geode::utils::file;

// Keep the TaskHolder alive so that Geode 5.4+ doesn't garbage-collect
// the pending file-pick operation before the OS dialog returns.
// Only one native file dialog can be open at a time, so a single holder
// is enough (a new pick replaces the previous one).
static geode::async::TaskHolder<Result<std::optional<std::filesystem::path>>>
    s_filePickHolder;

// ── filters ─────────────────────────────────────────────────────

namespace pt {

gfile::FilePickOptions::Filter imageFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Image Files (*.png, *.jpg, *.jpeg, *.webp, *.gif, *.bmp, *.tiff, *.tga, *.psd, *.qoi, *.jxl)";
    f.files = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp", "*.tiff", "*.tif", "*.tga", "*.psd", "*.qoi", "*.jxl"};
    return f;
}

gfile::FilePickOptions::Filter audioFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Audio Files (*.mp3, *.ogg, *.wav, *.flac, *.m4a)";
    f.files = {"*.mp3", "*.ogg", "*.wav", "*.flac", "*.m4a"};
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
    f.description = "Images & Videos (*.png, *.jpg, *.gif, *.webp, *.mp4, *.mov ...)";
    f.files = {
        "*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp", "*.tiff", "*.tif",
        "*.tga", "*.psd", "*.qoi", "*.jxl",
        "*.mp4", "*.mov", "*.m4v"
    };
    return f;
}

gfile::FilePickOptions::Filter pngFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "PNG Image (*.png)";
    f.files = {"*.png"};
    return f;
}

// ── pickers ─────────────────────────────────────────────────────

void pickImage(FilePickCallback cb) {
    s_filePickHolder.spawn("Paimbnails FilePicker",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {imageFilter()}}),
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

} // namespace pt
