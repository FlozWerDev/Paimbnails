#include "ButtonLayoutManager.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Layout.hpp>
#include <sstream>
#include <filesystem>
#include <typeinfo>
#include <cctype>
#include <functional>

using namespace geode::prelude;

ButtonLayoutManager::ButtonLayoutManager() = default;
ButtonLayoutManager::~ButtonLayoutManager() = default;

ButtonLayoutManager& ButtonLayoutManager::get() {
    static ButtonLayoutManager instance;
    return instance;
}

namespace {
std::string findFirstLabelRecursive(cocos2d::CCNode* node) {
    if (!node) return "";
    if (auto label = typeinfo_cast<cocos2d::CCLabelBMFont*>(node)) {
        char const* text = label->getString();
        if (text && text[0] != '\0') return text;
    }
    auto children = node->getChildren();
    if (!children) return "";
    for (auto* child : CCArrayExt<cocos2d::CCNode*>(children)) {
        auto nested = findFirstLabelRecursive(child);
        if (!nested.empty()) return nested;
    }
    return "";
}

std::string findFirstAssetRecursive(cocos2d::CCNode* node) {
    if (!node) return "";
    if (auto sprite = typeinfo_cast<cocos2d::CCSprite*>(node)) {
        auto id = sprite->getID();
        if (!id.empty()) return id;
    }
    auto children = node->getChildren();
    if (!children) return "";
    for (auto* child : CCArrayExt<cocos2d::CCNode*>(children)) {
        auto nested = findFirstAssetRecursive(child);
        if (!nested.empty()) return nested;
    }
    return "";
}

std::string sanitizeToken(std::string s) {
    if (s.empty()) return s;
    for (char& c : s) {
        auto uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.')) c = '_';
    }
    return s;
}

std::string sanitizeSceneFileStem(std::string const& sceneKey) {
    auto s = sanitizeToken(sceneKey);
    if (s.empty()) return "scene";
    return s;
}

std::filesystem::path layoutsDir() {
    return geode::Mod::get()->getSaveDir() / "button_layouts";
}

std::filesystem::path defaultsDir() {
    return geode::Mod::get()->getSaveDir() / "button_defaults";
}

std::filesystem::path sceneLayoutPath(std::string const& sceneKey) {
    return layoutsDir() / (sanitizeSceneFileStem(sceneKey) + ".txt");
}

std::filesystem::path sceneDefaultsPath(std::string const& sceneKey) {
    return defaultsDir() / (sanitizeSceneFileStem(sceneKey) + ".txt");
}

/// Formato heredado: sceneKey|buttonID|x|y|scale|opacity
static void parseLegacyLayoutFileContent(std::string const& content,
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>>& outMap) {
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

/// Formato por escena (archivo = una escena): buttonID|x|y|scale|opacity
static void parsePerSceneLayoutContent(std::string const& sceneKey,
    std::string const& content,
    std::unordered_map<std::string, ButtonLayout>& out) {
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream lineStream(line);
        std::string buttonID;
        ButtonLayout layout;
        layout.scale = 1.0f;
        layout.opacity = 1.0f;
        if (std::getline(lineStream, buttonID, '|') &&
            lineStream >> layout.position.x && lineStream.ignore(1) &&
            lineStream >> layout.position.y) {
            if (lineStream.ignore(1) && lineStream >> layout.scale) {
                lineStream.ignore(1);
                lineStream >> layout.opacity;
            }
            (void)sceneKey;
            // Saltar IDs heredados frágiles del formato antiguo
            // (ej: 'menu__idx_3'): el indice raw cambia cuando otros
            // mods añaden/quitan botones, así que la posicion guardada
            // se aplicaria al boton equivocado. La nueva nomenclatura
            // usa '__volatile_idx_' y no se persiste.
            if (buttonID.find("__idx_") != std::string::npos) continue;
            out[buttonID] = layout;
        }
    }
}

} // namespace

ButtonLayoutManager::ButtonInfo ButtonLayoutManager::extractButtonInfo(cocos2d::CCMenuItem* item, cocos2d::CCMenu* menu) {
    ButtonInfo info;

    if (!item) return info;

    info.buttonID = item->getID();

    info.buttonLabel = findFirstLabelRecursive(item);

    if (auto spriteExtra = geode::cast::typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
        if (auto normalImg = spriteExtra->getNormalImage()) {
            info.buttonAsset = normalImg->getID();
            if (info.buttonAsset.empty()) {
                info.buttonAsset = findFirstAssetRecursive(normalImg);
            }
        }
    }
    if (info.buttonAsset.empty()) {
        info.buttonAsset = findFirstAssetRecursive(item);
    }

    return info;
}

std::string ButtonLayoutManager::generateButtonID(ButtonInfo const& info, cocos2d::CCMenu* menu, int menuIndexHint) {
    if (!info.buttonID.empty()) return info.buttonID;

    std::string menuID = (menu && !menu->getID().empty()) ? sanitizeToken(menu->getID()) : "menu";
    if (!info.buttonLabel.empty()) {
        return fmt::format("{}__lbl_{}", menuID, sanitizeToken(info.buttonLabel));
    }
    if (!info.buttonAsset.empty()) {
        std::filesystem::path p(info.buttonAsset);
        auto fileName = geode::utils::string::pathToString(p.filename());
        if (fileName.empty()) fileName = info.buttonAsset;
        return fmt::format("{}__asset_{}", menuID, sanitizeToken(fileName));
    }
    if (menuIndexHint >= 0) {
        // ID basado en indice. Es FRAGIL: cambia cuando otros mods
        // añaden/quitan/reordenan botones del mismo menu. Lo prefijamos
        // con "__volatile" para que `isVolatileButtonID()` lo detecte y
        // saveScene/saveDefaultsScene NO lo persista a disco. Si el
        // usuario edita posiciones de un boton sin ID estable, su edit
        // solo dura mientras la escena esté viva.
        return fmt::format("{}__volatile_idx_{}", menuID, menuIndexHint);
    }
    return "";
}

bool ButtonLayoutManager::isVolatileButtonID(std::string const& id) {
    return id.find("__volatile_") != std::string::npos;
}

std::string ButtonLayoutManager::resolveButtonID(cocos2d::CCMenuItem* item, cocos2d::CCMenu* menu, int menuIndexHint) {
    auto info = extractButtonInfo(item, menu);
    return generateButtonID(info, menu, menuIndexHint);
}

std::string ButtonLayoutManager::buildSceneKeyForLayer(cocos2d::CCLayer* layer) {
    if (!layer) return "unknown-layer";
    auto typeName = sanitizeToken(typeid(*layer).name());
    if (!typeName.empty()) return typeName;
    auto id = sanitizeToken(layer->getID());
    if (!id.empty()) return id;
    return "unknown-layer";
}

void ButtonLayoutManager::migrateLegacyLayoutFilesIfNeeded() {
    auto legacy = geode::Mod::get()->getSaveDir() / "button_layouts.txt";
    std::error_code existsEc;
    if (!std::filesystem::exists(legacy, existsEc) || existsEc) {
        return;
    }
    auto content = file::readString(legacy).unwrapOr("");
    if (content.empty()) {
        std::error_code ec;
        std::filesystem::remove(legacy, ec);
        return;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> parsed;
    parseLegacyLayoutFileContent(content, parsed);
    (void)file::createDirectoryAll(layoutsDir());
    for (auto const& [sceneKey, buttons] : parsed) {
        m_layouts[sceneKey] = buttons;
        saveScene(sceneKey);
    }
    std::error_code ec;
    std::filesystem::remove(legacy, ec);
    log::info("[ButtonLayoutManager] Migrado button_layouts.txt -> button_layouts/*.txt ({} escenas)", parsed.size());
}

void ButtonLayoutManager::migrateLegacyDefaultsFilesIfNeeded() {
    auto legacy = geode::Mod::get()->getSaveDir() / "button_defaults.txt";
    std::error_code existsEc;
    if (!std::filesystem::exists(legacy, existsEc) || existsEc) {
        return;
    }
    auto content = file::readString(legacy).unwrapOr("");
    if (content.empty()) {
        std::error_code ec;
        std::filesystem::remove(legacy, ec);
        return;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> parsed;
    parseLegacyLayoutFileContent(content, parsed);
    (void)file::createDirectoryAll(defaultsDir());
    for (auto const& [sceneKey, buttons] : parsed) {
        m_defaults[sceneKey] = buttons;
        saveDefaultsScene(sceneKey);
    }
    std::error_code ec;
    std::filesystem::remove(legacy, ec);
    log::info("[ButtonLayoutManager] Migrado button_defaults.txt -> button_defaults/*.txt");
}

void ButtonLayoutManager::saveScene(std::string const& sceneKey) {
    (void)file::createDirectoryAll(layoutsDir());
    auto path = sceneLayoutPath(sceneKey);
    auto it = m_layouts.find(sceneKey);
    if (it == m_layouts.end() || it->second.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            std::filesystem::remove(path, ec);
        }
        return;
    }

    std::string data = fmt::format("# Button layouts for scene {}\n# buttonID|x|y|scale|opacity\n", sceneKey);
    for (auto const& [buttonID, layout] : it->second) {
        // Filtrar IDs volatiles (__volatile_idx_N): si los persistimos,
        // cuando otro mod añade/quita un boton el indice cambia y
        // aplicariamos la posicion guardada al boton equivocado.
        if (isVolatileButtonID(buttonID)) continue;
        data += fmt::format("{}|{}|{}|{}|{}\n", buttonID, layout.position.x, layout.position.y, layout.scale, layout.opacity);
    }
    auto res = file::writeStringSafe(path, data);
    if (!res) {
        log::error("[ButtonLayoutManager] no se pudo escribir {}: {}", geode::utils::string::pathToString(path), res.unwrapErr());
    }
}

void ButtonLayoutManager::saveDefaultsScene(std::string const& sceneKey) {
    (void)file::createDirectoryAll(defaultsDir());
    auto path = sceneDefaultsPath(sceneKey);
    auto it = m_defaults.find(sceneKey);
    if (it == m_defaults.end() || it->second.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            std::filesystem::remove(path, ec);
        }
        return;
    }

    std::string data = fmt::format("# Button defaults for scene {}\n# buttonID|x|y|scale|opacity\n", sceneKey);
    for (auto const& [buttonID, layout] : it->second) {
        if (isVolatileButtonID(buttonID)) continue;
        data += fmt::format("{}|{}|{}|{}|{}\n", buttonID, layout.position.x, layout.position.y, layout.scale, layout.opacity);
    }
    auto res = file::writeStringSafe(path, data);
    if (!res) {
        log::warn("[ButtonLayoutManager] no se pudo escribir defaults {}: {}", geode::utils::string::pathToString(path), res.unwrapErr());
    }
}

void ButtonLayoutManager::load() {
    if (m_loaded) return;
    m_loaded = true;
    m_layouts.clear();

    migrateLegacyLayoutFilesIfNeeded();

    auto dir = layoutsDir();
    std::error_code dirEc;
    if (std::filesystem::exists(dir, dirEc) && !dirEc) {
        std::error_code iterEc;
        for (auto const& entry : std::filesystem::directory_iterator(dir, iterEc)) {
            if (iterEc) break;
            if (!entry.is_regular_file()) continue;
            auto p = entry.path();
            if (p.extension() != ".txt") continue;

            std::string sceneKey = geode::utils::string::pathToString(p.stem());
            auto content = file::readString(p).unwrapOr("");
            if (content.empty()) continue;

            std::unordered_map<std::string, ButtonLayout> sceneMap;
            parsePerSceneLayoutContent(sceneKey, content, sceneMap);
            if (!sceneMap.empty()) {
                m_layouts[sceneKey] = std::move(sceneMap);
            }
        }
    }

    log::info("[ButtonLayoutManager] cargados layouts para {} escenas (archivos por capa)", m_layouts.size());
    loadDefaults();
}

void ButtonLayoutManager::save() {
    for (auto const& [sceneKey, _] : m_layouts) {
        saveScene(sceneKey);
    }
}

void ButtonLayoutManager::loadDefaults() {
    m_defaults.clear();
    migrateLegacyDefaultsFilesIfNeeded();

    auto dir = defaultsDir();
    std::error_code dirEc;
    if (!std::filesystem::exists(dir, dirEc) || dirEc) {
        log::debug("[ButtonLayoutManager] no hay directorio button_defaults");
        return;
    }

    std::error_code iterEc;
    for (auto const& entry : std::filesystem::directory_iterator(dir, iterEc)) {
        if (iterEc) break;
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        if (p.extension() != ".txt") continue;

        std::string sceneKey = geode::utils::string::pathToString(p.stem());
        auto content = file::readString(p).unwrapOr("");
        if (content.empty()) continue;

        std::unordered_map<std::string, ButtonLayout> sceneMap;
        parsePerSceneLayoutContent(sceneKey, content, sceneMap);
        if (!sceneMap.empty()) {
            m_defaults[sceneKey] = std::move(sceneMap);
        }
    }

    log::info("[ButtonLayoutManager] cargados defaults para {} escenas", m_defaults.size());
}

void ButtonLayoutManager::saveDefaults() {
    for (auto const& [sceneKey, _] : m_defaults) {
        saveDefaultsScene(sceneKey);
    }
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
    saveScene(sceneKey);
}

void ButtonLayoutManager::removeLayout(std::string const& sceneKey, std::string const& buttonID) {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return;

    sceneIt->second.erase(buttonID);
    if (sceneIt->second.empty()) {
        m_layouts.erase(sceneIt);
    }
    saveScene(sceneKey);
}

bool ButtonLayoutManager::hasCustomLayout(std::string const& sceneKey, std::string const& buttonID) const {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return false;
    return sceneIt->second.find(buttonID) != sceneIt->second.end();
}

void ButtonLayoutManager::resetScene(std::string const& sceneKey) {
    m_layouts.erase(sceneKey);
    saveScene(sceneKey);
}

void ButtonLayoutManager::resetAll() {
    m_layouts.clear();
    auto dir = layoutsDir();
    std::error_code ec;
    if (std::filesystem::exists(dir)) {
        for (auto const& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".txt") {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
}

void ButtonLayoutManager::applyLayoutToMenu(std::string const& sceneKey, cocos2d::CCMenu* menu) {
    if (!menu) return;
    auto children = menu->getChildren();
    if (!children) return;

    // Si el menu tiene un layout activo (AxisLayout, RowLayout,
    // ColumnLayout, AnchorLayout), cualquier setPosition() directo se
    // revierte la siguiente vez que alguien llama updateLayout(). Para
    // que las posiciones custom persistan tenemos que comunicarselas
    // al layout via AnchorLayoutOptions.
    bool const menuHasLayout = (menu->getLayout() != nullptr);

    int idx = 0;
    bool layoutModified = false;
    for (auto* node : CCArrayExt<cocos2d::CCNode*>(children)) {
        auto item = geode::cast::typeinfo_cast<cocos2d::CCMenuItem*>(node);
        if (!item) {
            ++idx;
            continue;
        }

        ButtonInfo info = extractButtonInfo(item, menu);
        int menuIndexHint = idx;
        std::string id = generateButtonID(info, menu, menuIndexHint);
        ++idx;
        if (id.empty()) continue;

        auto layout = getLayout(sceneKey, id);
        if (!layout && !info.buttonLabel.empty()) {
            layout = getLayout(sceneKey, fmt::format("lbl_{}", info.buttonLabel));
        }
        if (!layout && !info.buttonAsset.empty()) {
            std::filesystem::path p(info.buttonAsset);
            layout = getLayout(sceneKey, fmt::format("asset_{}", geode::utils::string::pathToString(p.filename())));
        }

        if (layout) {
            if (menuHasLayout) {
                // Calcular el offset relativo al centro del menu para
                // que AnchorLayoutOptions::Center aplique al boton sin
                // que el layout lo reposicione.
                auto menuSize = menu->getContentSize();
                cocos2d::CCPoint center = ccp(menuSize.width * 0.5f, menuSize.height * 0.5f);
                cocos2d::CCPoint offset = layout->position - center;

                item->setLayoutOptions(
                    geode::AnchorLayoutOptions::create()
                        ->setAnchor(geode::Anchor::Center)
                        ->setOffset(offset)
                );
                layoutModified = true;
            } else {
                // Menu sin layout: setPosition directo es seguro.
                item->setPosition(layout->position);
            }
            item->setScale(layout->scale);
            item->setOpacity(static_cast<GLubyte>(layout->opacity * 255.0f));

            if (auto spriteExtra = geode::cast::typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
                spriteExtra->m_baseScale = layout->scale;
            }
        }
    }

    // Si modificamos LayoutOptions de algun hijo, hay que pedir un
    // updateLayout para que las nuevas opciones tomen efecto.
    if (layoutModified) {
        menu->updateLayout();
    }
}

std::vector<cocos2d::CCLabelBMFont*> ButtonLayoutManager::collectEditableLabels(cocos2d::CCNode* root) {
    std::vector<cocos2d::CCLabelBMFont*> out;
    if (!root) return out;

    std::function<void(cocos2d::CCNode*, bool)> walk = [&](cocos2d::CCNode* node, bool underMenuItem) {
        if (!node) return;
        if (geode::cast::typeinfo_cast<cocos2d::CCMenuItem*>(node)) {
            underMenuItem = true;
        }
        if (auto* lbl = geode::cast::typeinfo_cast<cocos2d::CCLabelBMFont*>(node)) {
            if (!underMenuItem) {
                out.push_back(lbl);
            }
        }
        auto children = node->getChildren();
        if (!children) return;
        for (auto* child : CCArrayExt<cocos2d::CCNode*>(children)) {
            walk(child, underMenuItem);
        }
    };

    walk(root, false);
    return out;
}

std::string ButtonLayoutManager::resolveLabelID(cocos2d::CCLabelBMFont* label, int index) {
    if (!label) return "";
    std::string id = label->getID();
    if (!id.empty()) return sanitizeToken(id);
    char const* s = label->getString();
    std::string text = s ? std::string(s) : "";
    std::string token = sanitizeToken(text);
    if (token.size() > 48) token.resize(48);
    return fmt::format("label_{}_{}", index, token.empty() ? "text" : token);
}

void ButtonLayoutManager::applyLayoutsToLabels(std::string const& sceneKey, cocos2d::CCNode* root) {
    auto labels = collectEditableLabels(root);
    int idx = 0;
    for (auto* lbl : labels) {
        std::string id = resolveLabelID(lbl, idx++);
        auto layout = getLayout(sceneKey, id);
        if (layout) {
            lbl->setPosition(layout->position);
            lbl->setScale(layout->scale);
            lbl->setOpacity(static_cast<GLubyte>(layout->opacity * 255.0f));
        }
    }
}

void ButtonLayoutManager::captureLabelDefaultsIfAbsent(std::string const& sceneKey, cocos2d::CCNode* root) {
    auto labels = collectEditableLabels(root);
    int idx = 0;
    for (auto* lbl : labels) {
        std::string id = resolveLabelID(lbl, idx++);
        if (id.empty()) continue;
        ButtonLayout layout;
        layout.position = lbl->getPosition();
        layout.scale = lbl->getScale();
        layout.opacity = lbl->getOpacity() / 255.0f;
        setDefaultLayoutIfAbsent(sceneKey, id, layout);
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
        saveDefaultsScene(sceneKey);
    }
}

void ButtonLayoutManager::setDefaultLayout(std::string const& sceneKey, std::string const& buttonID, const ButtonLayout& layout) {
    m_defaults[sceneKey][buttonID] = layout;
    saveDefaultsScene(sceneKey);
}

void ButtonLayoutManager::captureSceneDefaults(std::string const& sceneKey, cocos2d::CCMenu* menu) {
    if (!menu) return;
    auto children = menu->getChildren();
    if (!children) return;

    int idx = 0;
    for (auto* node : CCArrayExt<cocos2d::CCNode*>(children)) {
        auto item = geode::cast::typeinfo_cast<cocos2d::CCMenuItem*>(node);
        if (!item) {
            ++idx;
            continue;
        }

        ButtonInfo info = extractButtonInfo(item, menu);
        std::string id = generateButtonID(info, menu, idx);
        ++idx;
        if (id.empty()) continue;

        ButtonLayout layout;
        layout.position = item->getPosition();
        layout.scale = item->getScale();
        layout.opacity = item->getOpacity() / 255.0f;

        setDefaultLayoutIfAbsent(sceneKey, id, layout);
    }
    log::info("[ButtonLayoutManager] Captured defaults for scene '{}'", sceneKey);
}

void ButtonLayoutManager::applyLayoutRecursively(std::string const& sceneKey, cocos2d::CCNode* root) {
    if (!root) return;
    if (auto menu = typeinfo_cast<cocos2d::CCMenu*>(root)) {
        applyLayoutToMenu(sceneKey, menu);
    }
    auto children = root->getChildren();
    if (!children) return;
    for (auto* child : CCArrayExt<cocos2d::CCNode*>(children)) {
        applyLayoutRecursively(sceneKey, child);
    }
}

void ButtonLayoutManager::captureSceneDefaultsRecursively(std::string const& sceneKey, cocos2d::CCNode* root) {
    if (!root) return;
    if (auto menu = typeinfo_cast<cocos2d::CCMenu*>(root)) {
        captureSceneDefaults(sceneKey, menu);
    }
    auto children = root->getChildren();
    if (!children) return;
    for (auto* child : CCArrayExt<cocos2d::CCNode*>(children)) {
        captureSceneDefaultsRecursively(sceneKey, child);
    }
}
