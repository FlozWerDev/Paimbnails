// Engine doubles; the configuration orchestration below is production source.
#include "src/features/paimon-shop/services/ShopConfiguration.hpp"
#include "src/utils/AtomicFileWrite.hpp"
#include "src/utils/BoundedFileRead.hpp"
#include "src/utils/FilesystemPath.hpp"
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <cassert>
#include <fstream>
#include <unordered_map>

namespace geode::prelude { using namespace geode; }
struct Mod {
    std::filesystem::path directory;
    static Mod* get() { static Mod mod; return &mod; }
    auto getSaveDir() { return directory; }
};
struct CCFileUtils {
    static CCFileUtils* sharedFileUtils() { static CCFileUtils files; return &files; }
    bool isFileExist(std::string const&) { return true; }
};
namespace paimon::modules {
inline std::unordered_map<std::string, bool> flags;
bool isSelfEnabled(std::string const& id) { return flags[id]; }
void setEnabled(std::string const& id, bool value) { flags[id] = value; }
}
namespace paimon::menu_layout {
struct MenuButtonLayout { std::string fontFile; matjson::Value properties; };
using DrawShapeLayout = matjson::Value;
struct LayoutSnapshot { std::unordered_map<std::string, MenuButtonLayout> buttons; std::vector<DrawShapeLayout> shapes; };
matjson::Value layoutToJson(std::string const& key, MenuButtonLayout const& layout) {
    auto json = layout.properties;
    json["key"] = key;
    json["fontFile"] = layout.fontFile;
    return json;
}
MenuButtonLayout layoutFromJson(matjson::Value const& json) { return {json["fontFile"].asString().unwrapOr(""), json}; }
matjson::Value shapeToJson(DrawShapeLayout const& shape) { return shape; }
DrawShapeLayout shapeFromJson(matjson::Value const& json) { return json; }
struct MainMenuLayoutManager {
    LayoutSnapshot snapshot;
    bool failNext = false;
    static MainMenuLayoutManager& get() { static MainMenuLayoutManager manager; return manager; }
    LayoutSnapshot exportCustomSnapshot() { return snapshot; }
    bool replaceCustomSnapshot(LayoutSnapshot const& next) {
        if (failNext) { failNext = false; return false; }
        snapshot = next;
        return true;
    }
};
}
namespace paimon::rtx {
struct RTXManager {
    matjson::Value configuration = matjson::makeObject({{"schema", 6}, {"enabled", false}, {"rayCount", 4}, {"raySteps", 14}, {"renderScale", 0.6}});
    bool failNext = false;
    static RTXManager& get() { static RTXManager manager; return manager; }
    void init() {}
    matjson::Value exportConfig() { return configuration; }
    bool importConfig(matjson::Value const& next) {
        if (failNext) { failNext = false; return false; }
        configuration = next;
        return true;
    }
};
}
namespace paimon::beat_shaders {
struct BeatShaderConfig {
    bool enabled = false;
    std::string shaderName = "glitch-beat";
    float intensity = 0.7f, bassMult = 1, midMult = 1, trebleMult = 1, beatMult = 1, energyMult = 1;
};
struct BeatShaderManager {
    BeatShaderConfig configuration;
    std::unordered_map<std::string, bool> layers;
    int saves = 0;
    struct ShaderEntry { std::string id; };
    static BeatShaderManager& get() { static BeatShaderManager manager; return manager; }
    BeatShaderConfig getConfig() { return configuration; }
    void saveConfig(BeatShaderConfig const& next) { configuration = next; ++saves; }
    std::vector<std::pair<std::string, std::string>> availableLayers() { return {{"menu", "Menu"}}; }
    std::vector<ShaderEntry> availableShaders() { return {{"glitch-beat"}, {"wave-beat"}}; }
    bool isLayerEnabled(std::string const& key) { return layers[key]; }
    void setLayerEnabled(std::string const& key, bool enabled) { layers[key] = enabled; }
    void rebuildBackgrounds() {}
    void refreshLiveSpriteUniforms() {}
};
}

@SHOP_CONFIGURATION_SOURCE@

void testConfigurations(std::filesystem::path const& directory) {
    using namespace paimon;
    using namespace paimon::shop;
    Mod::get()->directory = directory;
    auto original = captureConfiguration(Category::Pack);
    assert(validateConfiguration(Category::Pack, original));
    auto next = original;
    next["rtx"]["enabled"] = true;
    next["rtx"]["rayCount"] = 8;
    next["layout"]["buttons"].push(matjson::makeObject({{"key", "play"}, {"x", 42}}));
    next["layout"]["moduleEnabled"] = true;
    next["beat-shaders"]["enabled"] = true;
    next["beat-shaders"]["shaderName"] = "wave-beat";
    next["beat-shaders"]["moduleEnabled"] = true;
    next["beat-shaders"]["layers"]["menu"] = true;

    // No state changes when the backup cannot be written.
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "blocked") << "file";
    Mod::get()->directory = directory / "blocked";
    assert(applyConfiguration(Category::Pack, next).isErr());
    assert(captureConfiguration(Category::Pack) == original);
    Mod::get()->directory = directory;

    assert(applyConfiguration(Category::Pack, next));
    assert(hasConfigurationBackup());
    assert(rtx::RTXManager::get().configuration["rayCount"].asInt().unwrap() == 8);
    assert(menu_layout::MainMenuLayoutManager::get().snapshot.buttons.size() == 1);
    assert(beat_shaders::BeatShaderManager::get().configuration.shaderName == "wave-beat");
    assert(restoreConfiguration());
    assert(!hasConfigurationBackup());
    assert(captureConfiguration(Category::Pack) == original);

    // A layout write failure after RTX was changed rolls RTX back, too.
    menu_layout::MainMenuLayoutManager::get().failNext = true;
    assert(applyConfiguration(Category::Pack, next).isErr());
    assert(captureConfiguration(Category::Pack) == original);
    assert(restoreConfiguration());

    rtx::RTXManager::get().failNext = true;
    assert(applyConfiguration(Category::Pack, next).isErr());
    assert(captureConfiguration(Category::Pack) == original);
    assert(restoreConfiguration());

    // Unsupported shaders are rejected before replacing the backup or settings.
    next["beat-shaders"]["shaderName"] = "uninstalled";
    assert(applyConfiguration(Category::Pack, next).isErr());
    assert(!hasConfigurationBackup());
    assert(captureConfiguration(Category::Pack) == original);

    auto rtxOnly = matjson::makeObject({{"rtx", next["rtx"]}});
    assert(applyConfiguration(Category::RTX, rtxOnly));
    assert(captureConfiguration(Category::Layout) == matjson::makeObject({{"layout", original["layout"]}}));
    assert(captureConfiguration(Category::BeatShaders) == matjson::makeObject({{"beat-shaders", original["beat-shaders"]}}));
    assert(restoreConfiguration());
    assert(captureConfiguration(Category::Pack) == original);
    assert(restoreConfiguration().isErr());
}
