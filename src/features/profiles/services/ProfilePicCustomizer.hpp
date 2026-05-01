#pragma once
#include <Geode/DefaultInclude.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// Configuracion del borde de la foto de perfil
struct PicFrameConfig {
    std::string spriteFrame = "";       // Obsoleto: el borde sigue la forma
    cocos2d::ccColor3B color = {255, 255, 255}; // blanco por defecto
    float opacity = 255.f;
    float thickness = 4.f;              // Grosor del borde
    float offsetX = 0.f;
    float offsetY = 0.f;
};

// Decoracion colocada alrededor de la foto
struct PicDecoration {
    std::string spriteName = "";        // Nombre del sprite de decoracion
    float posX = 0.f;                   // Posicion relativa al centro (-1 a 1)
    float posY = 0.f;
    float scale = 1.f;
    float rotation = 0.f;
    cocos2d::ccColor3B color = {255, 255, 255};
    float opacity = 255.f;
    bool flipX = false;
    bool flipY = false;
    int zOrder = 0;
};

// Tipo de color para iconos
enum class IconColorSource {
    Custom,     // Colores personalizados seleccionados
    Player      // Colores del jugador desde GameManager
};

// Configuracion del icono del juego
struct PicIconConfig {
    int iconId = 0;                    // ID del icono del juego
    int iconType = 0;                  // 0=Cube, 1=Ship, 2=Ball, 3=Ufo, 4=Wave, 5=Robot, 6=Spider, 7=Swing
    cocos2d::ccColor3B color1 = {255, 255, 255};
    cocos2d::ccColor3B color2 = {255, 255, 255};
    bool glowEnabled = false;
    cocos2d::ccColor3B glowColor = {255, 255, 0};
    float scale = 1.f;

    // Fuente de colores (custom o del jugador)
    IconColorSource colorSource = IconColorSource::Custom;
    IconColorSource glowColorSource = IconColorSource::Custom;

    // Animaciones
    int animationType = 0;             // 0=None, 1=Zoom, 2=Rotate, 3=Both, 4=FlipX, 5=FlipY, 6=Shake, 7=Bounce
    float animationSpeed = 1.f;        // Multiplicador de velocidad (0.5 = mas lento, 2 = mas rapido)
    float animationAmount = 1.f;       // Intensidad de la animacion (para zoom: escala adicional, para rot: grados)

    // Icon Image (mostrar icono sobre una imagen/GIF)
    bool iconImageEnabled = false;
    std::string iconImagePath;         // Ruta de la imagen/GIF de fondo
    float iconImageScale = 1.f;        // Escala del icono sobre la imagen
};

// Configuracion de icono personalizado
struct PicCustomIcon {
    std::string spriteName;            // Nombre del sprite
    std::string path;                  // Ruta del archivo
    bool isModAsset = false;           // Si es un asset del mod vs archivo externo
};

// Configuracion completa de la foto de perfil
struct ProfilePicConfig {
    // Tamano y forma
    float scaleX = 1.f;                // Escala horizontal del contenedor
    float scaleY = 1.f;                // Escala vertical del contenedor
    float size = 120.f;                // Tamano base en pixeles
    float rotation = 0.f;              // Rotacion del contenedor (grados)

    // Ajustes de la imagen dentro del stencil
    float imageZoom = 1.f;             // Zoom de la imagen dentro del stencil
    float imageRotation = 0.f;         // Rotacion de la imagen (grados)
    float imageOffsetX = 0.f;          // Desplazamiento de la imagen dentro del stencil
    float imageOffsetY = 0.f;

    // Borde
    bool frameEnabled = false;
    PicFrameConfig frame;

    // Forma geometrica
    std::string stencilSprite = "circle"; // forma geometrica por defecto

    // Decoraciones
    std::vector<PicDecoration> decorations;

    // Offset del contenedor (no usado actualmente)
    float offsetX = 0.f;
    float offsetY = 0.f;

    // Font del boton de perfil
    std::string profileFont = "goldFont.fnt";

    // Modo icono (solo mostrar el icono del usuario en lugar de la imagen)
    bool onlyIconMode = false;
    PicIconConfig iconConfig;          // Configuracion del icono del juego

    // Iconos personalizados cargados por el mod
    std::vector<PicCustomIcon> customIcons;
    int selectedCustomIconIndex = -1;  // Indice del icono personalizado seleccionado (-1 = ninguno)
};

// Preset de configuracion predefinida
struct ProfilePicPreset {
    std::string id;           // id interno unico
    std::string displayName;  // nombre mostrado al usuario
    ProfilePicConfig config;  // configuracion del preset
};

// Categoria de decoraciones
struct DecorationCategory {
    std::string id;
    std::string displayName;
    std::vector<std::pair<std::string, std::string>> decorations; // <spriteName, label>
};

// Gestiona la configuracion de la foto de perfil
class ProfilePicCustomizer {
public:
    static ProfilePicCustomizer& get();

    // Lee y escribe la configuracion
    ProfilePicConfig getConfig() const;
    void setConfig(ProfilePicConfig const& config);

    // Guarda en disco
    void save();
    // Carga desde disco
    void load();
    
    // True si hay cambios pendientes de guardar
    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    // Lista de bordes disponibles
    static std::vector<std::pair<std::string, std::string>> getAvailableFrames();
    // Lista de formas disponibles
    static std::vector<std::pair<std::string, std::string>> getAvailableStencils();
    // Lista de decoraciones disponibles
    static std::vector<std::pair<std::string, std::string>> getAvailableDecorations();
    // Categorias de decoraciones
    static std::vector<DecorationCategory> getDecorationCategories();
    // Presets predefinidos
    static std::vector<ProfilePicPreset> getPresets();
    // Paleta de colores predefinidos
    static std::vector<std::pair<std::string, cocos2d::ccColor3B>> getColorPalette();

    // Iconos del juego disponibles
    static std::vector<std::pair<int, std::string>> getAvailableGameIcons();
    // Fuentes disponibles para el boton de perfil
    static std::vector<std::pair<std::string, std::string>> getAvailableFonts();

private:
    ProfilePicCustomizer();
    ProfilePicConfig m_config;
    bool m_loaded = false;
    bool m_dirty = false;
};
