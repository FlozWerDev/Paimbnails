#include "MainMenuLayoutManager.hpp"

#include "../ui/MainMenuDrawShapeNode.hpp"

#include <Geode/cocos/menu_nodes/CCMenuItem.h>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/cocos/sprite_nodes/CCSprite.h>
#include <Geode/cocos/sprite_nodes/CCSpriteBatchNode.h>
#include <Geode/cocos/label_nodes/CCLabelBMFont.h>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <unordered_set>

#ifndef _WIN32
#include <cxxabi.h>
#endif

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

    std::string demangleTypeName(char const* name) {
#ifdef _WIN32
        std::string result(name);
        if (result.rfind("class ", 0) == 0) result.erase(0, 6);
        auto pos = result.find_last_of("::");
        if (pos != std::string::npos && pos + 1 < result.size()) {
            return result.substr(pos + 1);
        }
        return result;
#else
        int status = 0;
        char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
        if (status == 0 && demangled) {
            std::string result(demangled);
            free(demangled);
            auto pos = result.find_last_of("::");
            if (pos != std::string::npos && pos + 1 < result.size()) {
                return result.substr(pos + 1);
            }
            return result;
        }
        std::string result(name);
        auto pos = result.find_last_of("::");
        if (pos != std::string::npos && pos + 1 < result.size()) {
            return result.substr(pos + 1);
        }
        return result;
#endif
    }

    CCNode* shapeContainer(CCNode* root, bool createIfMissing) {
        if (!root) return nullptr;
        if (auto* existing = root->getChildByID(kShapeContainerID)) {
            return existing;
        }
        if (!createIfMissing) return nullptr;

        auto* container = CCNode::create();
        container->setID(kShapeContainerID);
        container->setAnchorPoint({ 0.f, 0.f });
        container->setPosition({ 0.f, 0.f });
        container->setContentSize(CCDirector::sharedDirector()->getWinSize());
        root->addChild(container, 0);
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

    std::string buttonKey(CCMenuItem* item, CCNode* root) {
        std::vector<std::string> segments;
        for (CCNode* current = item; current && current != root; current = current->getParent()) {
            segments.push_back(nodeSegment(current));
        }
        std::reverse(segments.begin(), segments.end());
        return fmt::format("{}/{}", MainMenuLayoutManager::rootClassName(root), joinPath(segments));
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

    void addStandaloneNode(CCNode* root, std::vector<EditableMenuButton>& out, char const* id, char const* label) {
        if (!root) return;

        auto* node = root->getChildByIDRecursive(id);
        if (!node) return;

        auto key = fmt::format("{}/labels/{}", MainMenuLayoutManager::rootClassName(root), sanitizeSegment(id));
        if (containsKey(out, key)) return;

        out.push_back({
            nullptr,
            node,
            {},
            key,
            label,
        });
    }

    void collectButtonsRecursive(CCNode* node, CCNode* root, std::vector<EditableMenuButton>& out) {
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
                        {},
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

    bool hasMenuItemAncestor(CCNode* node, CCNode* root) {
        for (CCNode* p = node ? node->getParent() : nullptr; p && p != root; p = p->getParent()) {
            if (typeinfo_cast<CCMenuItem*>(p)) return true;
        }
        return false;
    }

    bool shouldSkipDecorNode(CCNode* node) {
        if (!node) return true;
        auto id = std::string(node->getID());
        // Fondos / overlays de Paimbnails que ocupan casi toda la pantalla
        if (id.find("paimon-levelinfo-pixel-bg") != std::string::npos) return true;
        if (id.find("paimon-levelinfo-pixel-overlay") != std::string::npos) return true;
        if (id.find("paimon-levelinfo-extra-darkness") != std::string::npos) return true;
        if (id == kShapeContainerID) return true;
        return false;
    }

    std::string decorNodePathKey(CCNode* leaf, CCNode* root) {
        std::vector<std::string> segments;
        for (CCNode* current = leaf; current && current != root; current = current->getParent()) {
            segments.push_back(nodeSegment(current));
        }
        std::reverse(segments.begin(), segments.end());
        return fmt::format("{}/decor/{}", MainMenuLayoutManager::rootClassName(root), joinPath(segments));
    }

    std::string trimLabelText(std::string const& text, size_t maxLen) {
        if (text.size() <= maxLen) return text;
        return text.substr(0, maxLen) + "...";
    }

    void gatherDecorFonts(CCNode* node, CCNode* root, std::vector<CCLabelBMFont*>& outFonts) {
        if (!node) return;

        if (node != root && !shouldSkipDecorNode(node)) {
            if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
                if (label->isVisible() && !hasMenuItemAncestor(label, root)) {
                    auto* txt = label->getString();
                    if (txt && txt[0] != '\0') {
                        outFonts.push_back(label);
                    }
                }
            }
        }

        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                gatherDecorFonts(child, root, outFonts);
            }
        }
    }

    /// Agrupa etiquetas BMFont vecinas por padre compartido (horizontal, misma línea).
    void emitGroupedDecorLabels(CCNode* root, std::vector<EditableMenuButton>& out, int& decorCount) {
        if (!root) return;

        std::vector<CCLabelBMFont*> gathered;
        gatherDecorFonts(root, root, gathered);
        std::unordered_map<CCNode*, std::vector<CCLabelBMFont*>> byParent;
        byParent.reserve(32);

        for (auto* lbl : gathered) {
            auto* parent = lbl->getParent();
            if (!parent) continue;
            byParent[parent].push_back(lbl);
        }

        constexpr float kGapPx = 48.f;
        constexpr float kDyPx = 10.f;

        for (auto& [parent, vec] : byParent) {
            (void)parent;
            std::sort(vec.begin(), vec.end(), [](CCLabelBMFont* a, CCLabelBMFont* b) {
                return a->boundingBox().getMinX() < b->boundingBox().getMinX();
            });

            std::vector<std::vector<CCLabelBMFont*>> runs;
            for (auto* cur : vec) {
                if (runs.empty()) {
                    runs.push_back({ cur });
                    continue;
                }

                auto* prev = runs.back().back();
                float gap = cur->boundingBox().getMinX() - prev->boundingBox().getMaxX();
                float dy = std::fabs(cur->getPositionY() - prev->getPositionY());
                if (gap <= kGapPx && dy <= kDyPx) {
                    runs.back().push_back(cur);
                } else {
                    runs.push_back({ cur });
                }
            }

            for (auto const& run : runs) {
                if (decorCount >= 300 || run.empty()) continue;

                CCLabelBMFont* anchor = run.front();

                EditableMenuButton eb;
                eb.menu = nullptr;
                eb.node = anchor;
                for (size_t i = 1; i < run.size(); ++i) {
                    eb.labelGroupFollowers.push_back(run[i]);
                }

                std::string stitched;
                for (auto* l : run) {
                    auto* s = l->getString();
                    if (s) stitched += std::string(s);
                }
                while (!stitched.empty() && stitched.front() == ' ') stitched.erase(stitched.begin());
                while (!stitched.empty() && stitched.back() == ' ') stitched.pop_back();
                eb.key = decorNodePathKey(anchor, root);
                eb.label = fmt::format("Label: {}", trimLabelText(stitched.empty() ? "text" : stitched, 48));

                if (containsKey(out, eb.key)) continue;

                out.push_back(std::move(eb));
                ++decorCount;
            }
        }
    }

    void collectDecorSpritesRecursive(CCNode* node, CCNode* root, std::vector<EditableMenuButton>& out, int& decorCount) {
        if (!node || decorCount >= 300) return;

        // Los CCLabelBMFont ya los maneja emitGroupedDecorLabels. Ademas heredan
        // de CCSpriteBatchNode y contienen un CCSprite por glifo, asi que si
        // recurrimos en sus hijos obtendriamos un entry (y por tanto un highlight)
        // por cada letra de la palabra. Cortamos la recursion aqui.
        if (typeinfo_cast<CCLabelBMFont*>(node)) {
            return;
        }

        // CCSpriteBatchNode es un contenedor cuyos hijos son sprites batched
        // (similar caso: cada glifo/particula es un CCSprite). No descendemos.
        bool isBatchNode = typeinfo_cast<CCSpriteBatchNode*>(node) != nullptr;

        if (node != root && !shouldSkipDecorNode(node)) {
            if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
                if (sprite->isVisible() && !hasMenuItemAncestor(sprite, root)) {
                    auto sz = sprite->getContentSize();
                    float w = std::abs(sz.width * sprite->getScaleX());
                    float h = std::abs(sz.height * sprite->getScaleY());
                    if (w >= 10.f && h >= 10.f && w <= 900.f && h <= 900.f) {
                        auto key = decorNodePathKey(sprite, root);
                        if (!containsKey(out, key)) {
                            std::string slug;
                            auto nodeId = std::string(sprite->getID());
                            if (!nodeId.empty()) {
                                slug = sanitizeSegment(nodeId);
                            } else if (auto* fr = sprite->displayFrame()) {
                                gd::string const& fn = fr->getFrameName();
                                if (!fn.empty()) {
                                    slug = sanitizeSegment(std::string(fn.c_str()));
                                }
                            }
                            if (slug.empty()) slug = "icon";
                            out.push_back({
                                nullptr,
                                sprite,
                                {},
                                key,
                                fmt::format("Sprite: {}", trimLabelText(slug, 40)),
                            });
                            ++decorCount;
                        }
                    }
                }
            }
        }

        if (isBatchNode) return;

        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                collectDecorSpritesRecursive(child, root, out, decorCount);
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
    m_labelFollowerOffsets.clear();

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

    if (auto loff = root["labelFollowerOffsets"].asArray()) {
        for (auto const& entry : loff.unwrap()) {
            auto key = entry["key"].asString().unwrapOr("");
            if (key.empty()) continue;
            auto oaOpt = entry["offsets"].asArray();
            if (!oaOpt) continue;
            std::vector<CCPoint> pts;
            for (auto const& pt : oaOpt.unwrap()) {
                float ox = static_cast<float>(pt["x"].asDouble().unwrapOr(0.0));
                float oy = static_cast<float>(pt["y"].asDouble().unwrapOr(0.0));
                pts.emplace_back(ox, oy);
            }
            m_labelFollowerOffsets[key] = std::move(pts);
        }
    }
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

    matjson::Value loff = matjson::Value::array();
    for (auto const& [key, offs] : m_labelFollowerOffsets) {
        if (offs.empty()) continue;
        matjson::Value entry = matjson::makeObject({});
        entry["key"] = key;
        matjson::Value arr = matjson::Value::array();
        for (auto const& p : offs) {
            matjson::Value xy = matjson::makeObject({});
            xy["x"] = static_cast<double>(p.x);
            xy["y"] = static_cast<double>(p.y);
            arr.push(std::move(xy));
        }
        entry["offsets"] = std::move(arr);
        loff.push(std::move(entry));
    }
    root["labelFollowerOffsets"] = loff;

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

std::vector<EditableMenuButton> MainMenuLayoutManager::collectButtons(CCNode* root) const {
    std::vector<EditableMenuButton> buttons;
    if (!root) return buttons;

    collectButtonsRecursive(root, root, buttons);
    addStandaloneNode(root, buttons, "main-title", "Geometry Dash Title");
    addStandaloneNode(root, buttons, "player-username", "Profile Username");
    int decorCount = 0;
    emitGroupedDecorLabels(root, buttons, decorCount);
    collectDecorSpritesRecursive(root, root, buttons, decorCount);
    return buttons;
}

std::vector<EditableMenuButton> MainMenuLayoutManager::collectShapeNodes(CCNode* root) const {
    std::vector<EditableMenuButton> out;
    auto* container = shapeContainer(root, false);
    if (!container) return out;

    auto prefix = fmt::format("{}/shapes/", rootClassName(root));
    if (auto* children = container->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!isDrawShapeNode(child)) continue;
            auto shape = readShapeLayout(child);
            out.push_back({
                nullptr,
                child,
                {},
                fmt::format("{}{}", prefix, shape.id),
                fmt::format("Paimon Draw / {}", shape.id),
            });
        }
    }
    return out;
}

void MainMenuLayoutManager::captureDefaultsAndApply(CCNode* root) {
    this->ensureLoaded();
    if (!root) return;

    auto buttons = this->collectButtons(root);
    bool changed = false;

    // El editor de layout solo opera sobre MenuLayer y PauseLayer; ambas
    // tienen layouts estables entre sesiones, asi que no necesitamos el
    // path de "scene dinamica" (antes usado para LevelInfoLayer).

    for (auto const& button : buttons) {
        if (!button.node) continue;
        if (m_defaults.find(button.key) != m_defaults.end()) continue;
        m_defaults[button.key] = this->readLayout(button.node);
        if (!button.labelGroupFollowers.empty()) {
            CCPoint anch = button.node->getPosition();
            std::vector<CCPoint> offs;
            offs.reserve(button.labelGroupFollowers.size());
            for (auto& f : button.labelGroupFollowers) {
                if (!f) continue;
                offs.push_back(ccpSub(f->getPosition(), anch));
            }
            m_labelFollowerOffsets[button.key] = std::move(offs);
        }
        changed = true;
    }

    for (auto const& button : buttons) {
        if (!button.node || button.labelGroupFollowers.empty()) continue;
        auto itOff = m_labelFollowerOffsets.find(button.key);
        if (itOff != m_labelFollowerOffsets.end() &&
            itOff->second.size() == button.labelGroupFollowers.size()) {
            continue;
        }
        CCPoint anch = button.node->getPosition();
        std::vector<CCPoint> offs;
        offs.reserve(button.labelGroupFollowers.size());
        for (auto& f : button.labelGroupFollowers) {
            if (!f) continue;
            offs.push_back(ccpSub(f->getPosition(), anch));
        }
        m_labelFollowerOffsets[button.key] = std::move(offs);
        changed = true;
    }

    if (changed) {
        this->save();
    }

    // Recolecta los menus que contienen botones con layout custom para
    // desactivar su AxisLayout automatico ANTES de aplicar posiciones.
    // Si no se desactiva, cualquier llamada posterior a updateLayout()
    // (por Geode internamente o por otro hook) resetea las posiciones
    // custom y los botones aparecen "desordenados" al reiniciar.
    std::unordered_set<CCMenu*> menusWithCustom;
    for (auto const& button : buttons) {
        if (!button.node) continue;
        if (m_custom.find(button.key) == m_custom.end()) continue;
        if (button.menu) {
            menusWithCustom.insert(button.menu);
        }
    }
    for (auto* menu : menusWithCustom) {
        if (menu && menu->getLayout()) {
            menu->setLayout(nullptr);
        }
    }

    for (auto const& button : buttons) {
        if (!button.node) continue;

        auto it = m_custom.find(button.key);
        if (it == m_custom.end()) continue;

        MenuButtonLayout effective = it->second;

        MainMenuLayoutManager::applyLayout(button, effective);
    }

    this->syncShapes(root, m_shapes);
}

void MainMenuLayoutManager::apply(CCNode* root) {
    this->captureDefaultsAndApply(root);
}

void MainMenuLayoutManager::applyDefaults(CCNode* root) {
    this->ensureLoaded();
    if (!root) return;

    auto buttons = this->collectButtons(root);
    bool changed = false;

    for (auto const& button : buttons) {
        if (!button.node) continue;
        if (m_defaults.find(button.key) != m_defaults.end()) continue;
        m_defaults[button.key] = this->readLayout(button.node);
        if (!button.labelGroupFollowers.empty()) {
            CCPoint anch = button.node->getPosition();
            std::vector<CCPoint> offs;
            offs.reserve(button.labelGroupFollowers.size());
            for (auto& f : button.labelGroupFollowers) {
                if (!f) continue;
                offs.push_back(ccpSub(f->getPosition(), anch));
            }
            m_labelFollowerOffsets[button.key] = std::move(offs);
        }
        changed = true;
    }

    if (changed) {
        this->save();
    }

    for (auto const& button : buttons) {
        if (!button.node) continue;

        auto it = m_defaults.find(button.key);
        if (it == m_defaults.end()) continue;
        MainMenuLayoutManager::applyLayout(button, it->second);
    }

    this->syncShapes(root, {});
}

void MainMenuLayoutManager::applySnapshot(std::vector<EditableMenuButton> const& buttons, LayoutSnapshot const& snapshot, CCNode* root) {
    this->ensureLoaded();

    for (auto const& button : buttons) {
        if (!button.node) continue;

        auto it = snapshot.buttons.find(button.key);
        if (it != snapshot.buttons.end()) {
            MainMenuLayoutManager::applyLayout(button, it->second);
            continue;
        }

        if (auto def = this->getDefaultLayout(button.key)) {
            MainMenuLayoutManager::applyLayout(button, *def);
        }
    }

    this->syncShapes(root, snapshot.shapes);
}

void MainMenuLayoutManager::commit(std::vector<EditableMenuButton> const& buttons, CCNode* root) {
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

        // El editor solo se usa en escenas estables (MenuLayer y PauseLayer),
        // asi que la posicion live se almacena tal cual.
        MenuButtonLayout toStore = current;

        if (approximatelyEqual(toStore, defaultIt->second)) {
            auto customIt = m_custom.find(button.key);
            if (customIt != m_custom.end()) {
                m_custom.erase(customIt);
                changed = true;
            }
            continue;
        }

        auto customIt = m_custom.find(button.key);
        if (customIt == m_custom.end() || !approximatelyEqual(customIt->second, toStore)) {
            m_custom[button.key] = toStore;
            changed = true;
        }
    }

    auto capturedShapes = captureShapes(root);
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

        MenuButtonLayout toStore = layout;

        if (!def || !approximatelyEqual(toStore, *def)) {
            m_custom[key] = toStore;
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

std::optional<MenuButtonLayout> MainMenuLayoutManager::getSessionDefaultLayout(std::string const& key) const {
    auto it = m_sessionDefaults.find(key);
    if (it == m_sessionDefaults.end()) return std::nullopt;
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

std::vector<DrawShapeLayout> MainMenuLayoutManager::captureShapes(CCNode* root) {
    std::vector<DrawShapeLayout> shapes;
    auto* container = shapeContainer(root, false);
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

void MainMenuLayoutManager::ensureLabelFollowerOffsets(EditableMenuButton const& eb) {
    if (!eb.node || eb.labelGroupFollowers.empty()) return;
    auto it = m_labelFollowerOffsets.find(eb.key);
    if (it != m_labelFollowerOffsets.end() && it->second.size() == eb.labelGroupFollowers.size()) {
        return;
    }

    CCPoint anch = eb.node->getPosition();
    std::vector<CCPoint> offs(eb.labelGroupFollowers.size());
    for (size_t i = 0; i < eb.labelGroupFollowers.size(); ++i) {
        auto& f = eb.labelGroupFollowers[i];
        if (f) offs[i] = ccpSub(f->getPosition(), anch);
    }
    m_labelFollowerOffsets[eb.key] = std::move(offs);
}

void MainMenuLayoutManager::syncLabelFollowerNodes(EditableMenuButton const& eb) {
    if (!eb.node || eb.labelGroupFollowers.empty()) return;

    ensureLabelFollowerOffsets(eb);
    auto it = m_labelFollowerOffsets.find(eb.key);
    if (it == m_labelFollowerOffsets.end() || it->second.size() != eb.labelGroupFollowers.size()) {
        return;
    }

    auto* anchorParent = eb.node->getParent();
    CCPoint anch = eb.node->getPosition();
    GLubyte anchorOp = 255;
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(eb.node.data())) {
        anchorOp = rgba->getOpacity();
    }
    bool const show = eb.node->isVisible();

    for (size_t i = 0; i < eb.labelGroupFollowers.size(); ++i) {
        auto& fol = eb.labelGroupFollowers[i];
        if (!fol || fol->getParent() != anchorParent) continue;
        fol->setPosition(ccpAdd(anch, it->second[i]));
        fol->setVisible(show);
        if (auto* fr = typeinfo_cast<CCRGBAProtocol*>(fol.data())) {
            fr->setOpacity(anchorOp);
        }
    }
}

void MainMenuLayoutManager::applyLayout(EditableMenuButton const& button, MenuButtonLayout const& layout) {
    MainMenuLayoutManager::applyLayout(button.node, layout);
    if (button.labelGroupFollowers.empty()) return;
    MainMenuLayoutManager::get().syncLabelFollowerNodes(button);
}

void MainMenuLayoutManager::rebuildLabelFollowerOffsets(EditableMenuButton const& button) {
    auto& mgr = MainMenuLayoutManager::get();
    mgr.m_labelFollowerOffsets.erase(button.key);
    mgr.ensureLabelFollowerOffsets(button);
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

void MainMenuLayoutManager::syncShapes(CCNode* root, std::vector<DrawShapeLayout> const& shapes) {
    auto* container = shapeContainer(root, !shapes.empty());
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

std::string MainMenuLayoutManager::rootClassName(CCNode* root) {
    if (!root) return "Unknown";
    return demangleTypeName(typeid(*root).name());
}

} // namespace paimon::menu_layout
