#pragma once

#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <unordered_map>
#include <string>

struct ButtonLayout {
    cocos2d::CCPoint position = cocos2d::CCPointZero;
    float scale = 1.0f;
    float opacity = 1.0f;
};

class ButtonLayoutManager {
public:
    static ButtonLayoutManager& get();

    void load();
    void save();
    void loadDefaults();
    void saveDefaults();

    std::optional<ButtonLayout> getLayout(std::string const& sceneKey, std::string const& buttonID) const;
    void setLayout(std::string const& sceneKey, std::string const& buttonID, ButtonLayout const& layout);
    void removeLayout(std::string const& sceneKey, std::string const& buttonID);
    bool hasCustomLayout(std::string const& sceneKey, std::string const& buttonID) const;
    void resetScene(std::string const& sceneKey);
    void resetAll();

    void applyLayoutToMenu(std::string const& sceneKey, cocos2d::CCMenu* menu);

    std::optional<ButtonLayout> getDefaultLayout(std::string const& sceneKey, std::string const& buttonID) const;
    void setDefaultLayoutIfAbsent(std::string const& sceneKey, std::string const& buttonID, ButtonLayout const& layout);
    void setDefaultLayout(std::string const& sceneKey, std::string const& buttonID, ButtonLayout const& layout);

    void captureSceneDefaults(std::string const& sceneKey, cocos2d::CCMenu* menu);
    void applyLayoutRecursively(std::string const& sceneKey, cocos2d::CCNode* root);
    void captureSceneDefaultsRecursively(std::string const& sceneKey, cocos2d::CCNode* root);

    /// Etiquetas BMFont fuera de CCMenuItem (mismo orden que collectEditableLabels).
    static std::vector<cocos2d::CCLabelBMFont*> collectEditableLabels(cocos2d::CCNode* root);
    static std::string resolveLabelID(cocos2d::CCLabelBMFont* label, int index);
    void applyLayoutsToLabels(std::string const& sceneKey, cocos2d::CCNode* root);
    void captureLabelDefaultsIfAbsent(std::string const& sceneKey, cocos2d::CCNode* root);

    static std::string resolveButtonID(cocos2d::CCMenuItem* item, cocos2d::CCMenu* menu, int menuIndexHint = -1);
    static std::string buildSceneKeyForLayer(cocos2d::CCLayer* layer);

    /// Devuelve true si el ID de boton es "volatil" (basado en indice raw).
    /// Estos IDs no se persisten a disco para evitar que mods que
    /// añadan/quiten botones invaliden el layout guardado.
    static bool isVolatileButtonID(std::string const& id);

private:
    ButtonLayoutManager();
    ~ButtonLayoutManager();
    ButtonLayoutManager(ButtonLayoutManager const&) = delete;
    ButtonLayoutManager& operator=(ButtonLayoutManager const&) = delete;

    struct ButtonInfo {
        std::string buttonID;
        std::string buttonLabel;
        std::string buttonAsset;
    };

    static ButtonInfo extractButtonInfo(cocos2d::CCMenuItem* item, cocos2d::CCMenu* menu);
    static std::string generateButtonID(ButtonInfo const& info, cocos2d::CCMenu* menu, int menuIndexHint);

    void migrateLegacyLayoutFilesIfNeeded();
    void migrateLegacyDefaultsFilesIfNeeded();
    void saveScene(std::string const& sceneKey);
    void saveDefaultsScene(std::string const& sceneKey);

    bool m_loaded = false;
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_layouts;
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_defaults;
};