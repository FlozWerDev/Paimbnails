#include "ButtonLayoutManager.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <sstream>

using namespace geode::prelude;

ButtonLayoutManager& ButtonLayoutManager::get() {
    static ButtonLayoutManager instance;
    return instance;
}

static void parseLayoutFileContent(std::string const& content, std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>>& outMap) {
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream lineStream(line);
        std::string sceneKey, buttonID;
        ButtonLayout layout;
        layout.scale = 1.0f;
        layout.opacity = 1.0f;
        if (std::getline(lineStream, sceneKey, '|') &&
            std::getline(lineStream, buttonID, '|') &&
            lineStream >> layout.position.x && lineStream.ignore(1) &&
            lineStream >> layout.position.y) {
            if (lineStream.ignore(1) && lineStream >> layout.scale) {
                lineStream.ignore(1);
                lineStream >> layout.opacity;
            }
            outMap[sceneKey][buttonID] = layout;
        }
    }
}

void ButtonLayoutManager::load() {
    if (m_loaded) return;
    m_loaded = true;
    auto filePath = geode::Mod::get()->getSaveDir() / "button_layouts.txt";
    auto content = file::readString(filePath).unwrapOr("");
    if (content.empty()) {
        log::debug("[ButtonLayoutManager] no se encontro archivo diseno; usando por defecto");
        m_layouts.clear();
        loadDefaults();
        return;
    }

    m_layouts.clear();
    parseLayoutFileContent(content, m_layouts);
    log::info("[ButtonLayoutManager] cargados {} disenos de escena", m_layouts.size());
    loadDefaults();
}

void ButtonLayoutManager::save() {
    auto filePath = geode::Mod::get()->getSaveDir() / "button_layouts.txt";
    std::string data = "# Button layouts (sceneKey|buttonID|x|y|scale|opacity)\n";
    for (auto& [sceneKey, buttons] : m_layouts) {
        for (auto& [buttonID, layout] : buttons) {
            data += sceneKey + "|" + buttonID + "|"
                + std::to_string(layout.position.x) + "|" + std::to_string(layout.position.y) + "|"
                + std::to_string(layout.scale) + "|" + std::to_string(layout.opacity) + "\n";
        }
    }
    auto res = file::writeStringSafe(filePath, data);
    if (!res) {
        log::error("[ButtonLayoutManager] no se pudo escribir archivo diseno: {}", res.unwrapErr());
        return;
    }
    log::info("[ButtonLayoutManager] disenos botones guardados");
}

void ButtonLayoutManager::loadDefaults() {
    auto filePath = geode::Mod::get()->getSaveDir() / "button_defaults.txt";
    m_defaults.clear();
    auto content = file::readString(filePath).unwrapOr("");
    if (content.empty()) {
        log::debug("[ButtonLayoutManager] no se encontro archivo defaults");
        return;
    }
    parseLayoutFileContent(content, m_defaults);
    log::info("[ButtonLayoutManager] cargados {} defaults escena", m_defaults.size());
}

void ButtonLayoutManager::saveDefaults() {
    auto filePath = geode::Mod::get()->getSaveDir() / "button_defaults.txt";
    std::string data = "# Button defaults (sceneKey|buttonID|x|y|scale|opacity)\n";
    for (auto& [sceneKey, buttons] : m_defaults) {
        for (auto& [buttonID, layout] : buttons) {
            data += sceneKey + "|" + buttonID + "|"
                + std::to_string(layout.position.x) + "|" + std::to_string(layout.position.y) + "|"
                + std::to_string(layout.scale) + "|" + std::to_string(layout.opacity) + "\n";
        }
    }
    auto res = file::writeStringSafe(filePath, data);
    if (!res) {
        log::warn("[ButtonLayoutManager] no se pudo escribir defaults: {}", res.unwrapErr());
        return;
    }
    log::info("[ButtonLayoutManager] defaults botones guardados");
}

std::optional<ButtonLayout> ButtonLayoutManager::getLayout(std::string const& sceneKey, std::string const& buttonID) const {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return std::nullopt;

    auto buttonIt = sceneIt->second.find(buttonID);
    if (buttonIt == sceneIt->second.end()) return std::nullopt;

    return buttonIt->second;
}

void ButtonLayoutManager::setLayout(std::string const& sceneKey, std::string const& buttonID, const ButtonLayout& layout) {
    m_layouts[sceneKey][buttonID] = layout;
    save();
}

void ButtonLayoutManager::removeLayout(std::string const& sceneKey, std::string const& buttonID) {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return;

    sceneIt->second.erase(buttonID);
    if (sceneIt->second.empty()) {
        m_layouts.erase(sceneKey);
    }
    save();
}

bool ButtonLayoutManager::hasCustomLayout(std::string const& sceneKey, std::string const& buttonID) const {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return false;
    return sceneIt->second.find(buttonID) != sceneIt->second.end();
}

void ButtonLayoutManager::resetScene(std::string const& sceneKey) {
    m_layouts.erase(sceneKey);
    save();
}

void ButtonLayoutManager::applyLayoutToMenu(std::string const& sceneKey, cocos2d::CCMenu* menu) {
    if (!menu) return;
    auto children = menu->getChildren();
    if (!children) return;

    for (auto* node : CCArrayExt<cocos2d::CCNode*>(children)) {
        auto item = geode::cast::typeinfo_cast<cocos2d::CCMenuItem*>(node);
        if (!item) continue;
        
        std::string id = item->getID();
        if (id.empty()) continue;

        auto layout = getLayout(sceneKey, id);
        if (layout) {
            item->setPosition(layout->position);
            item->setScale(layout->scale);
            item->setOpacity(static_cast<GLubyte>(layout->opacity * 255.0f));
            
            if (auto spriteExtra = geode::cast::typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
                spriteExtra->m_baseScale = layout->scale;
            }
        }
    }
}

std::optional<ButtonLayout> ButtonLayoutManager::getDefaultLayout(std::string const& sceneKey, std::string const& buttonID) const {
    auto sceneIt = m_defaults.find(sceneKey);
    if (sceneIt == m_defaults.end()) return std::nullopt;
    auto it = sceneIt->second.find(buttonID);
    if (it == sceneIt->second.end()) return std::nullopt;
    return it->second;
}

void ButtonLayoutManager::setDefaultLayoutIfAbsent(std::string const& sceneKey, std::string const& buttonID, const ButtonLayout& layout) {
    auto& sceneMap = m_defaults[sceneKey];
    if (sceneMap.find(buttonID) == sceneMap.end()) {
        sceneMap[buttonID] = layout;
        saveDefaults();
    }
}

void ButtonLayoutManager::setDefaultLayout(std::string const& sceneKey, std::string const& buttonID, const ButtonLayout& layout) {
    m_defaults[sceneKey][buttonID] = layout;
    saveDefaults();
}
