#include "ProfileShapes.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include <cmath>
#include <unordered_map>

using namespace cocos2d;

namespace paimon::profile_shapes {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.283185307179586f;

CCPoint c(float x, float y) { return ccp(0.5f + x, 0.5f + y); }

std::vector<CCPoint> regularPolygon(int sides, float radius, float phase) {
    std::vector<CCPoint> v;
    v.reserve(sides);
    for (int i = 0; i < sides; i++) {
        float a = kTwoPi * i / sides + phase;
        v.push_back(c(radius * cosf(a), radius * sinf(a)));
    }
    return v;
}

// rounded regular polygon: arc each corner so scaled-up shapes read as
// deliberate, not jagged.
std::vector<CCPoint> roundedPolygon(int sides, float radius, float phase, float cornerFrac) {
    std::vector<CCPoint> corners = regularPolygon(sides, radius, phase);
    std::vector<CCPoint> out;
    int arcSegs = 7;
    for (int i = 0; i < sides; i++) {
        CCPoint prev = corners[(i + sides - 1) % sides];
        CCPoint cur = corners[i];
        CCPoint next = corners[(i + 1) % sides];
        auto toward = [&](CCPoint a, CCPoint b, float t) {
            return ccp(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        };
        CCPoint a0 = toward(cur, prev, cornerFrac);
        CCPoint a1 = toward(cur, next, cornerFrac);
        for (int s = 0; s <= arcSegs; s++) {
            float t = static_cast<float>(s) / arcSegs;
            // quadratic bezier through the corner point.
            float u = 1.f - t;
            float x = u * u * a0.x + 2 * u * t * cur.x + t * t * a1.x;
            float y = u * u * a0.y + 2 * u * t * cur.y + t * t * a1.y;
            out.push_back(ccp(x, y));
        }
    }
    return out;
}

std::vector<CCPoint> star(int points, float outerR, float innerR, float phase) {
    int total = points * 2;
    std::vector<CCPoint> v;
    v.reserve(total);
    for (int i = 0; i < total; i++) {
        float a = kTwoPi * i / total + phase;
        float r = (i % 2 == 0) ? outerR : innerR;
        v.push_back(c(r * cosf(a), r * sinf(a)));
    }
    return v;
}

// softened star: round the tips so points don't alias when enlarged.
std::vector<CCPoint> roundedStar(int points, float outerR, float innerR, float phase) {
    std::vector<CCPoint> raw = star(points, outerR, innerR, phase);
    std::vector<CCPoint> out;
    int n = static_cast<int>(raw.size());
    int arcSegs = 4;
    for (int i = 0; i < n; i++) {
        CCPoint prev = raw[(i + n - 1) % n];
        CCPoint cur = raw[i];
        CCPoint next = raw[(i + 1) % n];
        float frac = (i % 2 == 0) ? 0.42f : 0.30f;
        auto toward = [&](CCPoint a, CCPoint b, float t) {
            return ccp(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        };
        CCPoint a0 = toward(cur, prev, frac);
        CCPoint a1 = toward(cur, next, frac);
        for (int s = 0; s <= arcSegs; s++) {
            float t = static_cast<float>(s) / arcSegs;
            float u = 1.f - t;
            float x = u * u * a0.x + 2 * u * t * cur.x + t * t * a1.x;
            float y = u * u * a0.y + 2 * u * t * cur.y + t * t * a1.y;
            out.push_back(ccp(x, y));
        }
    }
    return out;
}

std::vector<CCPoint> circle(float radius) {
    std::vector<CCPoint> v;
    const int segs = 96;
    v.reserve(segs);
    for (int i = 0; i < segs; i++) {
        float a = kTwoPi * i / segs;
        v.push_back(c(radius * cosf(a), radius * sinf(a)));
    }
    return v;
}

// rounded box with explicit corner radius in unit space.
std::vector<CCPoint> roundedBox(float half, float radius) {
    radius = std::min(radius, half);
    std::vector<CCPoint> v;
    const int arcSegs = 12;
    float inner = half - radius;
    struct Corner { float cx, cy, a0; };
    Corner corners[4] = {
        { inner,  inner, 0.f},
        {-inner,  inner, kPi * 0.5f},
        {-inner, -inner, kPi},
        { inner, -inner, kPi * 1.5f},
    };
    for (auto const& co : corners) {
        for (int i = 0; i <= arcSegs; i++) {
            float a = co.a0 + (kPi * 0.5f) * i / arcSegs;
            v.push_back(c(co.cx + radius * cosf(a), co.cy + radius * sinf(a)));
        }
    }
    return v;
}

// superellipse |x|^n + |y|^n = r^n; n=4 gives the apple-style squircle.
std::vector<CCPoint> squircle(float radius, float n) {
    std::vector<CCPoint> v;
    const int segs = 96;
    v.reserve(segs);
    for (int i = 0; i < segs; i++) {
        float a = kTwoPi * i / segs;
        float ca = cosf(a), sa = sinf(a);
        float x = powf(std::fabs(ca), 2.f / n) * (ca < 0 ? -1.f : 1.f) * radius;
        float y = powf(std::fabs(sa), 2.f / n) * (sa < 0 ? -1.f : 1.f) * radius;
        v.push_back(c(x, y));
    }
    return v;
}

std::vector<CCPoint> heart(float scale) {
    std::vector<CCPoint> v;
    const int segs = 90;
    v.reserve(segs);
    for (int i = 0; i < segs; i++) {
        float t = kTwoPi * i / segs;
        float x = 16.f * powf(sinf(t), 3.f);
        float y = 13.f * cosf(t) - 5.f * cosf(2 * t) - 2.f * cosf(3 * t) - cosf(4 * t);
        v.push_back(c(x / 32.f * scale, y / 32.f * scale));
    }
    return v;
}

std::vector<CCPoint> drop(float scale) {
    std::vector<CCPoint> v;
    const int segs = 72;
    for (int i = 0; i <= segs; i++) {
        float a = -kPi * 0.5f + kPi * i / segs;
        v.push_back(c(scale * cosf(a), scale * sinf(a) - scale * 0.15f));
    }
    v.push_back(c(0.f, scale * 1.5f));
    return v;
}

std::vector<CCPoint> shield(float w, float h) {
    std::vector<CCPoint> v;
    v.push_back(c(-w, h));
    v.push_back(c(w, h));
    v.push_back(c(w, -h * 0.25f));
    const int segs = 40;
    for (int i = 0; i <= segs; i++) {
        float t = static_cast<float>(i) / segs;
        float y = -h * 0.25f - (h * 0.95f) * sinf(kPi * 0.5f * t);
        v.push_back(c(w * (1.f - t * t), y));
    }
    v.push_back(c(0.f, -h * 1.1f));
    for (int i = segs; i >= 0; i--) {
        float t = static_cast<float>(i) / segs;
        float y = -h * 0.25f - (h * 0.95f) * sinf(kPi * 0.5f * t);
        v.push_back(c(-w * (1.f - t * t), y));
    }
    return v;
}

std::vector<CCPoint> cloud(float scale) {
    std::vector<CCPoint> v;
    struct Bump { float cx, cy, r; };
    Bump bumps[4] = {
        {-0.55f, -0.05f, 0.42f},
        {-0.18f,  0.22f, 0.5f},
        { 0.3f,   0.18f, 0.46f},
        { 0.58f, -0.05f, 0.38f},
    };
    const int segs = 24;
    for (int b = 0; b < 4; b++) {
        float startA = (b == 0) ? kPi * 0.6f : kPi * 0.95f;
        float sweep = (b == 0 || b == 3) ? kPi * 1.1f : kPi * 1.0f;
        if (b == 3) startA = kPi * 1.35f;
        for (int i = 0; i <= segs; i++) {
            float a = startA + sweep * i / segs;
            v.push_back(c((bumps[b].cx + bumps[b].r * cosf(a)) * scale,
                          (bumps[b].cy + bumps[b].r * sinf(a)) * scale));
        }
    }
    v.push_back(c(0.75f * scale, -0.35f * scale));
    v.push_back(c(-0.75f * scale, -0.35f * scale));
    return v;
}

std::vector<CCPoint> flower(int petals, float scale) {
    std::vector<CCPoint> v;
    const int segs = 160;
    for (int i = 0; i < segs; i++) {
        float a = kTwoPi * i / segs;
        float r = (0.72f + 0.28f * cosf(petals * a)) * scale;
        v.push_back(c(r * cosf(a), r * sinf(a)));
    }
    return v;
}

std::vector<CCPoint> gear(int teeth, float scale) {
    std::vector<CCPoint> v;
    int steps = teeth * 4;
    for (int i = 0; i < steps; i++) {
        float a = kTwoPi * i / steps;
        float phase = static_cast<float>(i % 4);
        float r = (phase < 2.f ? 0.78f : 0.98f) * scale;
        v.push_back(c(r * cosf(a), r * sinf(a)));
    }
    return v;
}

std::vector<CCPoint> blob(float scale) {
    std::vector<CCPoint> v;
    const int segs = 96;
    for (int i = 0; i < segs; i++) {
        float a = kTwoPi * i / segs;
        float r = (0.86f
            + 0.08f * sinf(3.f * a + 0.6f)
            + 0.05f * sinf(5.f * a + 2.1f)
            + 0.03f * sinf(7.f * a)) * scale;
        v.push_back(c(r * cosf(a), r * sinf(a)));
    }
    return v;
}

std::vector<CCPoint> leaf(float scale) {
    std::vector<CCPoint> v;
    const int segs = 60;
    for (int i = 0; i <= segs; i++) {
        float t = static_cast<float>(i) / segs;
        float x = (t - 0.5f) * 2.f * scale * 0.85f;
        float y = sinf(kPi * t) * scale * 0.6f;
        v.push_back(c(x * 0.70710677f - y * 0.70710677f, x * 0.70710677f + y * 0.70710677f));
    }
    for (int i = segs; i >= 0; i--) {
        float t = static_cast<float>(i) / segs;
        float x = (t - 0.5f) * 2.f * scale * 0.85f;
        float y = -sinf(kPi * t) * scale * 0.6f;
        v.push_back(c(x * 0.70710677f - y * 0.70710677f, x * 0.70710677f + y * 0.70710677f));
    }
    return v;
}

std::vector<CCPoint> cross(float arm, float thick) {
    return {
        c(-thick, -arm), c(thick, -arm), c(thick, -thick), c(arm, -thick),
        c(arm, thick), c(thick, thick), c(thick, arm), c(-thick, arm),
        c(-thick, thick), c(-arm, thick), c(-arm, -thick), c(-thick, -thick),
    };
}

// crescent: outer disc minus an offset inner disc, traced as one ring.
std::vector<CCPoint> moon(float scale) {
    std::vector<CCPoint> v;
    const int segs = 72;
    float r = scale;
    float ir = scale * 0.82f;
    float off = scale * 0.42f;
    for (int i = 0; i <= segs; i++) {
        float a = -kPi * 0.62f + (kPi * 1.24f) * i / segs;
        v.push_back(c(r * cosf(a), r * sinf(a)));
    }
    for (int i = segs; i >= 0; i--) {
        float a = -kPi * 0.5f + kPi * i / segs;
        v.push_back(c(off + ir * cosf(a), ir * sinf(a)));
    }
    return v;
}

// hollow top arc; works as a ring/arch stencil.
std::vector<CCPoint> ring(float outer, float inner) {
    std::vector<CCPoint> v;
    const int segs = 96;
    for (int i = 0; i <= segs; i++) {
        float a = kTwoPi * i / segs;
        v.push_back(c(outer * cosf(a), outer * sinf(a)));
    }
    v.push_back(c(outer, 0.f));
    for (int i = segs; i >= 0; i--) {
        float a = kTwoPi * i / segs;
        v.push_back(c(inner * cosf(a), inner * sinf(a)));
    }
    return v;
}

std::vector<CCPoint> badge(float scale) {
    // scalloped seal: many small bumps around a disc.
    std::vector<CCPoint> v;
    const int segs = 180;
    int bumps = 16;
    for (int i = 0; i < segs; i++) {
        float a = kTwoPi * i / segs;
        float r = (0.9f + 0.1f * cosf(bumps * a)) * scale;
        v.push_back(c(r * cosf(a), r * sinf(a)));
    }
    return v;
}

std::vector<CCPoint> ticket(float w, float h) {
    std::vector<CCPoint> v;
    const int notchSegs = 10;
    float notch = h * 0.22f;
    // right edge with a middle notch, counter-clockwise.
    v.push_back(c(w, h));
    for (int i = 0; i <= notchSegs; i++) {
        float t = static_cast<float>(i) / notchSegs;
        float a = -kPi * 0.5f + kPi * t;
        v.push_back(c(w - notch * cosf(a), notch * sinf(a)));
    }
    v.push_back(c(w, -h));
    v.push_back(c(-w, -h));
    for (int i = 0; i <= notchSegs; i++) {
        float t = static_cast<float>(i) / notchSegs;
        float a = kPi * 0.5f + kPi * t;
        v.push_back(c(-w - notch * cosf(a), notch * sinf(a)));
    }
    v.push_back(c(-w, h));
    return v;
}

using Builder = std::vector<CCPoint> (*)();

std::unordered_map<std::string, Builder> const& registry() {
    static std::unordered_map<std::string, Builder> const map = {
        {"circle",      [] { return circle(0.5f); }},
        {"ring",        [] { return ring(0.5f, 0.32f); }},
        {"square",      [] { return roundedBox(0.5f, 0.02f); }},
        {"rounded",     [] { return roundedBox(0.5f, 0.14f); }},
        {"rounded_lg",  [] { return roundedBox(0.5f, 0.26f); }},
        {"squircle",    [] { return squircle(0.5f, 4.f); }},
        {"rectangle",   [] { return roundedBox(0.5f, 0.06f); }},
        {"pill",        [] { return roundedBox(0.5f, 0.5f); }},
        {"triangle",    [] { return roundedPolygon(3, 0.56f, kPi * 0.5f, 0.22f); }},
        {"diamond",     [] { return roundedPolygon(4, 0.56f, kPi * 0.5f, 0.14f); }},
        {"pentagon",    [] { return roundedPolygon(5, 0.54f, kPi * 0.5f, 0.16f); }},
        {"hexagon",     [] { return roundedPolygon(6, 0.52f, 0.f, 0.16f); }},
        {"octagon",     [] { return roundedPolygon(8, 0.52f, kPi / 8.f, 0.18f); }},
        {"star",        [] { return roundedStar(5, 0.52f, 0.22f, kPi * 0.5f); }},
        {"star4",       [] { return roundedStar(4, 0.52f, 0.2f, kPi * 0.5f); }},
        {"star6",       [] { return roundedStar(6, 0.52f, 0.26f, kPi * 0.5f); }},
        {"star8",       [] { return roundedStar(8, 0.52f, 0.3f, kPi * 0.5f); }},
        {"heart",       [] { return heart(0.9f); }},
        {"shield",      [] { return shield(0.42f, 0.46f); }},
        {"cloud",       [] { return cloud(0.72f); }},
        {"flower",      [] { return flower(6, 0.5f); }},
        {"flower5",     [] { return flower(5, 0.5f); }},
        {"gear",        [] { return gear(10, 0.5f); }},
        {"blob",        [] { return blob(0.5f); }},
        {"leaf",        [] { return leaf(0.5f); }},
        {"drop",        [] { return drop(0.38f); }},
        {"cross",       [] { return cross(0.5f, 0.17f); }},
        {"badge",       [] { return badge(0.5f); }},
        {"moon",        [] { return moon(0.46f); }},
        {"ticket",      [] { return ticket(0.46f, 0.34f); }},
    };
    return map;
}

std::unordered_map<std::string, std::string> const& legacyMap() {
    static std::unordered_map<std::string, std::string> const map = {
        {"teardrop", "drop"},
        {"arch", "ring"},
        {"star5", "star"},
    };
    return map;
}
}

std::string canonicalId(std::string const& shapeName) {
    if (shapeName.empty()) return "circle";
    if (registry().count(shapeName)) return shapeName;
    auto it = legacyMap().find(shapeName);
    if (it != legacyMap().end()) return it->second;
    return "circle";
}

bool isKnown(std::string const& shapeName) {
    return registry().count(shapeName) != 0 || legacyMap().count(shapeName) != 0;
}

std::vector<CCPoint> unitOutline(std::string const& shapeName) {
    auto const& reg = registry();
    auto it = reg.find(canonicalId(shapeName));
    if (it == reg.end()) return circle(0.5f);
    return it->second();
}

CCNode* createFill(std::string const& shapeName, float size) {
    auto unit = unitOutline(shapeName);
    if (unit.size() < 3) return nullptr;

    std::vector<CCPoint> verts;
    verts.reserve(unit.size());
    for (auto const& p : unit) verts.push_back(ccp(p.x * size, p.y * size));

    auto draw = PaimonDrawNode::create();
    draw->drawPolygon(verts.data(), static_cast<unsigned int>(verts.size()),
                      ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));

    auto container = CCNode::create();
    container->setContentSize({size, size});
    container->addChild(draw);
    return container;
}

CCNode* createOutline(std::string const& shapeName, float size, float thickness, ccColor3B color, GLubyte opacity) {
    auto unit = unitOutline(shapeName);
    if (unit.size() < 3) return nullptr;

    std::vector<CCPoint> verts;
    verts.reserve(unit.size());
    for (auto const& p : unit) verts.push_back(ccp(p.x * size, p.y * size));

    auto draw = PaimonDrawNode::create();
    ccColor4F col = ccc4FFromccc4B(ccc4(color.r, color.g, color.b, opacity));
    for (size_t i = 0; i < verts.size(); i++) {
        size_t next = (i + 1) % verts.size();
        draw->drawCapsuleSegment(verts[i], verts[next], thickness, col, 10);
    }

    auto container = CCNode::create();
    container->setContentSize({size, size});
    container->addChild(draw);
    return container;
}

std::vector<std::pair<std::string, std::string>> pickerShapes() {
    return {
        {"circle", "Circle"},
        {"ring", "Ring"},
        {"rounded", "Rounded"},
        {"rounded_lg", "Round+"},
        {"squircle", "Squircle"},
        {"square", "Square"},
        {"rectangle", "Rect"},
        {"pill", "Pill"},
        {"triangle", "Triangle"},
        {"diamond", "Diamond"},
        {"pentagon", "Pentagon"},
        {"hexagon", "Hexagon"},
        {"octagon", "Octagon"},
        {"star", "Star 5"},
        {"star4", "Star 4"},
        {"star6", "Star 6"},
        {"star8", "Star 8"},
        {"heart", "Heart"},
        {"shield", "Shield"},
        {"cloud", "Cloud"},
        {"flower", "Flower"},
        {"flower5", "Bloom"},
        {"gear", "Gear"},
        {"blob", "Blob"},
        {"leaf", "Leaf"},
        {"drop", "Drop"},
        {"cross", "Cross"},
        {"badge", "Seal"},
        {"moon", "Moon"},
        {"ticket", "Ticket"},
    };
}

}
