#pragma once

#include <Geode/DefaultInclude.hpp>
#include <string>
#include <unordered_map>

// gestiona posiciones, escala, y opacidad personalizadas para botones del mod por escena e id.
// posiciones se guardan y cargan desde archivo texto en directorio guardado mod.

struct ButtonLayout {
    cocos2d::CCPoint position;
    float scale = 1.0f;
    float opacity = 1.0f; // 0.0 a 1.0
};

class ButtonLayoutManager {
public:
    static ButtonLayoutManager& get();

    // cargar disenos guardados desde archivo
    void load();
    // guardar disenos actuales a archivo
    void save();
    // cargar/guardar disenos default desde/a archivo
    void loadDefaults();
    void saveDefaults();

    // obtener diseno guardado para boton; retorna nullopt si no personalizado
    std::optional<ButtonLayout> getLayout(std::string const& sceneKey, std::string const& buttonID) const;

    // establecer diseno personalizado para boton
    void setLayout(std::string const& sceneKey, std::string const& buttonID, const ButtonLayout& layout);

    // eliminar diseno personalizado para boton (revertir a default)
    void removeLayout(std::string const& sceneKey, std::string const& buttonID);

    // verificar si escena+boton tiene diseno personalizado
    bool hasCustomLayout(std::string const& sceneKey, std::string const& buttonID) const;

    // resetear todos disenos para escena especifica
    void resetScene(std::string const& sceneKey);

    // aplica disenos guardados a todos los items de un menu
    void applyLayoutToMenu(std::string const& sceneKey, cocos2d::CCMenu* menu);

    // api defaults: posiciones base persistentes independientes de ediciones usuario
    std::optional<ButtonLayout> getDefaultLayout(std::string const& sceneKey, std::string const& buttonID) const;
    // establecer default solo si ausente; evita sobreescribir una vez capturado
    void setDefaultLayoutIfAbsent(std::string const& sceneKey, std::string const& buttonID, const ButtonLayout& layout);
    // sobreescribir default para boton (usado para migraciones/ajustes)
    void setDefaultLayout(std::string const& sceneKey, std::string const& buttonID, const ButtonLayout& layout);

private:
    ButtonLayoutManager() = default;
    bool m_loaded = false;
    // mapa: sceneKey -> (buttonID -> ButtonLayout)
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_layouts;
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_defaults;
};

