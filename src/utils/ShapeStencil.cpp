#include "ShapeStencil.hpp"
#include "PaimonDrawNode.hpp"
#include <cmath>

using namespace cocos2d;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Todos los shapes se dibujan centrados en el area de dibujo (offset = half,half).
// El caller NO mueve el nodo stencil; los vertices ya estan en la posicion correcta.

// Helper: poligono regular centrado en el area
static CCDrawNode* drawRegularPolygon(float half, int sides, float radius) {
    auto draw = PaimonDrawNode::create();
    std::vector<CCPoint> verts;
    verts.reserve(sides);
    for (int i = 0; i < sides; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / sides - M_PI / 2.0);
        verts.push_back(ccp(half + radius * cosf(angle), half + radius * sinf(angle)));
    }
    draw->drawPolygon(verts.data(), sides, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: estrella centrada en el area
static CCDrawNode* drawStar(float half, int points, float outerR, float innerR) {
    auto draw = PaimonDrawNode::create();
    int totalVerts = points * 2;
    std::vector<CCPoint> verts;
    verts.reserve(totalVerts);
    for (int i = 0; i < totalVerts; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / totalVerts - M_PI / 2.0);
        float r = (i % 2 == 0) ? outerR : innerR;
        verts.push_back(ccp(half + r * cosf(angle), half + r * sinf(angle)));
    }
    draw->drawPolygon(verts.data(), totalVerts, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: corazon centrado en el area
static CCDrawNode* drawHeart(float half, float size) {
    auto draw = PaimonDrawNode::create();
    const int segments = 60;
    std::vector<CCPoint> verts;
    verts.reserve(segments);
    for (int i = 0; i < segments; i++) {
        float t = static_cast<float>(2.0 * M_PI * i / segments);
        float x = 16.0f * powf(sinf(t), 3.0f);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2*t) - 2.0f * cosf(3*t) - cosf(4*t);
        float scale = size / 36.0f;
        verts.push_back(ccp(half + x * scale, half + y * scale));
    }
    draw->drawPolygon(verts.data(), segments, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: circulo suave (128 segmentos)
static CCDrawNode* drawCircle(float half, float radius) {
    auto draw = PaimonDrawNode::create();
    const int segments = 128;
    std::vector<CCPoint> verts;
    verts.reserve(segments);
    for (int i = 0; i < segments; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / segments);
        verts.push_back(ccp(half + radius * cosf(angle), half + radius * sinf(angle)));
    }
    draw->drawPolygon(verts.data(), segments, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: cuadrado con esquinas redondeadas
static CCDrawNode* drawRoundedSquare(float size, float radius) {
    auto draw = PaimonDrawNode::create();
    float h = size * 0.5f;
    float r = std::min(radius, h * 0.5f);
    std::vector<CCPoint> verts;
    const int arcSegs = 16;
    // Esquina inferior derecha
    for (int i = 0; i <= arcSegs; i++) {
        float a = -M_PI / 2.0f + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(h - r + r * cosf(a), h - r + r * sinf(a)));
    }
    // Esquina inferior izquierda
    for (int i = 0; i <= arcSegs; i++) {
        float a = -M_PI + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(r + r * cosf(a), h - r + r * sinf(a)));
    }
    // Esquina superior izquierda
    for (int i = 0; i <= arcSegs; i++) {
        float a = -3.0f * M_PI / 2.0f + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(r + r * cosf(a), size - h + r + r * sinf(a)));
    }
    // Esquina superior derecha
    for (int i = 0; i <= arcSegs; i++) {
        float a = 0.0f + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(size - h + r + r * cosf(a), size - h + r + r * sinf(a)));
    }
    draw->drawPolygon(verts.data(), verts.size(), ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: pill (rectangulo redondeado como capsula)
static CCDrawNode* drawPill(float half, float width, float height) {
    auto draw = PaimonDrawNode::create();
    float r = std::min(width, height) * 0.5f;
    float hw = width * 0.5f - r;
    float hh = height * 0.5f - r;
    std::vector<CCPoint> verts;
    const int arcSegs = 24;
    // Semicirculo derecho
    for (int i = 0; i <= arcSegs; i++) {
        float a = -M_PI / 2.0f + M_PI * i / arcSegs;
        verts.push_back(ccp(half + hw + r + r * cosf(a), half + hh + r * sinf(a)));
    }
    // Semicirculo izquierdo
    for (int i = 0; i <= arcSegs; i++) {
        float a = M_PI / 2.0f + M_PI * i / arcSegs;
        verts.push_back(ccp(half + r + r * cosf(a), half + hh + r * sinf(a)));
    }
    draw->drawPolygon(verts.data(), verts.size(), ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: arco / media luna invertida
static CCDrawNode* drawArch(float half, float size) {
    auto draw = PaimonDrawNode::create();
    float r = size * 0.5f;
    float thick = r * 0.35f;
    std::vector<CCPoint> outer;
    std::vector<CCPoint> inner;
    const int segs = 48;
    // Arco superior
    for (int i = 0; i <= segs; i++) {
        float a = M_PI * i / segs;
        outer.push_back(ccp(half + r * cosf(a), half + r * sinf(a)));
        inner.push_back(ccp(half + (r - thick) * cosf(a), half + (r - thick) * sinf(a)));
    }
    std::vector<CCPoint> verts = outer;
    for (int i = (int)inner.size() - 1; i >= 0; i--) verts.push_back(inner[i]);
    draw->drawPolygon(verts.data(), verts.size(), ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: gota / teardrop
static CCDrawNode* drawTeardrop(float half, float size) {
    auto draw = PaimonDrawNode::create();
    float r = size * 0.4f;
    std::vector<CCPoint> verts;
    const int segs = 48;
    // Semicirculo inferior
    for (int i = 0; i <= segs; i++) {
        float a = -M_PI / 2.0f + M_PI * i / segs;
        verts.push_back(ccp(half + r * cosf(a), half + r * sinf(a) - r * 0.3f));
    }
    // Punta superior
    verts.push_back(ccp(half, half + size * 0.45f));
    draw->drawPolygon(verts.data(), verts.size(), ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: nube (3 circulos superpuestos como un solo poligono)
static CCDrawNode* drawCloud(float half, float size) {
    auto draw = PaimonDrawNode::create();
    float r = size * 0.25f;
    std::vector<CCPoint> verts;
    const int segs = 32;
    // Circulo izquierdo
    for (int i = 0; i <= segs; i++) {
        float a = M_PI * 0.6f + M_PI * 0.8f * i / segs;
        verts.push_back(ccp(half - r * 0.7f + r * cosf(a), half + r * 0.2f + r * sinf(a)));
    }
    // Circulo derecho
    for (int i = 0; i <= segs; i++) {
        float a = -M_PI * 0.2f + M_PI * 0.8f * i / segs;
        verts.push_back(ccp(half + r * 0.7f + r * cosf(a), half + r * 0.2f + r * sinf(a)));
    }
    // Circulo central superior (arco superior)
    for (int i = 0; i <= segs; i++) {
        float a = M_PI * 0.15f + M_PI * 0.7f * i / segs;
        verts.push_back(ccp(half + r * cosf(a), half + r * 0.8f + r * sinf(a)));
    }
    draw->drawPolygon(verts.data(), verts.size(), ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: cruz
static CCDrawNode* drawCross(float half, float size) {
    auto draw = PaimonDrawNode::create();
    float t = size * 0.2f;
    float h = size * 0.5f;
    CCPoint verts[12] = {
        ccp(half - t, half - h), ccp(half + t, half - h), ccp(half + t, half - t), ccp(half + h, half - t),
        ccp(half + h, half + t),   ccp(half + t, half + t),  ccp(half + t, half + h),  ccp(half - t, half + h),
        ccp(half - t, half + t),  ccp(half - h, half + t), ccp(half - h, half - t), ccp(half - t, half - t)
    };
    draw->drawPolygon(verts, 12, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: luna
static CCDrawNode* drawMoon(float half, float size) {
    auto draw = PaimonDrawNode::create();
    float r = size * 0.45f;
    float offset = r * 0.4f;
    std::vector<CCPoint> verts;
    const int segs = 48;
    // Arco exterior
    for (int i = 0; i <= segs; i++) {
        float a = -M_PI * 0.6f + M_PI * 1.2f * i / segs;
        verts.push_back(ccp(half + r * cosf(a), half + r * sinf(a)));
    }
    // Arco interior (curvatura hacia adentro)
    for (int i = segs; i >= 0; i--) {
        float a = -M_PI * 0.5f + M_PI * 1.0f * i / segs;
        float innerR = r * 0.65f;
        verts.push_back(ccp(half + offset + innerR * cosf(a), half + innerR * sinf(a)));
    }
    draw->drawPolygon(verts.data(), verts.size(), ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: escudo
static CCDrawNode* drawShield(float half, float size) {
    auto draw = PaimonDrawNode::create();
    float w = size * 0.45f;
    float h = size * 0.5f;
    std::vector<CCPoint> verts;
    const int segs = 24;
    // Lado izquierdo arriba
    verts.push_back(ccp(half, half + h));
    verts.push_back(ccp(half - w, half + h * 0.6f));
    // Curva inferior izquierda
    for (int i = 0; i <= segs; i++) {
        float a = M_PI + M_PI * 0.5f * i / segs;
        verts.push_back(ccp(half + w * cosf(a), half - h * 0.3f + h * 0.5f * sinf(a)));
    }
    // Punta inferior
    verts.push_back(ccp(half, half - h));
    // Curva inferior derecha
    for (int i = 0; i <= segs; i++) {
        float a = -M_PI * 0.5f + M_PI * 0.5f * i / segs;
        verts.push_back(ccp(half + w * cosf(a), half - h * 0.3f + h * 0.5f * sinf(a)));
    }
    verts.push_back(ccp(half + w, half + h * 0.6f));
    draw->drawPolygon(verts.data(), verts.size(), ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// Helper: insignia / badge (hexagono alargado vertical)
static CCDrawNode* drawBadge(float half, float size) {
    auto draw = PaimonDrawNode::create();
    float w = size * 0.4f;
    float h = size * 0.5f;
    CCPoint verts[6] = {
        ccp(half, half + h), ccp(half + w, half + h * 0.5f), ccp(half + w, half - h * 0.5f),
        ccp(half, half - h), ccp(half - w, half - h * 0.5f), ccp(half - w, half + h * 0.5f)
    };
    draw->drawPolygon(verts, 6, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// === STENCIL ===

CCNode* createShapeStencil(std::string const& shapeName, float size) {
    float half = size / 2.f;
    CCDrawNode* draw = nullptr;

    if (shapeName == "circle") {
        draw = drawCircle(half, half);
    }
    else if (shapeName == "rounded") {
        draw = drawRoundedSquare(size, size * 0.15f);
    }
    else if (shapeName == "square") {
        draw = PaimonDrawNode::create();
        CCPoint rect[4] = { ccp(0, 0), ccp(size, 0), ccp(size, size), ccp(0, size) };
        draw->drawPolygon(rect, 4, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    }
    else if (shapeName == "rectangle") {
        draw = PaimonDrawNode::create();
        float w = size * 0.5f;
        float h = size * 0.35f;
        float cx = half;
        float cy = half;
        CCPoint rect[4] = { ccp(cx - w, cy - h), ccp(cx + w, cy - h), ccp(cx + w, cy + h), ccp(cx - w, cy + h) };
        draw->drawPolygon(rect, 4, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    }
    else if (shapeName == "pill") {
        draw = drawPill(half, size * 0.9f, size * 0.5f);
    }
    else if (shapeName == "triangle") {
        draw = drawRegularPolygon(half, 3, half);
    }
    else if (shapeName == "diamond") {
        draw = drawRegularPolygon(half, 4, half);
    }
    else if (shapeName == "pentagon") {
        draw = drawRegularPolygon(half, 5, half);
    }
    else if (shapeName == "hexagon") {
        draw = drawRegularPolygon(half, 6, half);
    }
    else if (shapeName == "octagon") {
        draw = drawRegularPolygon(half, 8, half);
    }
    else if (shapeName == "arch") {
        draw = drawArch(half, size * 0.9f);
    }
    else if (shapeName == "teardrop") {
        draw = drawTeardrop(half, size * 0.9f);
    }
    else if (shapeName == "cloud") {
        draw = drawCloud(half, size * 0.9f);
    }
    else if (shapeName == "cross") {
        draw = drawCross(half, size * 0.8f);
    }
    else if (shapeName == "moon") {
        draw = drawMoon(half, size * 0.9f);
    }
    else if (shapeName == "shield") {
        draw = drawShield(half, size * 0.9f);
    }
    else if (shapeName == "badge") {
        draw = drawBadge(half, size * 0.9f);
    }
    else if (shapeName == "star") {
        draw = drawStar(half, 5, half, half * 0.4f);
    }
    else if (shapeName == "star6") {
        draw = drawStar(half, 6, half, half * 0.5f);
    }
    else if (shapeName == "heart") {
        draw = drawHeart(half, half);
    }

    if (draw) {
        // El draw node esta centrado en (0,0) con vertices en coordenadas locales.
        // Lo envolvemos en un container con contentSize; el caller posiciona el container.
        auto container = CCNode::create();
        container->setContentSize({size, size});
        container->addChild(draw);
        return container;
    }

    return nullptr;
}

// === BORDER: dibuja solo el contorno de la forma ===

// Helper: genera vertices de un poligono regular centrado en el area
static std::vector<CCPoint> getRegularPolygonVerts(float half, int sides, float radius) {
    std::vector<CCPoint> verts;
    verts.reserve(sides);
    for (int i = 0; i < sides; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / sides - M_PI / 2.0);
        verts.push_back(ccp(half + radius * cosf(angle), half + radius * sinf(angle)));
    }
    return verts;
}

// Helper: vertices de estrella centrada en el area
static std::vector<CCPoint> getStarVerts(float half, int points, float outerR, float innerR) {
    int totalVerts = points * 2;
    std::vector<CCPoint> verts;
    verts.reserve(totalVerts);
    for (int i = 0; i < totalVerts; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / totalVerts - M_PI / 2.0);
        float r = (i % 2 == 0) ? outerR : innerR;
        verts.push_back(ccp(half + r * cosf(angle), half + r * sinf(angle)));
    }
    return verts;
}

// Helper: vertices de corazon centrado en el area
static std::vector<CCPoint> getHeartVerts(float half, float size) {
    const int segments = 60;
    std::vector<CCPoint> verts;
    verts.reserve(segments);
    for (int i = 0; i < segments; i++) {
        float t = static_cast<float>(2.0 * M_PI * i / segments);
        float x = 16.0f * powf(sinf(t), 3.0f);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2*t) - 2.0f * cosf(3*t) - cosf(4*t);
        float scale = size / 36.0f;
        verts.push_back(ccp(half + x * scale, half + y * scale));
    }
    return verts;
}

// Helper: vertices de cruz centrada en el area
static std::vector<CCPoint> getCrossVerts(float half, float size) {
    float t = size * 0.2f;
    float h = size * 0.5f;
    return {
        ccp(half - t, half - h), ccp(half + t, half - h), ccp(half + t, half - t), ccp(half + h, half - t),
        ccp(half + h, half + t), ccp(half + t, half + t), ccp(half + t, half + h), ccp(half - t, half + h),
        ccp(half - t, half + t), ccp(half - h, half + t), ccp(half - h, half - t), ccp(half - t, half - t)
    };
}

// Helper: vertices de cuadrado centrado en el area
static std::vector<CCPoint> getSquareVerts(float half) {
    return {
        ccp(0, 0), ccp(half * 2, 0),
        ccp(half * 2, half * 2), ccp(0, half * 2)
    };
}

// Helper: vertices de rectangulo centrado en el area
static std::vector<CCPoint> getRectVerts(float half, float w, float h) {
    return {
        ccp(half - w, half - h), ccp(half + w, half - h), ccp(half + w, half + h), ccp(half - w, half + h)
    };
}

// Helper: vertices de circulo suave centrado en el area
static std::vector<CCPoint> getCircleVerts(float half, float radius) {
    const int segments = 128;
    std::vector<CCPoint> verts;
    verts.reserve(segments);
    for (int i = 0; i < segments; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / segments);
        verts.push_back(ccp(half + radius * cosf(angle), half + radius * sinf(angle)));
    }
    return verts;
}

// Helper: vertices de rounded square centrado en el area
static std::vector<CCPoint> getRoundedSquareVerts(float half, float size, float radius) {
    float h = size * 0.5f;
    float r = std::min(radius, h * 0.5f);
    std::vector<CCPoint> verts;
    const int arcSegs = 16;
    for (int i = 0; i <= arcSegs; i++) {
        float a = -M_PI / 2.0f + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(h - r + r * cosf(a), h - r + r * sinf(a)));
    }
    for (int i = 0; i <= arcSegs; i++) {
        float a = -M_PI + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(r + r * cosf(a), h - r + r * sinf(a)));
    }
    for (int i = 0; i <= arcSegs; i++) {
        float a = -3.0f * M_PI / 2.0f + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(r + r * cosf(a), size - h + r + r * sinf(a)));
    }
    for (int i = 0; i <= arcSegs; i++) {
        float a = 0.0f + (M_PI / 2.0f) * i / arcSegs;
        verts.push_back(ccp(size - h + r + r * cosf(a), size - h + r + r * sinf(a)));
    }
    return verts;
}

// Obtener vertices de la forma por nombre (centrados en 0,0)
static std::vector<CCPoint> getShapeVerts(std::string const& shapeName, float half) {
    if (shapeName == "circle") return getCircleVerts(half, half);
    if (shapeName == "square") return getSquareVerts(half);
    if (shapeName == "rectangle") return getRectVerts(half, half * 0.7f, half * 0.7f * 0.7f);
    if (shapeName == "rounded") return getRoundedSquareVerts(half, half * 2.0f, half * 0.3f);
    if (shapeName == "triangle") return getRegularPolygonVerts(half, 3, half);
    if (shapeName == "diamond") return getRegularPolygonVerts(half, 4, half);
    if (shapeName == "pentagon") return getRegularPolygonVerts(half, 5, half);
    if (shapeName == "hexagon") return getRegularPolygonVerts(half, 6, half);
    if (shapeName == "octagon") return getRegularPolygonVerts(half, 8, half);
    if (shapeName == "star") return getStarVerts(half, 5, half, half * 0.4f);
    if (shapeName == "star6") return getStarVerts(half, 6, half, half * 0.5f);
    if (shapeName == "heart") return getHeartVerts(half, half);
    if (shapeName == "cross") return getCrossVerts(half, half * 2.0f);
    return {};
}

CCNode* createShapeBorder(std::string const& shapeName, float size, float thickness, ccColor3B color, GLubyte opacity) {
    float half = size / 2.f;
    auto verts = getShapeVerts(shapeName, half);

    if (!verts.empty()) {
        auto draw = PaimonDrawNode::create();
        ccColor4F borderColor = ccc4FFromccc4B(ccc4(color.r, color.g, color.b, opacity));

        for (size_t i = 0; i < verts.size(); i++) {
            size_t next = (i + 1) % verts.size();
            draw->drawSegment(verts[i], verts[next], thickness / 2.f, borderColor);
        }

        auto container = CCNode::create();
        container->setContentSize({size, size});
        container->addChild(draw);
        return container;
    }

    return nullptr;
}

std::vector<std::pair<std::string, std::string>> getGeometricShapes() {
    return {
        {"circle", "Circle"},
        {"rounded", "Rounded"},
        {"square", "Square"},
        {"rectangle", "Rect"},
        {"pill", "Pill"},
        {"triangle", "Triangle"},
        {"diamond", "Diamond"},
        {"pentagon", "Pentagon"},
        {"hexagon", "Hexagon"},
        {"octagon", "Octagon"},
        {"arch", "Arch"},
        {"teardrop", "Drop"},
        {"cloud", "Cloud"},
        {"cross", "Cross"},
        {"moon", "Moon"},
        {"shield", "Shield"},
        {"badge", "Badge"},
        {"star", "Star 5"},
        {"star6", "Star 6"},
        {"heart", "Heart"},
    };
}
