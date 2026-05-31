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
    // Retain via Ref<> para evitar dangling: algunos nodos (ej. glyphs de
    // CCLabelBMFont, hijos temporales de shapes, o nodos de escenas que se
    // van al cambiar a PlayLayer) pueden liberarse mientras el editor sigue
    // vivo y con updates en vuelo via el scheduler global.
    geode::Ref<cocos2d::CCNode> node;
    /// Labels adicionales (misma línea horizontal) que siguen el ancla `node`; vacío si no agrupa texto.
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
    std::vector<EditableMenuButton> collectShapeNodes(cocos2d::CCNode* root) const;
    void captureDefaultsAndApply(cocos2d::CCNode* root);
    void apply(cocos2d::CCNode* root);
    void applyDefaults(cocos2d::CCNode* root);
    void applySnapshot(std::vector<EditableMenuButton> const& buttons, LayoutSnapshot const& snapshot, cocos2d::CCNode* root);
    void commit(std::vector<EditableMenuButton> const& buttons, cocos2d::CCNode* root);
    void resetAll();
    void setCustomFromSnapshot(LayoutSnapshot const& snapshot);
    void syncShapes(cocos2d::CCNode* root, std::vector<DrawShapeLayout> const& shapes);

    std::optional<MenuButtonLayout> getDefaultLayout(std::string const& key) const;
    std::optional<MenuButtonLayout> getCustomLayout(std::string const& key) const;
    /// Default capturado en la sesion actual (escenas dinamicas). Devuelve
    /// nullopt si la escena no es dinamica o el boton no se capturo.
    std::optional<MenuButtonLayout> getSessionDefaultLayout(std::string const& key) const;

    static LayoutSnapshot captureSnapshot(std::vector<EditableMenuButton> const& buttons);
    static std::vector<DrawShapeLayout> captureShapes(cocos2d::CCNode* root);
    static std::string rootClassName(cocos2d::CCNode* root);
    static MenuButtonLayout readLayout(cocos2d::CCNode* node);
    static void applyLayout(cocos2d::CCNode* node, MenuButtonLayout const& layout);
    static void applyLayout(EditableMenuButton const& button, MenuButtonLayout const& layout);
    /// Rewrite stored follower offsets from the current follower positions vs anchor (e.g. after resize).
    static void rebuildLabelFollowerOffsets(EditableMenuButton const& button);
    static bool isDrawShapeNode(cocos2d::CCNode* node);
    static DrawShapeLayout readShapeLayout(cocos2d::CCNode* node);
    static void applyShapeLayout(cocos2d::CCNode* node, DrawShapeLayout const& layout);
    static std::string createShapeID();

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
    /// Defaults capturados en la sesion actual (NO persistidos). Se usan en
    /// escenas con layouts dinamicos (p.ej. LevelInfoLayer) para que los
    /// customs mantengan el desplazamiento relativo aunque el juego reordene
    /// los botones entre sesiones.
    std::unordered_map<std::string, MenuButtonLayout> m_sessionDefaults;
};

} // namespace paimon::menu_layout
