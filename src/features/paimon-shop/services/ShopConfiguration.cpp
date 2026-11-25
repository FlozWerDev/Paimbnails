#include "../../../utils/FilesystemPath.hpp"
#include "ShopConfiguration.hpp"
#include "../../main-menu-layout/services/MainMenuLayoutSerialization.hpp"
#include "../../rtx/services/RTXManager.hpp"
#include "../../beat-shaders/services/BeatShaderManager.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/BoundedFileRead.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace paimon::shop {
namespace {
std::filesystem::path backupPath() { return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "paimon-shop" / "previous-config.json"; }

matjson::Value captureLayout() {
    auto snapshot = menu_layout::MainMenuLayoutManager::get().exportCustomSnapshot();
    auto value = matjson::makeObject({{"buttons", matjson::Value::array()}, {"shapes", matjson::Value::array()},
        {"moduleEnabled", modules::isSelfEnabled("paimbnails.menulayout.menu")}});
    for (auto const& [key, button] : snapshot.buttons) {
        auto shared = button;
        if (!shared.fontFile.empty()) shared.fontFile = std::filesystem::path(shared.fontFile).filename().string();
        value["buttons"].push(menu_layout::layoutToJson(key, shared));
    }
    for (auto const& shape : snapshot.shapes) value["shapes"].push(menu_layout::shapeToJson(shape));
    return value;
}

matjson::Value captureBeatShaders() {
    auto& shaders = beat_shaders::BeatShaderManager::get();
    auto cfg = shaders.getConfig();
    auto value = matjson::makeObject({{"enabled", cfg.enabled}, {"shaderName", cfg.shaderName},
        {"intensity", cfg.intensity}, {"bassMult", cfg.bassMult}, {"midMult", cfg.midMult},
        {"trebleMult", cfg.trebleMult}, {"beatMult", cfg.beatMult}, {"energyMult", cfg.energyMult},
        {"moduleEnabled", modules::isSelfEnabled("paimbnails.beatshaders.global")}, {"layers", matjson::makeObject({})}});
    for (auto const& [key, label] : shaders.availableLayers()) value["layers"][key] = shaders.isLayerEnabled(key);
    return value;
}

Result<> checkSupported(Category category, matjson::Value const& configuration) {
    auto valid = validateConfiguration(category, configuration);
    if (!valid) return valid;
    if (configuration.contains("beat-shaders")) {
        auto name = configuration["beat-shaders"]["shaderName"].asString().unwrapOr("");
        auto shaders = beat_shaders::BeatShaderManager::get().availableShaders();
        if (std::none_of(shaders.begin(), shaders.end(), [&name](auto const& shader) { return shader.id == name; })) {
            return Err("Este shader no esta instalado / This shader is not installed");
        }
    }
    if (configuration.contains("layout")) {
        for (auto const& entry : configuration["layout"]["buttons"]) {
            auto font = entry["fontFile"].asString().unwrapOr("");
            if (!font.empty() && !CCFileUtils::sharedFileUtils()->isFileExist(font)) {
                return Err("El layout requiere una fuente no instalada / Layout requires an uninstalled font");
            }
        }
    }
    return Ok();
}

Result<> applyValidated(matjson::Value const& configuration) {
    if (configuration.contains("rtx") && !rtx::RTXManager::get().importConfig(configuration["rtx"])) {
        return Err("No se pudo guardar RTX / Could not save RTX");
    }
    if (configuration.contains("layout")) {
        auto const& value = configuration["layout"];
        menu_layout::LayoutSnapshot snapshot;
        for (auto const& entry : value["buttons"]) snapshot.buttons[entry["key"].asString().unwrap()] = menu_layout::layoutFromJson(entry);
        for (auto const& entry : value["shapes"]) snapshot.shapes.push_back(menu_layout::shapeFromJson(entry));
        if (!menu_layout::MainMenuLayoutManager::get().replaceCustomSnapshot(snapshot)) {
            return Err("No se pudo guardar el layout / Could not save layout");
        }
        modules::setEnabled("paimbnails.menulayout.menu", value["moduleEnabled"].asBool().unwrapOr(true));
    }
    if (configuration.contains("beat-shaders")) {
        auto const& value = configuration["beat-shaders"];
        beat_shaders::BeatShaderConfig cfg;
        cfg.enabled = value["enabled"].asBool().unwrapOr(false);
        cfg.shaderName = value["shaderName"].asString().unwrap();
        cfg.intensity = std::clamp(static_cast<float>(value["intensity"].asDouble().unwrap()), 0.f, 1.f);
        cfg.bassMult = static_cast<float>(value["bassMult"].asDouble().unwrap());
        cfg.midMult = static_cast<float>(value["midMult"].asDouble().unwrap());
        cfg.trebleMult = static_cast<float>(value["trebleMult"].asDouble().unwrap());
        cfg.beatMult = static_cast<float>(value["beatMult"].asDouble().unwrap());
        cfg.energyMult = static_cast<float>(value["energyMult"].asDouble().unwrap());
        modules::setEnabled("paimbnails.beatshaders.global", value["moduleEnabled"].asBool().unwrapOr(true));
        auto& shaders = beat_shaders::BeatShaderManager::get();
        shaders.saveConfig(cfg);
        for (auto const& [key, label] : shaders.availableLayers()) {
            shaders.setLayerEnabled(key, value["layers"][key].asBool().unwrapOr(true));
        }
        shaders.rebuildBackgrounds();
        shaders.refreshLiveSpriteUniforms();
    }
    return Ok();
}

Result<> applyWithRollback(matjson::Value const& next, matjson::Value const& previous) {
    auto applied = applyValidated(next);
    if (applied) return applied;
    auto restored = applyValidated(previous);
    if (!restored) return Err(applied.unwrapErr() + ". Recupera previous-config.json / Recover previous-config.json");
    return applied;
}
}

matjson::Value captureConfiguration(Category category) {
    auto result = matjson::makeObject({});
    if (category == Category::Layout || category == Category::Pack) result["layout"] = captureLayout();
    if (category == Category::RTX || category == Category::Pack) {
        rtx::RTXManager::get().init();
        result["rtx"] = rtx::RTXManager::get().exportConfig();
    }
    if (category == Category::BeatShaders || category == Category::Pack) result["beat-shaders"] = captureBeatShaders();
    return result;
}

Result<> applyConfiguration(Category category, matjson::Value const& configuration) {
    auto supported = checkSupported(category, configuration);
    if (!supported) return supported;
    auto previous = captureConfiguration(category);
    auto backup = matjson::makeObject({{"category", categoryId(category)}, {"configuration", previous}});
    if (!paimon::file::writeAtomically(backupPath(), backup.dump())) return Err("No se pudo crear el respaldo / Could not create backup");
    return applyWithRollback(configuration, previous);
}

bool hasConfigurationBackup() {
    std::error_code ec;
    return std::filesystem::is_regular_file(backupPath(), ec) && !ec;
}

Result<> restoreConfiguration() {
    auto bytes = paimon::file::readBytes(backupPath(), 512 * 1024);
    if (bytes.empty()) return Err("No hay respaldo legible / No readable backup");
    auto json = parseBoundedJson(std::string_view(reinterpret_cast<char const*>(bytes.data()), bytes.size()));
    if (!json) return Err("Respaldo invalido / Invalid backup");
    auto category = parseCategory(json.unwrap()["category"].asString().unwrapOr(""));
    if (!category) return Err(category.unwrapErr());
    auto const& config = json.unwrap()["configuration"];
    auto supported = checkSupported(category.unwrap(), config);
    if (!supported) return supported;
    auto result = applyWithRollback(config, captureConfiguration(category.unwrap()));
    if (result) {
        std::error_code ec;
        std::filesystem::remove(backupPath(), ec);
    }
    return result;
}

std::string describeConfiguration(Preset const& preset) {
    std::string result;
    if (preset.configuration.contains("layout")) {
        auto const& layout = preset.configuration["layout"];
        result += fmt::format("Custom Layout: {} buttons, {} shapes\n", layout["buttons"].size(), layout["shapes"].size());
    }
    if (preset.configuration.contains("rtx")) {
        auto const& rtx = preset.configuration["rtx"];
        result += fmt::format("RTX: {} rays / {} steps | {}%\n", rtx["rayCount"].asInt().unwrapOr(4),
            rtx["raySteps"].asInt().unwrapOr(14), static_cast<int>(std::clamp(rtx["renderScale"].asDouble().unwrapOr(0.6), 0.2, 1.0) * 100));
        result += rtx["enabled"].asBool().unwrapOr(false) ? "RTX ON\n" : "RTX OFF\n";
    }
    if (preset.configuration.contains("beat-shaders")) result += "Shader: " + preset.configuration["beat-shaders"]["shaderName"].asString().unwrapOr("");
    return result;
}
} // namespace paimon::shop
