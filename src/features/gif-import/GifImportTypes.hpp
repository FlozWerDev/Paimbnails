#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::gifimport {

enum class BackgroundMode {
    Keep,
    AutoBorder,
};

enum class SamplingMode {
    Pixel,
    Smooth,
};

enum class ImportMode {
    Blocks,
    Art,
    Paint,
    Render,
    Free,
    Circles,
    Blur,
    Vert,
};

enum class GlowMode {
    Off,
    Soft,
    Strong,
};

inline bool usesPaintGeometry(ImportMode mode) {
    return mode == ImportMode::Paint || mode == ImportMode::Render ||
        mode == ImportMode::Free || mode == ImportMode::Circles;
}

// El modo circulos dibuja con la forma, no con la rejilla. No se le rematan las
// costuras con cuadrados —GD pinta el circulo en otra hoja de sprites y el parche
// le quedaria debajo por mucho que se le baje la capa— ni se le pide el aprobado
// de fidelidad de la pintura, que lo unico que conseguiria es mandarlo a bajar la
// resolucion por algo que es a proposito.
inline bool matchesGridExactly(ImportMode mode) {
    return usesPaintGeometry(mode) && mode != ImportMode::Circles;
}

enum class BuildStage {
    Preparing,
    Resizing,
    Palette,
    Geometry,
    Reviewing,
    Refining,
    Done,
};

struct BuildProgress {
    BuildStage stage = BuildStage::Preparing;
    float value = 0.f;
    int pass = 0;
    int passes = 0;
};

inline std::size_t animationEventGroupCount(std::size_t frames, bool loop) {
    if (frames <= 1) return 0;
    return loop ? frames : frames - 1;
}

struct SourceFrame {
    int delayMs = 100;
    std::vector<std::uint8_t> rgba;
};

struct SourceAnimation {
    int width = 0;
    int height = 0;
    std::vector<SourceFrame> frames;
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(Color const&) const = default;
};

// Cada celda es un indice de la paleta, o -1 si ahi no hay nada que pintar.
struct GridFrame {
    int delayMs = 100;
    std::vector<std::int32_t> cells;
};

// Una figura de la biblioteca de decoracion reducida a lo que el trazado
// necesita: que parte de su caja pinta. Las filas van de arriba abajo, igual que
// las de la rejilla.
struct StampMask {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> coverage;

    bool empty() const { return coverage.empty(); }
};

// Un molde ya orientado: el objeto de GD, el giro y el volteo con los que hay
// que soltarlo, y el tamano de su arte una vez girada. Cada orientacion es su
// propia entrada para que la figura del plan solo tenga que decir que caja
// llena, sin arrastrar la trigonometria del emisor.
struct PlanStamp {
    int objectId = 0;
    float baseWidth = 30.f;
    float baseHeight = 30.f;
    // El arte de un objeto casi nunca llena su cuadro, asi que la caja del molde
    // es solo la parte que pinta y esto dice cuanto hay que correr el objeto para
    // que esa parte caiga donde toca, en fracciones de la caja.
    float offsetX = 0.f;
    float offsetY = 0.f;
    float rotation = 0.f;
    bool flipX = false;
    StampMask mask;
};

struct Options {
    int maxDimension = 48;
    int minDimension = 6;
    int maxColors = 16;
    int maxFrames = 60;
    int objectBudget = 12000;
    int alphaThreshold = 96;
    int backgroundTolerance = 28;
    float pixelSize = 6.f;
    BackgroundMode background = BackgroundMode::AutoBorder;
    SamplingMode sampling = SamplingMode::Smooth;
    ImportMode mode = ImportMode::Blocks;
    GlowMode glow = GlowMode::Off;
    bool dither = false;
    bool loop = true;
    bool motion = true;
    float blurRadius = 1.f;
    bool softBackdrop = true;
    // Snapshot of native alpha masks, prepared on the GL thread.
    std::vector<PlanStamp> softStamps;
};

inline bool usesSoftGeometry(ImportMode mode) {
    return mode == ImportMode::Blur || mode == ImportMode::Vert;
}

enum class PrimitiveKind {
    Block,
    Stroke,
    Circle,
    Triangle,
    WideTriangle,
    Glow,
    Stamp,
};

inline constexpr std::size_t kPrimitiveKinds = 7;

struct Primitive {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float rotation = 0.f;
    std::uint16_t color = 0;
    PrimitiveKind kind = PrimitiveKind::Block;
    std::int16_t layer = 0;
    // Solo lo miran las figuras Stamp: indice en `ImportPlan::stamps`. Va al
    // final para no romper las inicializaciones por lista que ya hay.
    std::uint16_t stamp = 0;
};

struct VisibilityTrack {
    std::vector<std::uint64_t> mask;
    std::vector<Primitive> objects;
};

// Donde esta la figura en un frame, en celdas y respecto a la pose de referencia.
struct MotionKey {
    int frame = 0;
    int x = 0;
    int y = 0;
};

// Una silueta que se repetia igual en varios frames movida de sitio. En vez de
// pagar una copia entera por frame se dibuja una vez y la corren triggers Move.
struct MotionTrack {
    std::vector<std::uint64_t> mask;
    std::vector<Primitive> objects;
    std::vector<MotionKey> keys;
};

struct ImportPlan {
    int width = 0;
    int height = 0;
    int sourceFrames = 0;
    int requestedDimension = 0;
    int actualDimension = 0;
    ImportMode mode = ImportMode::Blocks;
    std::string strategy;
    std::vector<Color> palette;
    std::vector<PlanStamp> stamps;
    std::vector<GridFrame> frames;
    std::vector<Primitive> staticObjects;
    std::vector<VisibilityTrack> tracks;
    std::vector<MotionTrack> motionTracks;
    // Desde aqui la paleta son canales de glow: mezclados y a media opacidad.
    std::size_t glowPaletteStart = static_cast<std::size_t>(-1);
    float glowOpacity = 1.f;
    int softBackdropColor = -1;
    std::size_t visualObjects = 0;
    std::size_t triggerObjects = 0;
    std::size_t totalObjects = 0;
    std::size_t blockObjects = 0;
    std::size_t strokeObjects = 0;
    std::size_t circleObjects = 0;
    std::size_t triangleObjects = 0;
    std::size_t glowObjects = 0;
    std::size_t stampObjects = 0;
    std::size_t moveTriggers = 0;
    float similarity = 100.f;
    float geometrySimilarity = 100.f;
    float detailSimilarity = 100.f;
    int renderPasses = 0;

    bool animated() const { return frames.size() > 1; }
};

struct BuildResult {
    ImportPlan plan;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

} // namespace paimon::gifimport
