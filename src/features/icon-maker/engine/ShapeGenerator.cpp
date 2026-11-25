#include "ShapeGenerator.hpp"

#include <algorithm>
#include <cmath>

namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float len(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline Vec2 absV(Vec2 a) { return {std::fabs(a.x), std::fabs(a.y)}; }
inline Vec2 maxV(Vec2 a, float v) { return {std::max(a.x, v), std::max(a.y, v)}; }

inline float sdCircle(Vec2 p, float r) {
    return len(p) - r;
}

inline float sdRoundedBox(Vec2 p, Vec2 b, float r) {
    r = std::clamp(r, 0.f, std::min(b.x, b.y));
    Vec2 q = absV(p) - (b - Vec2{r, r});
    return len(maxV(q, 0.f)) + std::min(std::max(q.x, q.y), 0.f) - r;
}

inline float sdSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 pa = p - a;
    Vec2 ba = b - a;
    float denom = dot(ba, ba);
    float h = denom > 1e-6f ? std::clamp(dot(pa, ba) / denom, 0.f, 1.f) : 0.f;
    return len(pa - ba * h);
}

inline float sdCapsule(Vec2 p, Vec2 a, Vec2 b, float r) {
    return sdSegment(p, a, b) - r;
}

inline float sdRegularPolygon(Vec2 p, int n, float r, float rotRad = -kPi * 0.5f) {
    if (n < 3) return sdCircle(p, r);
    float an = kPi / static_cast<float>(n);
    float a = std::atan2(p.y, p.x) - rotRad;
    float sector = 2.f * an;
    a = std::fmod(a, sector);
    if (a < 0.f) a += sector;
    a -= an;
    float d = len(p);
    Vec2 q{d * std::cos(a), std::fabs(d * std::sin(a))};
    float apothem = r * std::cos(an);
    float halfEdge = r * std::sin(an);
    q.x -= apothem;
    q.y -= std::clamp(q.y, 0.f, halfEdge);
    float dist = len(q);
    return (q.x < 0.f) ? -dist : dist;
}

inline float sdStar(Vec2 p, int n, float outerR, float innerRatio) {
    n = std::clamp(n, 3, 12);
    float innerR = outerR * std::clamp(innerRatio, 0.15f, 0.9f);
    float step = kPi / static_cast<float>(n);
    // point straight up (-y in image space -> angle -pi/2)
    float angle = std::atan2(p.y, p.x) + kPi * 0.5f;
    float twoStep = 2.f * step;
    angle = std::fmod(angle, twoStep);
    if (angle < 0.f) angle += twoStep;
    if (angle > step) angle = twoStep - angle;

    float d = len(p);
    Vec2 q{d * std::cos(angle), d * std::sin(angle)};
    Vec2 v0{outerR, 0.f};
    Vec2 v1{innerR * std::cos(step), innerR * std::sin(step)};
    float edgeDist = sdSegment(q, v0, v1);

    // cross product sign tells inside/outside relative to segment v0->v1
    Vec2 e = v1 - v0;
    Vec2 w = q - v0;
    float cross = e.x * w.y - e.y * w.x;
    return cross >= 0.f ? -edgeDist : edgeDist;
}

inline float sdHeart(Vec2 p) {
    // flip y so heart points downward in top-down image coords
    p.y = -p.y;
    p.x = std::fabs(p.x);
    p.y += 0.35f;
    p = p * 1.15f;
    if (p.y + p.x > 1.f) {
        Vec2 d = p - Vec2{0.25f, 0.75f};
        return len(d) - std::sqrt(2.f) / 4.f;
    }
    Vec2 d1 = p - Vec2{0.f, 1.f};
    float k = 0.5f * std::max(p.x + p.y, 0.f);
    Vec2 d2 = p - Vec2{k, k};
    float dist = std::sqrt(std::min(dot(d1, d1), dot(d2, d2)));
    float sign = (p.x - p.y) >= 0.f ? 1.f : -1.f;
    return dist * sign;
}

inline float sdShield(Vec2 p) {
    // classic heraldic shield in [-1, 1]
    float ax = std::fabs(p.x);
    if (p.y < 0.05f) {
        return sdRoundedBox({ax, p.y + 0.3f}, {0.72f, 0.45f}, 0.12f);
    }
    // tapered curved bottom
    float t = std::clamp((p.y - 0.05f) / 0.80f, 0.f, 1.f);
    float halfW = 0.72f * (1.f - t * t);
    float dx = ax - halfW;
    float dy = p.y - 0.85f;
    if (p.y > 0.85f) return len({ax, dy});
    return std::max(dx * 0.78f, p.y - 0.85f);
}

inline float sdTriangle(Vec2 p, float r, float cornerR) {
    // upright triangle in top-down coords (apex at top y = -r)
    Vec2 a{0.f, -r * 0.96f};
    Vec2 b{r * 0.92f, r * 0.78f};
    Vec2 c{-r * 0.92f, r * 0.78f};

    auto edgeDist = [&](Vec2 v0, Vec2 v1) {
        return sdSegment(p, v0, v1);
    };
    float d = std::min({edgeDist(a, b), edgeDist(b, c), edgeDist(c, a)});
    auto cross2 = [](Vec2 u, Vec2 v) { return u.x * v.y - u.y * v.x; };
    bool inside = cross2(b - a, p - a) >= 0.f &&
                  cross2(c - b, p - b) >= 0.f &&
                  cross2(a - c, p - c) >= 0.f;
    return (inside ? -d : d) - cornerR * 0.35f;
}

inline float sdPolygonConv(Vec2 p, std::vector<Vec2> const& pts) {
    float minD = 1e9f;
    bool inside = false;
    std::size_t n = pts.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        minD = std::min(minD, sdSegment(p, pts[j], pts[i]));
        bool cond = ((pts[i].y > p.y) != (pts[j].y > p.y)) &&
                    (p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y) /
                     ((pts[j].y - pts[i].y) + 1e-7f) + pts[i].x);
        if (cond) inside = !inside;
    }
    return inside ? -minD : minD;
}

inline float opUnion(float a, float b) { return std::min(a, b); }
inline float opSubtract(float a, float b) { return std::max(a, -b); }

float evalPrimitive(PieceShape const& shape, Vec2 p) {
    float const r = 0.88f;
    float const cr = std::clamp(shape.cornerRadius, 0.f, 0.48f);
    float d = 1.f;
    switch (shape.primitive) {
        case PrimitiveKind::RoundedSquare:
            d = sdRoundedBox(p, {r, r}, cr * 1.4f);
            break;
        case PrimitiveKind::Circle:
            d = sdCircle(p, r);
            break;
        case PrimitiveKind::Triangle:
            d = sdTriangle(p, r, cr);
            break;
        case PrimitiveKind::Diamond: {
            Vec2 q{(p.x + p.y) * 0.70710678f, (p.y - p.x) * 0.70710678f};
            d = sdRoundedBox(q, {r * 0.68f, r * 0.68f}, cr * 0.9f);
            break;
        }
        case PrimitiveKind::Hexagon:
            d = sdRegularPolygon(p, 6, r, 0.f) - cr * 0.25f;
            break;
        case PrimitiveKind::Octagon:
            d = sdRegularPolygon(p, 8, r, kPi / 8.f) - cr * 0.2f;
            break;
        case PrimitiveKind::Star:
            d = sdStar(p, shape.points, r, shape.innerRatio);
            break;
        case PrimitiveKind::Cross: {
            float arm = std::clamp(shape.innerRatio, 0.15f, 0.65f) * r;
            float h = sdRoundedBox(p, {r, arm}, cr * 0.5f);
            float v = sdRoundedBox(p, {arm, r}, cr * 0.5f);
            d = opUnion(h, v);
            break;
        }
        case PrimitiveKind::Heart:
            d = sdHeart(p);
            break;
        case PrimitiveKind::Capsule:
            d = sdRoundedBox(p, {r, r * 0.42f}, r * 0.42f);
            break;
        case PrimitiveKind::Shield:
            d = sdShield(p);
            break;
        case PrimitiveKind::Crescent:
            d = opSubtract(sdCircle(p, r), sdCircle(p - Vec2{0.34f, -0.14f}, r * 0.80f));
            break;
    }

    if (shape.hollow) {
        float halfStroke = std::clamp(shape.strokeWidth, 0.04f, 0.48f) * 0.55f;
        d = std::fabs(d + halfStroke) - halfStroke;
    }
    return d;
}

float evalDecal(DecalKind decal, Vec2 p) {
    Vec2 sym{std::fabs(p.x), p.y};
    switch (decal) {
        case DecalKind::EyesClassic: {
            // Iconic GD two square eyes
            return sdRoundedBox({sym.x - 0.36f, sym.y + 0.12f}, {0.20f, 0.20f}, 0.03f);
        }
        case DecalKind::EyesAngry: {
            // Fierce slanted eyes
            static std::vector<Vec2> const eye{
                {0.14f, -0.10f}, {0.66f, -0.34f}, {0.60f, 0.06f}, {0.16f, 0.10f}
            };
            return sdPolygonConv(sym, eye) - 0.02f;
        }
        case DecalKind::EyesHappy: {
            // Curved happy ^ ^ eyes
            float d1 = sdCapsule(sym, {0.16f, 0.02f}, {0.38f, -0.22f}, 0.08f);
            float d2 = sdCapsule(sym, {0.38f, -0.22f}, {0.60f, 0.02f}, 0.08f);
            return opUnion(d1, d2);
        }
        case DecalKind::EyesCross: {
            // X_X eyes
            Vec2 c{sym.x - 0.38f, sym.y + 0.10f};
            float d1 = sdCapsule(c, {-0.18f, -0.18f}, {0.18f, 0.18f}, 0.065f);
            float d2 = sdCapsule(c, {-0.18f,  0.18f}, {0.18f, -0.18f}, 0.065f);
            return opUnion(d1, d2);
        }
        case DecalKind::EyeCyclops: {
            float outer = sdCircle(p, 0.46f);
            float inner = sdCircle(p, 0.20f);
            float pupil = sdCircle(p, 0.09f);
            return opUnion(opSubtract(outer, inner), pupil);
        }
        case DecalKind::VisorCyber: {
            float bar = sdRoundedBox({p.x, p.y + 0.10f}, {0.72f, 0.18f}, 0.08f);
            float notch = sdRoundedBox({p.x, p.y + 0.26f}, {0.16f, 0.10f}, 0.02f);
            return opSubtract(bar, notch);
        }
        case DecalKind::MouthSmile: {
            // Classic GD cube rectangular mouth
            return sdRoundedBox({p.x, p.y - 0.34f}, {0.56f, 0.13f}, 0.04f);
        }
        case DecalKind::MouthFangs: {
            float base = sdRoundedBox({p.x, p.y - 0.34f}, {0.58f, 0.14f}, 0.04f);
            static std::vector<Vec2> const fang1{{0.24f, 0.22f}, {0.42f, 0.22f}, {0.33f, 0.44f}};
            static std::vector<Vec2> const fang2{{0.02f, 0.46f}, {0.18f, 0.46f}, {0.10f, 0.28f}};
            float cut = opUnion(sdPolygonConv(sym, fang1), sdPolygonConv(sym, fang2));
            return opSubtract(base, cut);
        }
        case DecalKind::Lightning: {
            static std::vector<Vec2> const bolt{
                { 0.14f, -0.84f}, { 0.46f, -0.84f}, { 0.08f, -0.06f},
                { 0.44f, -0.06f}, {-0.20f,  0.86f}, {-0.04f,  0.14f},
                {-0.42f,  0.14f}
            };
            return sdPolygonConv(p, bolt) - 0.015f;
        }
        case DecalKind::Crown: {
            static std::vector<Vec2> const crown{
                {-0.76f,  0.54f}, {-0.84f, -0.44f}, {-0.38f,  0.02f},
                { 0.00f, -0.64f}, { 0.38f,  0.02f}, { 0.84f, -0.44f},
                { 0.76f,  0.54f}
            };
            float body = sdPolygonConv(p, crown) - 0.02f;
            float base = sdRoundedBox({p.x, p.y - 0.62f}, {0.76f, 0.10f}, 0.04f);
            return opUnion(body, base);
        }
        case DecalKind::Horns: {
            // Sweeping devil horns
            float h1 = sdCapsule(sym, {0.26f,  0.34f}, {0.62f,  0.06f}, 0.14f);
            float h2 = sdCapsule(sym, {0.62f,  0.06f}, {0.78f, -0.56f}, 0.09f);
            float tip = sdCapsule(sym, {0.76f, -0.44f}, {0.58f, -0.78f}, 0.055f);
            return opUnion(opUnion(h1, h2), tip);
        }
        case DecalKind::Wings: {
            float f1 = sdCapsule(sym, {0.14f,  0.12f}, {0.86f, -0.46f}, 0.11f);
            float f2 = sdCapsule(sym, {0.16f,  0.22f}, {0.82f, -0.10f}, 0.10f);
            float f3 = sdCapsule(sym, {0.18f,  0.32f}, {0.68f,  0.24f}, 0.09f);
            return opUnion(opUnion(f1, f2), f3);
        }
        case DecalKind::Skull: {
            float cranium = sdCircle({p.x, p.y + 0.14f}, 0.62f);
            float jaw = sdRoundedBox({p.x, p.y - 0.44f}, {0.34f, 0.24f}, 0.08f);
            float head = opUnion(cranium, jaw);
            float eyes = sdCircle({sym.x - 0.24f, p.y + 0.06f}, 0.15f);
            float nose = sdTriangle({p.x, p.y - 0.24f}, 0.11f, 0.02f);
            float tooth1 = sdRoundedBox({sym.x - 0.11f, p.y - 0.56f}, {0.035f, 0.12f}, 0.01f);
            return opSubtract(opSubtract(opSubtract(head, eyes), nose), tooth1);
        }
        case DecalKind::Gem: {
            static std::vector<Vec2> const gem{
                {-0.46f, -0.64f}, { 0.46f, -0.64f}, { 0.82f, -0.18f},
                { 0.00f,  0.76f}, {-0.82f, -0.18f}
            };
            return sdPolygonConv(p, gem) - 0.015f;
        }
        case DecalKind::Crosshair: {
            float ring = std::fabs(sdCircle(p, 0.60f)) - 0.065f;
            float dotC = sdCircle(p, 0.10f);
            float tickH = sdRoundedBox({sym.x - 0.62f, p.y}, {0.22f, 0.055f}, 0.02f);
            float tickV = sdRoundedBox({p.x, std::fabs(p.y) - 0.62f}, {0.055f, 0.22f}, 0.02f);
            return opUnion(opUnion(ring, dotC), opUnion(tickH, tickV));
        }
        case DecalKind::Flame: {
            float base = sdCircle({p.x, p.y - 0.22f}, 0.48f);
            static std::vector<Vec2> const tip1{
                {-0.44f, 0.16f}, {0.02f, -0.82f}, {0.44f, 0.16f}
            };
            static std::vector<Vec2> const tip2{
                {0.05f, 0.20f}, {0.48f, -0.46f}, {0.50f, 0.22f}
            };
            float outer = opUnion(opUnion(base, sdPolygonConv(p, tip1) - 0.04f),
                                  sdPolygonConv(p, tip2) - 0.03f);
            float innerCut = sdCircle({p.x, p.y - 0.30f}, 0.20f);
            return opSubtract(outer, innerCut);
        }
        case DecalKind::Shuriken: {
            float a = std::atan2(p.y, p.x);
            float r = len(p);
            float sector = kPi * 0.5f;
            float m = std::fmod(a, sector);
            if (m < 0.f) m += sector;
            Vec2 q{r * std::cos(m), r * std::sin(m)};
            static std::vector<Vec2> const blade{
                {0.0f, 0.0f}, {0.86f, 0.0f}, {0.34f, 0.34f}, {0.0f, 0.26f}
            };
            float d = sdPolygonConv(q, blade) - 0.015f;
            float hole = sdCircle(p, 0.18f);
            return opSubtract(d, hole);
        }
        case DecalKind::Gear: {
            float body = sdCircle(p, 0.60f);
            float a = std::atan2(p.y, p.x);
            float r = len(p);
            float sector = kPi / 4.f;  // 8 teeth
            float m = std::fmod(a + sector * 0.5f, sector);
            if (m < 0.f) m += sector;
            m -= sector * 0.5f;
            Vec2 q{r * std::cos(m), r * std::sin(m)};
            float tooth = sdRoundedBox(q - Vec2{0.68f, 0.f}, {0.16f, 0.11f}, 0.03f);
            float hole = sdCircle(p, 0.24f);
            return opSubtract(opUnion(body, tooth), hole);
        }
    }
    return 1.f;
}

}  // anonymous namespace

std::vector<PrimitiveInfo> const& ShapeGenerator::primitives() {
    static std::vector<PrimitiveInfo> const list{
        {PrimitiveKind::RoundedSquare, "Cuadrado",   true,  false, false},
        {PrimitiveKind::Circle,        "Circulo",    false, false, false},
        {PrimitiveKind::Triangle,      "Triangulo",  true,  false, false},
        {PrimitiveKind::Diamond,       "Diamante",   true,  false, false},
        {PrimitiveKind::Hexagon,       "Hexagono",   true,  false, false},
        {PrimitiveKind::Octagon,       "Octagono",   true,  false, false},
        {PrimitiveKind::Star,          "Estrella",   false, true,  true},
        {PrimitiveKind::Cross,         "Cruz",       true,  false, true},
        {PrimitiveKind::Heart,         "Corazon",    false, false, false},
        {PrimitiveKind::Capsule,       "Capsula",    false, false, false},
        {PrimitiveKind::Shield,        "Escudo",     false, false, false},
        {PrimitiveKind::Crescent,      "Luna",       false, false, false},
    };
    return list;
}

std::vector<DecalInfo> const& ShapeGenerator::decals() {
    static std::vector<DecalInfo> const list{
        {DecalKind::EyesClassic, "Ojos GD",     "Caras"},
        {DecalKind::EyesAngry,   "Ojos Furia",  "Caras"},
        {DecalKind::EyesHappy,   "Ojos Feliz",  "Caras"},
        {DecalKind::EyesCross,   "Ojos X_X",    "Caras"},
        {DecalKind::EyeCyclops,  "Ciclope",     "Caras"},
        {DecalKind::VisorCyber,  "Visor Cyber", "Caras"},
        {DecalKind::MouthSmile,  "Sonrisa GD",  "Caras"},
        {DecalKind::MouthFangs,  "Colmillos",   "Caras"},
        {DecalKind::Crown,       "Corona",      "Extras"},
        {DecalKind::Horns,       "Cuernos",     "Extras"},
        {DecalKind::Wings,       "Alas",        "Extras"},
        {DecalKind::Lightning,   "Rayo",        "Emblemas"},
        {DecalKind::Flame,       "Llama",       "Emblemas"},
        {DecalKind::Skull,       "Calavera",    "Emblemas"},
        {DecalKind::Gem,         "Gema",        "Emblemas"},
        {DecalKind::Crosshair,   "Mira",        "Emblemas"},
        {DecalKind::Shuriken,    "Shuriken",    "Emblemas"},
        {DecalKind::Gear,        "Engranaje",   "Emblemas"},
    };
    return list;
}

PrimitiveInfo const* ShapeGenerator::infoFor(PrimitiveKind kind) {
    for (auto const& info : primitives()) {
        if (info.kind == kind) return &info;
    }
    return nullptr;
}

DecalInfo const* ShapeGenerator::infoFor(DecalKind kind) {
    for (auto const& info : decals()) {
        if (info.kind == kind) return &info;
    }
    return nullptr;
}

ts::ImageBuffer ShapeGenerator::rasterize(PieceShape const& shape, int size) {
    if (size <= 0) return {};
    ts::ImageBuffer out(size, size);
    auto* dst = out.data();

    float const half = static_cast<float>(size) * 0.5f;
    float const invHalf = 1.f / half;
    float const aaWidth = std::max(1.25f * invHalf, 0.008f);

    bool const isDecal = shape.kind == PieceShape::Kind::Decal;

    for (int y = 0; y < size; ++y) {
        float const ny = ((static_cast<float>(y) + 0.5f) - half) * invHalf;
        for (int x = 0; x < size; ++x) {
            float const nx = ((static_cast<float>(x) + 0.5f) - half) * invHalf;
            Vec2 const p{nx, ny};
            float const d = isDecal ? evalDecal(shape.decal, p) : evalPrimitive(shape, p);

            // Smooth anti-aliased coverage across the 0 iso-contour.
            float const cov = std::clamp(0.5f - d / aaWidth, 0.f, 1.f);
            if (cov <= 0.001f) continue;

            // Gentle 3D shading value (around 195-220) so `keepLuminance` gives
            // clean depth without darkening flat colors or turning edges black.
            float const bevel = std::clamp(1.f - 0.10f * ny + 0.06f * std::clamp(-d * 3.f, 0.f, 1.f),
                                           0.84f, 1.0f);
            auto const lum = static_cast<std::uint8_t>(std::lround(205.f * bevel));
            auto const alpha = static_cast<std::uint8_t>(std::lround(cov * 255.f));

            auto* px = dst + (static_cast<std::size_t>(y) * size + x) * 4;
            px[0] = lum;
            px[1] = lum;
            px[2] = lum;
            px[3] = alpha;
        }
    }
    return out;
}

ts::ImageBuffer ShapeGenerator::rasterizePrimitive(PrimitiveKind kind, int size, bool hollow) {
    PieceShape shape;
    shape.kind = PieceShape::Kind::Primitive;
    shape.primitive = kind;
    shape.hollow = hollow;
    return rasterize(shape, size);
}

ts::ImageBuffer ShapeGenerator::rasterizeDecal(DecalKind kind, int size) {
    PieceShape shape;
    shape.kind = PieceShape::Kind::Decal;
    shape.decal = kind;
    return rasterize(shape, size);
}

}  // namespace paimon::icon_maker
