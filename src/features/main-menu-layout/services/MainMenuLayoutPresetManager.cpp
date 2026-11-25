#include "../../../utils/FilesystemPath.hpp"
#include "MainMenuLayoutPresetManager.hpp"
#include "MainMenuLayoutSerialization.hpp"
#include "../../../utils/AtomicFileWrite.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <chrono>

using namespace geode::prelude;

namespace paimon::menu_layout {
namespace {
    constexpr int kPresetConfigVersion = 2;

    int64_t unixSecondsNow() {
        using namespace std::chrono;
        return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    }
}

MainMenuLayoutPresetManager& MainMenuLayoutPresetManager::get() {
    static MainMenuLayoutPresetManager instance;
    return instance;
}

std::filesystem::path MainMenuLayoutPresetManager::configPath() const {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "main_menu_layout_presets.json";
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

    auto raw = geode::utils::file::readString(paimon::paths::toSdkPath(this->configPath())).unwrapOr("");
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
            preset.name = presetJson["name"].asString().unwrapOr("");
            preset.sceneTag = presetJson["sceneTag"].asString().unwrapOr("");
            preset.updatedAt = presetJson["updatedAt"].asInt().unwrapOr(0);

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

            preset.buttonCount = static_cast<int>(preset.snapshot.buttons.size());
            preset.shapeCount = static_cast<int>(preset.snapshot.shapes.size());
            preset.itemCount = std::max(preset.itemCount, preset.buttonCount + preset.shapeCount);
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
        if (!preset.name.empty()) presetJson["name"] = preset.name;
        if (!preset.sceneTag.empty()) presetJson["sceneTag"] = preset.sceneTag;
        if (preset.updatedAt > 0) presetJson["updatedAt"] = preset.updatedAt;

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

    auto text = root.dump(matjson::TAB_INDENTATION);
    if (!paimon::file::writeAtomically(path, text)) {
        log::error("[MainMenuLayoutPreset] Failed to write presets config");
    }
}

void MainMenuLayoutPresetManager::setPreset(int slotIndex, LayoutSnapshot const& snapshot,
                                            std::string const& name, std::string const& sceneTag) {
    this->ensureLoaded();
    if (slotIndex < 0) return;

    LayoutPreset preset;
    preset.slotIndex = slotIndex;
    preset.buttonCount = static_cast<int>(snapshot.buttons.size());
    preset.shapeCount = static_cast<int>(snapshot.shapes.size());
    preset.itemCount = preset.buttonCount + preset.shapeCount;
    preset.snapshot = snapshot;
    preset.updatedAt = unixSecondsNow();

    auto existing = m_presets.find(slotIndex);
    if (!name.empty()) {
        preset.name = name;
    } else if (existing != m_presets.end()) {
        preset.name = existing->second.name;
    }
    if (!sceneTag.empty()) {
        preset.sceneTag = sceneTag;
    } else if (existing != m_presets.end()) {
        preset.sceneTag = existing->second.sceneTag;
    }

    m_presets[slotIndex] = std::move(preset);
    this->save();
}

bool MainMenuLayoutPresetManager::renamePreset(int slotIndex, std::string const& newName) {
    this->ensureLoaded();
    auto it = m_presets.find(slotIndex);
    if (it == m_presets.end()) return false;
    it->second.name = newName;
    it->second.updatedAt = unixSecondsNow();
    this->save();
    return true;
}

bool MainMenuLayoutPresetManager::deletePreset(int slotIndex) {
    this->ensureLoaded();
    if (m_presets.erase(slotIndex) == 0) return false;
    this->save();
    return true;
}

bool MainMenuLayoutPresetManager::duplicatePreset(int fromSlot, int toSlot) {
    this->ensureLoaded();
    if (toSlot < 0 || fromSlot == toSlot) return false;
    auto it = m_presets.find(fromSlot);
    if (it == m_presets.end()) return false;
    LayoutPreset copy = it->second;
    copy.slotIndex = toSlot;
    copy.updatedAt = unixSecondsNow();
    m_presets[toSlot] = std::move(copy);
    this->save();
    return true;
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
