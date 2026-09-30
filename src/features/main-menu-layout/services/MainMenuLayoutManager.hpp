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
    // retained via ref<>: nodes can be freed (unloading scenes, playlayer entry)
    // while the editor is alive with in-flight scheduler updates.
    geode::Ref<cocos2d::CCNode> node;
    // extra labels on the same line following anchor `node`.
    std::vector<geode::Ref<cocos2d::CCNode>> labelGroupFollowers;
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

    std::vector<EditableMenuButton> collectButtons(cocos2d::CCNode* root) const;
    void captureDefaultsAndApply(cocos2d::CCNode* root);
    void applyDefaults(cocos2d::CCNode* root);
    void applySnapshot(std::vector<EditableMenuButton> const& buttons, LayoutSnapshot const& snapshot, cocos2d::CCNode* root);
    void commit(std::vector<EditableMenuButton> const& buttons, cocos2d::CCNode* root);
    void resetAll();
    // merge: update only the given keys, erase from custom when == default.
    // leaves other scenes and shapes alone; used by the editor on save.
    void mergeCustomFromButtons(std::unordered_map<std::string, MenuButtonLayout> const& buttons);
    void syncShapes(cocos2d::CCNode* root, std::vector<DrawShapeLayout> const& shapes);

    std::optional<MenuButtonLayout> getDefaultLayout(std::string const& key) const;
    std::optional<MenuButtonLayout> getCustomLayout(std::string const& key) const;
    // session default (dynamic scenes); nullopt when not captured.
    std::optional<MenuButtonLayout> getSessionDefaultLayout(std::string const& key) const;

    static std::vector<DrawShapeLayout> captureShapes(cocos2d::CCNode* root);
    static std::string rootClassName(cocos2d::CCNode* root);
    static MenuButtonLayout readLayout(cocos2d::CCNode* node);
    static void applyLayout(cocos2d::CCNode* node, MenuButtonLayout const& layout);
    static void applyLayout(EditableMenuButton const& button, MenuButtonLayout const& layout);
    static bool isDrawShapeNode(cocos2d::CCNode* node);
    static DrawShapeLayout readShapeLayout(cocos2d::CCNode* node);
    static void applyShapeLayout(cocos2d::CCNode* node, DrawShapeLayout const& layout);

private:
    MainMenuLayoutManager() = default;

    std::filesystem::path configPath() const;
    void ensureLoaded();
    void ensureLabelFollowerOffsets(EditableMenuButton const& eb);
    void syncLabelFollowerNodes(EditableMenuButton const& eb);

    bool m_loaded = false;
    std::unordered_map<std::string, MenuButtonLayout> m_defaults;
    std::unordered_map<std::string, MenuButtonLayout> m_custom;
    std::vector<DrawShapeLayout> m_shapes;
    std::unordered_map<std::string, std::vector<cocos2d::CCPoint>> m_labelFollowerOffsets;
    // not persisted; keeps custom offsets right when gd reorders buttons.
    std::unordered_map<std::string, MenuButtonLayout> m_sessionDefaults;
};

} // namespace paimon::menu_layout
