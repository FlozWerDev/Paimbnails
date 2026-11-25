#include "../src/features/paimon-shop/data/ShopPreset.hpp"
#include "../src/features/paimon-shop/services/ShopLibrary.hpp"
#include "../src/utils/Base64.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace paimon::shop;
void testConfigurations(std::filesystem::path const& directory);

int main(int argc, char** argv) {
    assert(argc == 2);
    Preset preset;
    preset.title = "Mi RTX";
    preset.description = "Luz suave";
    preset.author = "Paimon";
    preset.category = Category::RTX;
    preset.configuration = matjson::makeObject({{"rtx", matjson::makeObject({{"schema", 6}, {"enabled", true}, {"rayCount", 4}})}});
    preset.preview = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=";
    assert(validatePreset(preset).isOk());
    auto decoded = presetFromJson(presetToJson(preset));
    assert(decoded.isOk() && decoded.unwrap().configuration == preset.configuration);
    auto invalid = presetToJson(preset);
    invalid["version"] = 99;
    assert(presetFromJson(invalid).isErr());

    assert(parseBoundedJson(std::string(10000, '[')).isErr());
    assert(parseBoundedJson(std::string("// \"\n") + std::string(10000, '[')).isErr());
    assert(parseBoundedJson(R"({"title":"[ { \" } ]"})").isOk());
    assert(parseBoundedJson(std::string(kMaxPresetBytes + 1, ' ')).isErr());
    invalid = presetToJson(preset);
    invalid["configuration"]["rtx"]["renderScale"] = 1e300;
    assert(presetFromJson(invalid).isErr());

    auto layout = matjson::makeObject({{"buttons", matjson::Value::array()}, {"shapes", matjson::Value::array()}});
    auto layoutConfig = matjson::makeObject({{"layout", layout}});
    assert(validateConfiguration(Category::Layout, layoutConfig).isOk());
    layoutConfig["layout"]["buttons"].push(matjson::makeObject({{"key", "menu/play"}, {"opacity", 0.5}, {"x", 140}}));
    assert(validateConfiguration(Category::Layout, layoutConfig).isOk());
    auto duplicate = layoutConfig["layout"]["buttons"][0];
    layoutConfig["layout"]["buttons"].push(duplicate);
    assert(validateConfiguration(Category::Layout, layoutConfig).isErr());
    layoutConfig["layout"] = layout;
    duplicate["fontFile"] = "../outside.fnt";
    layoutConfig["layout"]["buttons"].push(duplicate);
    assert(validateConfiguration(Category::Layout, layoutConfig).isErr());
    layoutConfig["layout"] = layout;
    duplicate["fontFile"] = "bigFont.fnt";
    duplicate["scale"] = -2;
    layoutConfig["layout"]["buttons"].push(duplicate);
    assert(validateConfiguration(Category::Layout, layoutConfig).isErr());
    auto beat = matjson::makeObject({{"shaderName", "glitch-beat"}, {"intensity", 0.7}, {"bassMult", 1},
        {"midMult", 1}, {"trebleMult", 1}, {"beatMult", 1}, {"energyMult", 1}});
    auto beatConfig = matjson::makeObject({{"beat-shaders", beat}});
    assert(validateConfiguration(Category::BeatShaders, beatConfig).isOk());
    auto pack = preset.configuration;
    pack["beat-shaders"] = beat;
    pack["layout"] = layout;
    assert(validateConfiguration(Category::Pack, pack).isOk());
    assert(validateConfiguration(Category::RTX, pack).isErr());
    beatConfig["beat-shaders"]["bassMult"] = 4;
    assert(validateConfiguration(Category::BeatShaders, beatConfig).isErr());
    std::vector<uint8_t> png;
    assert(paimon::base64Decode(preset.preview, png));
    png[16] = 1;
    assert(validatePreview(png).isErr());
    invalid = presetToJson(preset);
    invalid["category"] = "unknown";
    assert(presetFromJson(invalid).isErr());
    invalid = presetToJson(preset);
    invalid["configuration"]["layout"] = matjson::makeObject({});
    assert(presetFromJson(invalid).isErr());
    invalid = presetToJson(preset);
    invalid["preview"] = "not an image";
    assert(presetFromJson(invalid).isErr());
    invalid = presetToJson(preset);
    invalid["title"] = "  ";
    assert(presetFromJson(invalid).isErr());
    invalid = presetToJson(preset);
    invalid["configuration"]["rtx"]["schema"] = 999;
    assert(presetFromJson(invalid).isErr());
    invalid = presetToJson(preset);
    invalid["configuration"]["rtx"]["rayCount"] = "bad";
    assert(presetFromJson(invalid).isErr());

    ShopLibrary library(argv[1]);
    assert(library.list().unwrap().empty());
    auto id = library.add(preset).unwrap();
    assert(library.list().unwrap().size() == 1);
    assert(library.read(id).unwrap().title == preset.title);
    assert(library.read("../outside").isErr());
    auto exported = library.read(id).unwrap();
    auto secondId = library.add(exported).unwrap();
    assert(secondId != id);
    assert(library.remove(id).isOk());
    assert(library.list().unwrap().size() == 1);
    assert(library.read(id).isErr());
    assert(library.read(secondId).isOk());
    // Corrupt metadata must fail without replacing the existing library.
    std::ofstream(std::filesystem::path(argv[1]) / "index.json") << "broken";
    assert(library.list().isErr());
    assert(library.add(preset).isErr());
    assert(library.read(secondId).isOk());
    testConfigurations(std::filesystem::path(argv[1]) / "configuration");
    std::cout << "PASS: preset validation, round trip, library persistence, safe paths, corrupt-index preservation, backup, restore and partial-failure rollback\n";
}
