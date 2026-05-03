#pragma once

#include <Geode/Geode.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::menu_layout {

struct MenuButtonLayout {
    cocos2d::CCPoint position = { 0.f, 0.f };
    float scale = 1.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float opacity = 1.f;
    bool hidden = false;
    int layer = 0;
    std::string linkGroup;
    bool hasColor = false;
    cocos2d::ccColor3B color = { 255, 255, 255 };
    std::string fontFile;
};

enum class DrawShapeKind {
    Rectangle,
    RoundedRect,
    Circle,
};

struct DrawShapeLayout {
    std::string id;
    DrawShapeKind kind = DrawShapeKind::RoundedRect;
    cocos2d::CCPoint position = { 0.f, 0.f };
    float scale = 1.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float opacity = 0.75f;
    bool hidden = false;
    float width = 110.f;
    float height = 70.f;
    float cornerRadius = 18.f;
    cocos2d::ccColor3B color = { 90, 220, 255 };
    int zOrder = 0;
    int layer = 0;
    std::string linkGroup;
};

struct EditableMenuButton {
    cocos2d::CCMenu* menu = nullptr;
    cocos2d::CCNode* node = nullptr;
    std::string key;
    std::string label;
};

struct LayoutSnapshot {
    std::unordered_map<std::string, MenuButtonLayout> buttons;
    std::vector<DrawShapeLayout> shapes;
};

class MainMenuLayoutManager {
public:
    static MainMenuLayoutManager& get();

    void load();
    void save();

    std::vector<EditableMenuButton> collectButtons(MenuLayer* layer) const;
    std::vector<EditableMenuButton> collectShapeNodes(MenuLayer* layer) const;
    void captureDefaultsAndApply(MenuLayer* layer);
    void apply(MenuLayer* layer);
    void applyDefaults(MenuLayer* layer);
    void applySnapshot(std::vector<EditableMenuButton> const& buttons, LayoutSnapshot const& snapshot, MenuLayer* layer);
    void commit(std::vector<EditableMenuButton> const& buttons, MenuLayer* layer);
    void resetAll();
    void setCustomFromSnapshot(LayoutSnapshot const& snapshot);
    void syncShapes(MenuLayer* layer, std::vector<DrawShapeLayout> const& shapes);

    std::optional<MenuButtonLayout> getDefaultLayout(std::string const& key) const;
    std::optional<MenuButtonLayout> getCustomLayout(std::string const& key) const;

    static LayoutSnapshot captureSnapshot(std::vector<EditableMenuButton> const& buttons);
    static std::vector<DrawShapeLayout> captureShapes(MenuLayer* layer);
    static MenuButtonLayout readLayout(cocos2d::CCNode* node);
    static void applyLayout(cocos2d::CCNode* node, MenuButtonLayout const& layout);
    static bool isDrawShapeNode(cocos2d::CCNode* node);
    static DrawShapeLayout readShapeLayout(cocos2d::CCNode* node);
    static void applyShapeLayout(cocos2d::CCNode* node, DrawShapeLayout const& layout);
    static std::string createShapeID();

private:
    MainMenuLayoutManager() = default;

    std::filesystem::path configPath() const;
    void ensureLoaded();

    bool m_loaded = false;
    std::unordered_map<std::string, MenuButtonLayout> m_defaults;
    std::unordered_map<std::string, MenuButtonLayout> m_custom;
    std::vector<DrawShapeLayout> m_shapes;
};

} // namespace paimon::menu_layout
