#include "../src/features/backgrounds/services/WallpaperPlayback.hpp"
#include "../src/features/backgrounds/services/SceneTexture.hpp"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace paimon::wallpaper;

namespace {

void createFile(std::filesystem::path const& path) {
    std::ofstream file(path, std::ios::binary);
    file << "test fixture";
    assert(file.good());
}

void write32(std::ofstream& out, uint32_t n) {
    for (int i = 0; i < 4; ++i) out.put(static_cast<char>((n >> (8 * i)) & 0xff));
}

void createScenePackage(std::filesystem::path const& path, std::string const& name) {
    std::ofstream out(path, std::ios::binary);
    write32(out, 8);
    out.write("PKGV0018", 8);
    write32(out, 1);
    write32(out, static_cast<uint32_t>(name.size()));
    out.write(name.data(), static_cast<std::streamsize>(name.size()));
    write32(out, 0);
    write32(out, 2);
    out.write("{}", 2);
    assert(out.good());
}

void append32(std::vector<uint8_t>& out, uint32_t n) {
    for (int i = 0; i < 4; ++i) out.push_back((n >> (8 * i)) & 0xff);
}

void appendTag(std::vector<uint8_t>& out, char const* tag) {
    while (*tag) out.push_back(static_cast<uint8_t>(*tag++));
    out.push_back(0);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    auto const root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);

    WallpaperItem web;
    web.type = WallpaperType::Web;
    web.rootPath = root;
    web.entryFile = root / "index.html";
    web.previewPath = root / "preview.jpg";
    createFile(web.entryFile);
    createFile(web.previewPath);
    createFile(root / "preview.gif");
    createFile(root / "project.json");

    // Web projects must reach the integrated renderer, never a preview.
    auto playback = resolveWallpaperPlayback(web);
    assert(playback && playback->path == root / "project.json");
    assert(playback->type == WallpaperType::Web);
    assert(std::string(playback->backgroundType()) == "wallpaper");

    auto scene = web;
    scene.type = WallpaperType::Scene;
    scene.entryFile = root / "scene.json"; // Packed projects may omit this file.
    assert(!resolveWallpaperPlayback(scene)); // A missing Scene must not use its preview.
    createFile(root / "scene.json");
    playback = resolveWallpaperPlayback(scene);
    assert(playback && playback->type == WallpaperType::Scene);
    assert(playback->path == root / "project.json");
    assert(std::string(playback->backgroundType()) == "scene");
    std::filesystem::remove(root / "scene.json");

    auto const packed = root / "packed";
    std::filesystem::create_directory(packed);
    createFile(packed / "project.json");
    createScenePackage(packed / "scene.pkg", "scene.json");
    auto package = ScenePackage::open(packed);
    assert(package && package->contains("scene.json"));
    auto content = package->read("scene.json");
    assert(std::string(content.begin(), content.end()) == "{}");
    assert(!package->contains("../scene.json"));
    assert(package->read("../scene.json").empty());
    auto packedScene = scene;
    packedScene.rootPath = packed;
    playback = resolveWallpaperPlayback(packedScene);
    assert(playback && playback->type == WallpaperType::Scene);
    createScenePackage(packed / "scene.pkg", "../scene.json");
    assert(!ScenePackage::open(packed));
    assert(!resolveWallpaperPlayback(packedScene));

    // Decode a tiny TEXV0005 RGBA texture through TEXB0003 and an LZ4 block.
    std::vector<uint8_t> tex;
    appendTag(tex, "TEXV0005");
    appendTag(tex, "TEXI0001");
    for (uint32_t v : {0u, 0u, 2u, 2u, 2u, 2u, 0u}) append32(tex, v);
    appendTag(tex, "TEXB0003");
    append32(tex, 1); append32(tex, 0xffffffff); append32(tex, 1);
    append32(tex, 2); append32(tex, 2); append32(tex, 1); append32(tex, 16);
    append32(tex, 18); // LZ4 literal run for 16 raw bytes.
    tex.push_back(0xf0); tex.push_back(1);
    for (int i = 0; i < 16; ++i) tex.push_back(static_cast<uint8_t>(i));
    auto decoded = decodeSceneTexture(tex);
    assert(decoded && decoded->width == 2 && decoded->height == 2);
    assert(!decoded->encoded && decoded->pixels.size() == 16);
    for (int i = 0; i < 16; ++i) assert(decoded->pixels[i] == i);
    tex.back() = 77;
    decoded = decodeSceneTexture(tex);
    assert(decoded && decoded->pixels.back() == 77);

    createFile(root / "preview.mp4");
    playback = resolveWallpaperPlayback(web);
    assert(playback && playback->path == root / "project.json");
    assert(playback->type == WallpaperType::Web);
    assert(std::string(playback->backgroundType()) == "wallpaper");
    std::filesystem::remove(root / "preview.mp4");
    std::filesystem::remove(root / "preview.gif");
    playback = resolveWallpaperPlayback(web);
    assert(playback && playback->path == root / "project.json");

    std::filesystem::remove(web.previewPath);
    assert(resolveWallpaperPlayback(web));
    assert(!resolveWallpaperPlayback(scene));
    std::filesystem::remove(root / "project.json");
    assert(!resolveWallpaperPlayback(web));
    assert(!resolveWallpaperPlayback(scene));
    std::filesystem::create_directory(root / "preview.gif");
    assert(!resolveWallpaperPlayback(web)); // A directory is not playable media.
    std::filesystem::remove(root / "preview.gif");

    WallpaperItem video;
    video.type = WallpaperType::Video;
    video.entryFile = root / std::filesystem::path(u8"fondo-雪.MP4");
    createFile(video.entryFile);
    playback = resolveWallpaperPlayback(video);
    assert(playback && playback->path == video.entryFile);
    video.entryFile = web.entryFile;
    assert(!resolveWallpaperPlayback(video)); // Never send HTML to the video decoder.
    video.entryFile = root / "missing.mp4";
    assert(!resolveWallpaperPlayback(video));

    for (auto const& [type, name] : {
        std::pair{WallpaperType::Gif, "background.GIF"},
        std::pair{WallpaperType::Image, "background.PNG"}
    }) {
        WallpaperItem image;
        image.type = type;
        image.entryFile = root / name;
        createFile(image.entryFile);
        playback = resolveWallpaperPlayback(image);
        assert(playback && playback->type == type);
        assert(std::string(playback->backgroundType()) == "custom");
    }

    for (auto type : {WallpaperType::Unsupported, WallpaperType::All, WallpaperType::Procedural}) {
        auto unsupported = scene;
        unsupported.type = type;
        unsupported.entryFile = root / "background.PNG";
        assert(!resolveWallpaperPlayback(unsupported));
    }

    // Test Windows backslashes and case-insensitivity in ScenePackage
    auto const winPkgDir = root / "winpkg";
    std::filesystem::create_directories(winPkgDir);
    createScenePackage(winPkgDir / "scene.pkg", "materials\\scene.json");
    // Package with materials\scene.json normalized should be openable if scene.json is accessible
    // Also test direct scene.pkg opening
    createScenePackage(winPkgDir / "scene.pkg", "models\\test.tex");
    // Test case insensitive lookup
    auto winPkg = ScenePackage::open(winPkgDir / "scene.pkg");
    // winPkg won't find scene.json if it only contains models\test.tex, so create one with scene.json:
    {
        std::ofstream out(winPkgDir / "scene.pkg", std::ios::binary);
        write32(out, 8);
        out.write("PKGV0018", 8);
        write32(out, 2);
        // Entry 1: SCENE.JSON
        std::string e1 = "SCENE.JSON";
        write32(out, static_cast<uint32_t>(e1.size()));
        out.write(e1.data(), static_cast<std::streamsize>(e1.size()));
        write32(out, 0);
        write32(out, 2);
        // Entry 2: models\character.tex (with Windows backslash)
        std::string e2 = "models\\character.tex";
        write32(out, static_cast<uint32_t>(e2.size()));
        out.write(e2.data(), static_cast<std::streamsize>(e2.size()));
        write32(out, 2);
        write32(out, 4);
        out.write("{}", 2);
        out.write("TEXV", 4);
        assert(out.good());
    }
    auto testWinPkg = ScenePackage::open(winPkgDir / "scene.pkg");
    assert(testWinPkg && testWinPkg->contains("scene.json")); // Case-insensitive match!
    assert(testWinPkg->contains("models/character.tex")); // Backslash normalized!
    assert(testWinPkg->contains("MODELS/CHARACTER.TEX")); // Case-insensitive normalized!
    auto readData = testWinPkg->read("models/character.tex");
    assert(readData.size() == 4 && std::memcmp(readData.data(), "TEXV", 4) == 0);

    std::cout << "Wallpaper playback regressions passed\n";
}
