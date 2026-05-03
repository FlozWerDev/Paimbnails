#include "MainMenuLayoutManager.hpp"

#include "../ui/MainMenuDrawShapeNode.hpp"

#include <Geode/cocos/menu_nodes/CCMenuItem.h>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    constexpr int kConfigVersion = 1;
    constexpr float kPositionEpsilon = 0.05f;
    constexpr float kScaleEpsilon = 0.001f;
    constexpr char const* kShapeNodePrefix = "paimon-draw-shape-";
    constexpr char const* kShapeContainerID = "paimon-draw-shape-container";
    uint64_t s_shapeIDCounter = 0;

    CCNode* shapeContainer(MenuLayer* layer, bool createIfMissing) {
        if (!layer) return nullptr;
        if (auto* existing = layer->getChildByID(kShapeContainerID)) {
            return existing;
        }
        if (!createIfMissing) return nullptr;

        auto* container = CCNode::create();
        container->setID(kShapeContainerID);
        container->setAnchorPoint({ 0.f, 0.f });
        container->setPosition({ 0.f, 0.f });
        container->setContentSize(CCDirector::sharedDirector()->getWinSize());
        layer->addChild(container, 0);
        return container;
    }

    std::string shapeKindToString(DrawShapeKind kind) {
        switch (kind) {
            case DrawShapeKind::Rectangle: return "rect";
            case DrawShapeKind::RoundedRect: return "round";
            case DrawShapeKind::Circle: return "circle";
        }
        return "round";
    }

    DrawShapeKind shapeKindFromString(std::string const& value) {
        if (value == "rect") return DrawShapeKind::Rectangle;
        if (value == "circle") return DrawShapeKind::Circle;
        return DrawShapeKind::RoundedRect;
    }

    uint64_t shapeNumericID(std::string const& id) {
        constexpr char const* prefix = "shape-";
        if (id.rfind(prefix, 0) != 0) return 0;

        try {
            return static_cast<uint64_t>(std::stoull(id.substr(std::char_traits<char>::length(prefix))));
        } catch (...) {
            return 0;
        }
    }

    void syncShapeIDCounter(std::vector<DrawShapeLayout> const& shapes) {
        for (auto const& shape : shapes) {
            s_shapeIDCounter = std::max(s_shapeIDCounter, shapeNumericID(shape.id));
        }
    }

    bool approximatelyEqual(MenuButtonLayout const& a, MenuButtonLayout const& b) {
        return std::abs(a.position.x - b.position.x) <= kPositionEpsilon &&
               std::abs(a.position.y - b.position.y) <= kPositionEpsilon &&
               std::abs(a.scale - b.scale) <= kScaleEpsilon &&
               std::abs(a.scaleX - b.scaleX) <= kScaleEpsilon &&
               std::abs(a.scaleY - b.scaleY) <= kScaleEpsilon &&
               std::abs(a.opacity - b.opacity) <= kScaleEpsilon &&
               a.hidden == b.hidden &&
               a.layer == b.layer &&
               a.linkGroup == b.linkGroup &&
               a.hasColor == b.hasColor &&
               a.color.r == b.color.r &&
               a.color.g == b.color.g &&
               a.color.b == b.color.b &&
               a.fontFile == b.fontFile;
    }

    int childIndex(CCNode* node) {
        if (!node || !node->getParent()) return 0;

        int index = 0;
        if (auto* children = node->getParent()->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;
                if (child == node) break;
                ++index;
            }
        }
        return index;
    }

    std::string sanitizeSegment(std::string segment) {
        std::replace(segment.begin(), segment.end(), '/', '>');
        return segment;
    }

    std::string nodeSegment(CCNode* node) {
        if (!node) return "null";

        auto id = std::string(node->getID());
        if (!id.empty()) {
            return sanitizeSegment(id);
        }

        auto index = childIndex(node);
        if (typeinfo_cast<CCMenuItem*>(node)) {
            return fmt::format("button@{}", index);
        }
        if (typeinfo_cast<CCMenu*>(node)) {
            return fmt::format("menu@{}", index);
        }
        return fmt::format("node@{}", index);
    }

    std::string joinPath(std::vector<std::string> const& segments) {
        std::string result;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i != 0) result += "/";
            result += segments[i];
        }
        return result;
    }

    std::string buttonKey(CCMenuItem* item, MenuLayer* root) {
        std::vector<std::string> segments;
        for (CCNode* current = item; current && current != root; current = current->getParent()) {
            segments.push_back(nodeSegment(current));
        }
        std::reverse(segments.begin(), segments.end());
        return fmt::format("MenuLayer/{}", joinPath(segments));
    }

    std::string buttonLabel(CCMenu* menu, CCNode* node, std::string const& key) {
        auto menuID = std::string(menu ? menu->getID() : "");
        auto itemID = std::string(node ? node->getID() : "");

        if (!menuID.empty() && !itemID.empty()) {
            return fmt::format("{} / {}", menuID, itemID);
        }
        if (!itemID.empty()) {
            return itemID;
        }
        if (!menuID.empty()) {
            return fmt::format("{} / item", menuID);
        }
        return key;
    }

    bool containsKey(std::vector<EditableMenuButton> const& out, std::string const& key) {
        for (auto const& item : out) {
            if (item.key == key) return true;
        }
        return false;
    }

    void addStandaloneNode(MenuLayer* root, std::vector<EditableMenuButton>& out, char const* id, char const* label) {
        if (!root) return;

        auto* node = root->getChildByIDRecursive(id);
        if (!node) return;

        auto key = fmt::format("MenuLayer/labels/{}", sanitizeSegment(id));
        if (containsKey(out, key)) return;

        out.push_back({
            nullptr,
            node,
            key,
            label,
        });
    }

    void collectButtonsRecursive(CCNode* node, MenuLayer* root, std::vector<EditableMenuButton>& out) {
        if (!node) return;

        if (auto* menu = typeinfo_cast<CCMenu*>(node)) {
            if (auto* children = menu->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    auto* item = typeinfo_cast<CCMenuItem*>(child);
                    if (!item) continue;

                    auto key = buttonKey(item, root);
                    out.push_back({
                        menu,
                        item,
                        key,
                        buttonLabel(menu, item, key),
                    });
                }
            }
        }

        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                collectButtonsRecursive(child, root, out);
            }
        }
    }

    MenuButtonLayout parseLayout(matjson::Value const& value) {
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

    matjson::Value toJson(std::string const& key, MenuButtonLayout const& layout) {
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

    DrawShapeLayout parseShape(matjson::Value const& value) {
        DrawShapeLayout layout;
        layout.id = value["id"].asString().unwrapOr("");
        layout.kind = shapeKindFromString(value["kind"].asString().unwrapOr("round"));
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

    matjson::Value shapeToJson(DrawShapeLayout const& layout) {
        matjson::Value value = matjson::makeObject({});
        value["id"] = layout.id;
        value["kind"] = shapeKindToString(layout.kind);
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
}

MainMenuLayoutManager& MainMenuLayoutManager::get() {
    static MainMenuLayoutManager instance;
    return instance;
}

std::filesystem::path MainMenuLayoutManager::configPath() const {
    return Mod::get()->getSaveDir() / "main_menu_layout.json";
}

void MainMenuLayoutManager::ensureLoaded() {
    if (!m_loaded) {
        this->load();
    }
}

void MainMenuLayoutManager::load() {
    if (m_loaded) return;
    m_loaded = true;

    m_defaults.clear();
    m_custom.clear();
    m_shapes.clear();

    auto raw = file::readString(this->configPath()).unwrapOr("");
    if (raw.empty()) {
        return;
    }

    auto parsed = matjson::parse(raw);
    if (parsed.isErr()) {
        log::warn("[MainMenuLayout] Failed to parse layout config");
        return;
    }

    auto root = parsed.unwrap();

    if (auto defaults = root["defaults"].asArray()) {
        for (auto const& entry : defaults.unwrap()) {
            auto key = entry["key"].asString().unwrapOr("");
            if (key.empty()) continue;
            m_defaults[key] = parseLayout(entry);
        }
    }

    if (auto custom = root["custom"].asArray()) {
        for (auto const& entry : custom.unwrap()) {
            auto key = entry["key"].asString().unwrapOr("");
            if (key.empty()) continue;
            m_custom[key] = parseLayout(entry);
        }
    }

    m_shapes.clear();
    if (auto shapes = root["shapes"].asArray()) {
        for (auto const& entry : shapes.unwrap()) {
            auto shape = parseShape(entry);
            if (!shape.id.empty()) {
                m_shapes.push_back(std::move(shape));
            }
        }
    }

    syncShapeIDCounter(m_shapes);
}

void MainMenuLayoutManager::save() {
    this->ensureLoaded();

    matjson::Value root = matjson::makeObject({});
    root["version"] = kConfigVersion;

    matjson::Value defaults = matjson::Value::array();
    for (auto const& [key, layout] : m_defaults) {
        defaults.push(toJson(key, layout));
    }
    root["defaults"] = defaults;

    matjson::Value custom = matjson::Value::array();
    for (auto const& [key, layout] : m_custom) {
        custom.push(toJson(key, layout));
    }
    root["custom"] = custom;

    matjson::Value shapes = matjson::Value::array();
    for (auto const& shape : m_shapes) {
        shapes.push(shapeToJson(shape));
    }
    root["shapes"] = shapes;

    auto path = this->configPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) {
        log::error("[MainMenuLayout] Failed to open config for writing");
        return;
    }

    auto text = root.dump(matjson::TAB_INDENTATION);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::vector<EditableMenuButton> MainMenuLayoutManager::collectButtons(MenuLayer* layer) const {
    std::vector<EditableMenuButton> buttons;
    if (!layer) return buttons;

    collectButtonsRecursive(layer, layer, buttons);
    addStandaloneNode(layer, buttons, "main-title", "Geometry Dash Title");
    addStandaloneNode(layer, buttons, "player-username", "Profile Username");
    return buttons;
}

std::vector<EditableMenuButton> MainMenuLayoutManager::collectShapeNodes(MenuLayer* layer) const {
    std::vector<EditableMenuButton> out;
    auto* container = shapeContainer(layer, false);
    if (!container) return out;

    if (auto* children = container->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!isDrawShapeNode(child)) continue;
            auto shape = readShapeLayout(child);
            out.push_back({
                nullptr,
                child,
                fmt::format("MenuLayer/shapes/{}", shape.id),
                fmt::format("Paimon Draw / {}", shape.id),
            });
        }
    }
    return out;
}

void MainMenuLayoutManager::captureDefaultsAndApply(MenuLayer* layer) {
    this->ensureLoaded();
    if (!layer) return;

    auto buttons = this->collectButtons(layer);
    bool changed = false;

    for (auto const& button : buttons) {
        if (!button.node) continue;
        if (m_defaults.find(button.key) != m_defaults.end()) continue;
        m_defaults[button.key] = this->readLayout(button.node);
        changed = true;
    }

    if (changed) {
        this->save();
    }

    for (auto const& button : buttons) {
        if (!button.node) continue;

        auto it = m_custom.find(button.key);
        if (it == m_custom.end()) continue;
        this->applyLayout(button.node, it->second);
    }

    this->syncShapes(layer, m_shapes);
}

void MainMenuLayoutManager::apply(MenuLayer* layer) {
    this->captureDefaultsAndApply(layer);
}

void MainMenuLayoutManager::applyDefaults(MenuLayer* layer) {
    this->ensureLoaded();
    if (!layer) return;

    auto buttons = this->collectButtons(layer);
    bool changed = false;

    for (auto const& button : buttons) {
        if (!button.node) continue;
        if (m_defaults.find(button.key) != m_defaults.end()) continue;
        m_defaults[button.key] = this->readLayout(button.node);
        changed = true;
    }

    if (changed) {
        this->save();
    }

    for (auto const& button : buttons) {
        if (!button.node) continue;

        auto it = m_defaults.find(button.key);
        if (it == m_defaults.end()) continue;
        this->applyLayout(button.node, it->second);
    }

    this->syncShapes(layer, {});
}

void MainMenuLayoutManager::applySnapshot(std::vector<EditableMenuButton> const& buttons, LayoutSnapshot const& snapshot, MenuLayer* layer) {
    this->ensureLoaded();

    for (auto const& button : buttons) {
        if (!button.node) continue;

        auto it = snapshot.buttons.find(button.key);
        if (it != snapshot.buttons.end()) {
            this->applyLayout(button.node, it->second);
            continue;
        }

        if (auto def = this->getDefaultLayout(button.key)) {
            this->applyLayout(button.node, *def);
        }
    }

    this->syncShapes(layer, snapshot.shapes);
}

void MainMenuLayoutManager::commit(std::vector<EditableMenuButton> const& buttons, MenuLayer* layer) {
    this->ensureLoaded();

    bool changed = false;
    for (auto const& button : buttons) {
        if (!button.node) continue;

        auto current = this->readLayout(button.node);

        auto defaultIt = m_defaults.find(button.key);
        if (defaultIt == m_defaults.end()) {
            m_defaults[button.key] = current;
            changed = true;
            continue;
        }

        if (approximatelyEqual(current, defaultIt->second)) {
            auto customIt = m_custom.find(button.key);
            if (customIt != m_custom.end()) {
                m_custom.erase(customIt);
                changed = true;
            }
            continue;
        }

        auto customIt = m_custom.find(button.key);
        if (customIt == m_custom.end() || !approximatelyEqual(customIt->second, current)) {
            m_custom[button.key] = current;
            changed = true;
        }
    }

    auto capturedShapes = captureShapes(layer);
    if (capturedShapes.size() != m_shapes.size()) {
        m_shapes = std::move(capturedShapes);
        changed = true;
    } else {
        for (size_t i = 0; i < capturedShapes.size(); ++i) {
            auto const& lhs = capturedShapes[i];
            auto const& rhs = m_shapes[i];
            if (lhs.id != rhs.id || lhs.kind != rhs.kind ||
                std::abs(lhs.position.x - rhs.position.x) > kPositionEpsilon ||
                std::abs(lhs.position.y - rhs.position.y) > kPositionEpsilon ||
                std::abs(lhs.scale - rhs.scale) > kScaleEpsilon ||
                std::abs(lhs.scaleX - rhs.scaleX) > kScaleEpsilon ||
                std::abs(lhs.scaleY - rhs.scaleY) > kScaleEpsilon ||
                std::abs(lhs.opacity - rhs.opacity) > kScaleEpsilon ||
                lhs.hidden != rhs.hidden ||
                std::abs(lhs.width - rhs.width) > kPositionEpsilon ||
                std::abs(lhs.height - rhs.height) > kPositionEpsilon ||
                std::abs(lhs.cornerRadius - rhs.cornerRadius) > kPositionEpsilon ||
                lhs.color.r != rhs.color.r || lhs.color.g != rhs.color.g || lhs.color.b != rhs.color.b ||
                lhs.zOrder != rhs.zOrder ||
                lhs.layer != rhs.layer ||
                lhs.linkGroup != rhs.linkGroup) {
                m_shapes = std::move(capturedShapes);
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        this->save();
    }
}

void MainMenuLayoutManager::resetAll() {
    this->ensureLoaded();
    if (m_custom.empty() && m_shapes.empty()) return;
    m_custom.clear();
    m_shapes.clear();
    this->save();
}

void MainMenuLayoutManager::setCustomFromSnapshot(LayoutSnapshot const& snapshot) {
    this->ensureLoaded();
    m_custom.clear();

    for (auto const& [key, layout] : snapshot.buttons) {
        auto def = this->getDefaultLayout(key);
        if (!def || !approximatelyEqual(layout, *def)) {
            m_custom[key] = layout;
        }
    }

    m_shapes = snapshot.shapes;
    syncShapeIDCounter(m_shapes);

    this->save();
}

std::optional<MenuButtonLayout> MainMenuLayoutManager::getDefaultLayout(std::string const& key) const {
    auto it = m_defaults.find(key);
    if (it == m_defaults.end()) return std::nullopt;
    return it->second;
}

std::optional<MenuButtonLayout> MainMenuLayoutManager::getCustomLayout(std::string const& key) const {
    auto it = m_custom.find(key);
    if (it == m_custom.end()) return std::nullopt;
    return it->second;
}

LayoutSnapshot MainMenuLayoutManager::captureSnapshot(std::vector<EditableMenuButton> const& buttons) {
    LayoutSnapshot snapshot;
    for (auto const& button : buttons) {
        if (!button.node) continue;
        snapshot.buttons[button.key] = readLayout(button.node);
    }
    return snapshot;
}

std::vector<DrawShapeLayout> MainMenuLayoutManager::captureShapes(MenuLayer* layer) {
    std::vector<DrawShapeLayout> shapes;
    auto* container = shapeContainer(layer, false);
    if (!container) return shapes;

    if (auto* children = container->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!isDrawShapeNode(child)) continue;
            shapes.push_back(readShapeLayout(child));
        }
    }

    std::sort(shapes.begin(), shapes.end(), [](auto const& a, auto const& b) {
        if (a.zOrder != b.zOrder) return a.zOrder < b.zOrder;
        return a.id < b.id;
    });
    return shapes;
}

MenuButtonLayout MainMenuLayoutManager::readLayout(CCNode* node) {
    MenuButtonLayout layout;
    if (!node) return layout;

    layout.position = node->getPosition();
    layout.scale = node->getScale();
    layout.scaleX = node->getScaleX();
    layout.scaleY = node->getScaleY();
    layout.hidden = !node->isVisible();
    layout.layer = node->getZOrder();

    if (auto* menuItem = typeinfo_cast<CCMenuItem*>(node)) {
        layout.hidden = layout.hidden || !menuItem->isEnabled();
    }

    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
        layout.opacity = static_cast<float>(rgba->getOpacity()) / 255.f;
        layout.color = rgba->getColor();
        layout.hasColor = true;
    } else if (auto* menuSprite = typeinfo_cast<CCMenuItemSprite*>(node)) {
        if (auto* normal = menuSprite->getNormalImage()) {
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(normal)) {
                layout.opacity = static_cast<float>(rgba->getOpacity()) / 255.f;
                layout.color = rgba->getColor();
                layout.hasColor = true;
            }
        }
    }

    if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
        auto* fnt = label->getFntFile();
        layout.fontFile = fnt ? std::string(fnt) : std::string();
    }
    return layout;
}

void MainMenuLayoutManager::applyLayout(CCNode* node, MenuButtonLayout const& layout) {
    if (!node) return;

    node->setPosition(layout.position);
    node->setScale(layout.scale);
    node->setScaleX(layout.scaleX);
    node->setScaleY(layout.scaleY);
    node->setVisible(!layout.hidden);
    node->setZOrder(layout.layer);

    auto opacityByte = static_cast<GLubyte>(std::clamp(layout.opacity, 0.f, 1.f) * 255.f);

    // Helper: recursively set opacity on a node and all its CCRGBAProtocol children.
    // This is needed for complex buttons (e.g. Paimbnails, profile with image)
    // whose child sprites don't inherit opacity automatically.
    std::function<void(CCNode*, GLubyte)> setOpacityRecursive = [&](CCNode* n, GLubyte opacity) {
        if (!n) return;
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(n)) {
            rgba->setOpacity(opacity);
        }
        if (auto* children = n->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                setOpacityRecursive(child, opacity);
            }
        }
    };

    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
        rgba->setOpacity(opacityByte);
        if (layout.hasColor) {
            rgba->setColor(layout.color);
        }
        // Also propagate to children for composite nodes
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                setOpacityRecursive(child, opacityByte);
            }
        }
    } else {
        // Node itself is not CCRGBAProtocol (e.g. CCMenuItemSpriteExtra wrapper).
        // Propagate opacity to all children recursively.
        setOpacityRecursive(node, opacityByte);
        if (layout.hasColor) {
            if (auto* menuSprite = typeinfo_cast<CCMenuItemSprite*>(node)) {
                if (auto* normal = menuSprite->getNormalImage()) {
                    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(normal)) {
                        rgba->setColor(layout.color);
                    }
                }
            }
        }
    }

    if (auto* item = typeinfo_cast<CCMenuItem*>(node)) {
        item->setEnabled(!layout.hidden);
    }

    if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
        if (!layout.fontFile.empty()) {
            label->setFntFile(layout.fontFile.c_str());
        }
    }

    if (auto* spriteExtra = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        spriteExtra->m_baseScale = layout.scaleX;
    }
}

bool MainMenuLayoutManager::isDrawShapeNode(CCNode* node) {
    auto id = std::string(node ? node->getID() : "");
    return node && id.rfind(kShapeNodePrefix, 0) == 0 && typeinfo_cast<MainMenuDrawShapeNode*>(node);
}

DrawShapeLayout MainMenuLayoutManager::readShapeLayout(CCNode* node) {
    if (auto* drawNode = typeinfo_cast<MainMenuDrawShapeNode*>(node)) {
        return drawNode->readLayout();
    }
    return {};
}

void MainMenuLayoutManager::applyShapeLayout(CCNode* node, DrawShapeLayout const& layout) {
    auto* drawNode = typeinfo_cast<MainMenuDrawShapeNode*>(node);
    if (!drawNode) return;
    drawNode->applyLayout(layout);
}

void MainMenuLayoutManager::syncShapes(MenuLayer* layer, std::vector<DrawShapeLayout> const& shapes) {
    auto* container = shapeContainer(layer, !shapes.empty());
    if (!container) return;

    std::unordered_map<std::string, CCNode*> existing;
    if (auto* children = container->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!isDrawShapeNode(child)) continue;
            auto layout = readShapeLayout(child);
            existing[layout.id] = child;
        }
    }

    std::unordered_set<std::string> alive;
    for (auto const& shape : shapes) {
        if (shape.id.empty()) continue;
        alive.insert(shape.id);

        auto it = existing.find(shape.id);
        CCNode* node = it != existing.end() ? it->second : nullptr;
        if (!node) {
            node = MainMenuDrawShapeNode::create(shape);
            container->addChild(node, shape.zOrder);
        }
        applyShapeLayout(node, shape);
    }

    for (auto const& [id, node] : existing) {
        if (!alive.contains(id) && node && node->getParent()) {
            node->removeFromParent();
        }
    }
}

std::string MainMenuLayoutManager::createShapeID() {
    return fmt::format("shape-{}", ++s_shapeIDCounter);
}

} // namespace paimon::menu_layout
