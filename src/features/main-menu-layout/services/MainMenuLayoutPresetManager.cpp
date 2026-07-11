#include "MainMenuLayoutPresetManager.hpp"

#include <Geode/utils/file.hpp>

#include <fstream>

using namespace geode::prelude;

namespace paimon::menu_layout {
namespace {
    constexpr int kPresetConfigVersion = 1;

    matjson::Value layoutToJson(std::string const& key, MenuButtonLayout const& layout) {
        matjson::Value value = matjson::makeObject({});
        value["key"] = key;
        value["x"] = layout.position.x;
        value["y"] = layout.position.y;
        value["scale"] = layout.scale;
        value["scaleX"] = layout.scaleX;
        value["scaleY"] = layout.scaleY;
        value["opacity"] = layout.opacity;
        value["hidden"] = layout.hidden;
        value["layer"] = layout.layer;
        value["linkGroup"] = layout.linkGroup;
        value["hasColor"] = layout.hasColor;
        value["r"] = layout.color.r;
        value["g"] = layout.color.g;
        value["b"] = layout.color.b;
        value["fontFile"] = layout.fontFile;
        return value;
    }

    MenuButtonLayout layoutFromJson(matjson::Value const& value) {
        MenuButtonLayout layout;
        layout.position.x = static_cast<float>(value["x"].asDouble().unwrapOr(0.0));
        layout.position.y = static_cast<float>(value["y"].asDouble().unwrapOr(0.0));
        layout.scale = static_cast<float>(value["scale"].asDouble().unwrapOr(1.0));
        layout.scaleX = static_cast<float>(value["scaleX"].asDouble().unwrapOr(layout.scale));
        layout.scaleY = static_cast<float>(value["scaleY"].asDouble().unwrapOr(layout.scale));
        layout.opacity = static_cast<float>(value["opacity"].asDouble().unwrapOr(1.0));
        layout.hidden = value["hidden"].asBool().unwrapOr(false);
        layout.layer = static_cast<int>(value["layer"].asInt().unwrapOr(0));
        layout.linkGroup = value["linkGroup"].asString().unwrapOr("");
        layout.hasColor = value["hasColor"].asBool().unwrapOr(false);
        layout.color.r = static_cast<GLubyte>(value["r"].asInt().unwrapOr(255));
        layout.color.g = static_cast<GLubyte>(value["g"].asInt().unwrapOr(255));
        layout.color.b = static_cast<GLubyte>(value["b"].asInt().unwrapOr(255));
        layout.fontFile = value["fontFile"].asString().unwrapOr("");
        return layout;
    }

    matjson::Value shapeToJson(DrawShapeLayout const& layout) {
        matjson::Value value = matjson::makeObject({});
        value["id"] = layout.id;
        value["kind"] = layout.kind == DrawShapeKind::Rectangle ? "rect" : layout.kind == DrawShapeKind::Circle ? "circle" : "round";
        value["x"] = layout.position.x;
        value["y"] = layout.position.y;
        value["scale"] = layout.scale;
        value["scaleX"] = layout.scaleX;
        value["scaleY"] = layout.scaleY;
        value["opacity"] = layout.opacity;
        value["hidden"] = layout.hidden;
        value["width"] = layout.width;
        value["height"] = layout.height;
        value["cornerRadius"] = layout.cornerRadius;
        value["r"] = layout.color.r;
        value["g"] = layout.color.g;
        value["b"] = layout.color.b;
        value["zOrder"] = layout.zOrder;
        value["layer"] = layout.layer;
        value["linkGroup"] = layout.linkGroup;
        return value;
    }

    DrawShapeLayout shapeFromJson(matjson::Value const& value) {
        DrawShapeLayout layout;
        layout.id = value["id"].asString().unwrapOr("");
        auto kind = value["kind"].asString().unwrapOr("round");
        layout.kind = kind == "rect" ? DrawShapeKind::Rectangle : kind == "circle" ? DrawShapeKind::Circle : DrawShapeKind::RoundedRect;
        layout.position.x = static_cast<float>(value["x"].asDouble().unwrapOr(0.0));
        layout.position.y = static_cast<float>(value["y"].asDouble().unwrapOr(0.0));
        layout.scale = static_cast<float>(value["scale"].asDouble().unwrapOr(1.0));
        layout.scaleX = static_cast<float>(value["scaleX"].asDouble().unwrapOr(layout.scale));
        layout.scaleY = static_cast<float>(value["scaleY"].asDouble().unwrapOr(layout.scale));
        layout.opacity = static_cast<float>(value["opacity"].asDouble().unwrapOr(0.75));
        layout.hidden = value["hidden"].asBool().unwrapOr(false);
        layout.width = static_cast<float>(value["width"].asDouble().unwrapOr(110.0));
        layout.height = static_cast<float>(value["height"].asDouble().unwrapOr(70.0));
        layout.cornerRadius = static_cast<float>(value["cornerRadius"].asDouble().unwrapOr(18.0));
        layout.color.r = static_cast<GLubyte>(value["r"].asInt().unwrapOr(90));
        layout.color.g = static_cast<GLubyte>(value["g"].asInt().unwrapOr(220));
        layout.color.b = static_cast<GLubyte>(value["b"].asInt().unwrapOr(255));
        layout.zOrder = static_cast<int>(value["zOrder"].asInt().unwrapOr(0));
        layout.layer = static_cast<int>(value["layer"].asInt().unwrapOr(0));
        layout.linkGroup = value["linkGroup"].asString().unwrapOr("");
        return layout;
    }
}

MainMenuLayoutPresetManager& MainMenuLayoutPresetManager::get() {
    static MainMenuLayoutPresetManager instance;
    return instance;
}

std::filesystem::path MainMenuLayoutPresetManager::configPath() const {
    return Mod::get()->getSaveDir() / "main_menu_layout_presets.json";
}

void MainMenuLayoutPresetManager::ensureLoaded() {
    if (!m_loaded) {
        this->load();
    }
}

void MainMenuLayoutPresetManager::load() {
    if (m_loaded) return;
    m_loaded = true;
    m_presets.clear();

    auto raw = file::readString(this->configPath()).unwrapOr("");
    if (raw.empty()) {
        return;
    }

    auto parsed = matjson::parse(raw);
    if (parsed.isErr()) {
        log::warn("[MainMenuLayoutPreset] Failed to parse presets config");
        return;
    }

    auto root = parsed.unwrap();
    if (auto presets = root["presets"].asArray()) {
        for (auto const& presetJson : presets.unwrap()) {
            auto slot = static_cast<int>(presetJson["slot"].asInt().unwrapOr(-1));
            if (slot < 0) continue;

            LayoutPreset preset;
            preset.slotIndex = slot;
            preset.itemCount = static_cast<int>(presetJson["itemCount"].asInt().unwrapOr(0));

            if (auto entries = presetJson["entries"].asArray()) {
                for (auto const& entry : entries.unwrap()) {
                    auto key = entry["key"].asString().unwrapOr("");
                    if (key.empty()) continue;
                    preset.snapshot.buttons[key] = layoutFromJson(entry);
                }
            }

            if (auto shapes = presetJson["shapes"].asArray()) {
                for (auto const& shapeJson : shapes.unwrap()) {
                    auto shape = shapeFromJson(shapeJson);
                    if (!shape.id.empty()) {
                        preset.snapshot.shapes.push_back(std::move(shape));
                    }
                }
            }

            preset.itemCount = std::max(preset.itemCount, static_cast<int>(preset.snapshot.buttons.size() + preset.snapshot.shapes.size()));
            m_presets[slot] = std::move(preset);
        }
    }
}

void MainMenuLayoutPresetManager::save() {
    this->ensureLoaded();

    matjson::Value root = matjson::makeObject({});
    root["version"] = kPresetConfigVersion;

    matjson::Value presets = matjson::Value::array();
    for (auto const& [slot, preset] : m_presets) {
        matjson::Value presetJson = matjson::makeObject({});
        presetJson["slot"] = slot;
        presetJson["itemCount"] = preset.itemCount;

        matjson::Value entries = matjson::Value::array();
        for (auto const& [key, layout] : preset.snapshot.buttons) {
            entries.push(layoutToJson(key, layout));
        }
        presetJson["entries"] = entries;

        matjson::Value shapes = matjson::Value::array();
        for (auto const& shape : preset.snapshot.shapes) {
            shapes.push(shapeToJson(shape));
        }
        presetJson["shapes"] = shapes;
        presets.push(presetJson);
    }
    root["presets"] = presets;

    auto path = this->configPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) {
        log::error("[MainMenuLayoutPreset] Failed to open presets config for writing");
        return;
    }

    auto text = root.dump(matjson::TAB_INDENTATION);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void MainMenuLayoutPresetManager::setPreset(int slotIndex, LayoutSnapshot const& snapshot) {
    this->ensureLoaded();
    if (slotIndex < 0) return;

    LayoutPreset preset;
    preset.slotIndex = slotIndex;
    preset.itemCount = static_cast<int>(snapshot.buttons.size() + snapshot.shapes.size());
    preset.snapshot = snapshot;
    m_presets[slotIndex] = std::move(preset);
    this->save();
}

std::optional<LayoutPreset> MainMenuLayoutPresetManager::getPreset(int slotIndex) const {
    const_cast<MainMenuLayoutPresetManager*>(this)->ensureLoaded();
    auto it = m_presets.find(slotIndex);
    if (it == m_presets.end()) return std::nullopt;
    return it->second;
}

bool MainMenuLayoutPresetManager::hasPreset(int slotIndex) const {
    const_cast<MainMenuLayoutPresetManager*>(this)->ensureLoaded();
    return m_presets.find(slotIndex) != m_presets.end();
}

int MainMenuLayoutPresetManager::maxUsedSlot() const {
    const_cast<MainMenuLayoutPresetManager*>(this)->ensureLoaded();
    if (m_presets.empty()) return -1;
    return m_presets.rbegin()->first;
}

} // namespace paimon::menu_layout
