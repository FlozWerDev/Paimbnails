#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x, y;
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};

float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float lerp(float a, float b, float t) { return a + (b - a) * clamp01(t); }
float smoothstep(float e0, float e1, float x) {
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

float smin(float a, float b, float k) {
    if (k <= 0.f) return std::min(a, b);
    float h = clamp01(0.5f + 0.5f * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.f - h);
}

struct RGBA {
    float r, g, b, a;
};

RGBA over(RGBA dst, RGBA src) {
    float sa = clamp01(src.a);
    float da = clamp01(dst.a);
    float oa = sa + da * (1.f - sa);
    if (oa <= 1e-6f) return {0.f, 0.f, 0.f, 0.f};
    return {
        (src.r * sa + dst.r * da * (1.f - sa)) / oa,
        (src.g * sa + dst.g * da * (1.f - sa)) / oa,
        (src.b * sa + dst.b * da * (1.f - sa)) / oa,
        oa
    };
}

// Analytical SDF primitives in centered coords [-0.5, 0.5] (+y is down)
float sdCircle(Vec2 p, Vec2 c, float r) {
    return length(p - c) - r;
}

float sdRoundBox(Vec2 p, Vec2 b, float r) {
    Vec2 q = {std::abs(p.x) - b.x + r, std::abs(p.y) - b.y + r};
    Vec2 qpos = {std::max(q.x, 0.f), std::max(q.y, 0.f)};
    return std::min(std::max(q.x, q.y), 0.f) + length(qpos) - r;
}

float sdOctagon(Vec2 p, float r, float roundR = 0.015f) {
    const float k = 0.9238795325f; // cos(pi/8)
    const float s = 0.3826834323f; // sin(pi/8)
    p = {std::abs(p.x), std::abs(p.y)};
    float d1 = std::max(p.x, p.y) - r;
    float d2 = (p.x + p.y) * 0.70710678f - r * 1.13f;
    // Chamfered square octagon
    (void)k; (void)s;
    return std::max(d1, d2) - roundR * 0.f;
}

float sdChamferBox(Vec2 p, float half, float chamfer, float roundR) {
    p = {std::abs(p.x), std::abs(p.y)};
    float dBox = std::max(p.x, p.y) - half;
    float dDiag = (p.x + p.y) * 0.70710678f - (half * 1.41421356f - chamfer * 0.70710678f);
    return smin(-smin(-dBox, -dDiag, roundR), 0.f, 0.f); // smooth intersection
}

float sdChamferedOctagon(Vec2 p, float half, float chamfer, float roundR) {
    p = {std::abs(p.x), std::abs(p.y)};
    float dBox = std::max(p.x, p.y) - (half - roundR);
    float dDiag = (p.x + p.y - (2.f * half - chamfer - roundR * 1.41421356f)) * 0.70710678f;
    // Smooth max (intersection)
    float k = roundR * 0.6f;
    if (k > 0.f) {
        float h = clamp01(0.5f - 0.5f * (dDiag - dBox) / k);
        return lerp(dDiag, dBox, h) + k * h * (1.f - h) - roundR;
    }
    return std::max(dBox, dDiag) - roundR;
}

float sdHexagon(Vec2 p, float r, float roundR) {
    const Vec2 k = {-0.866025404f, 0.5f};
    p = {std::abs(p.x), std::abs(p.y)};
    float m = 2.f * std::min(dot(k, p), 0.f);
    p = p - k * m;
    p.x -= std::clamp(p.x, -0.57735027f * r, 0.57735027f * r);
    p.y -= r;
    float d = length(p) * (p.y < 0.f ? -1.f : 1.f);
    // Rotate 90 deg for pointy-top by passing {p.y, p.x} at call site
    return d - roundR;
}

float sdPolygon(Vec2 p, Vec2 const* v, int n) {
    float d = dot(p - v[0], p - v[0]);
    float s = 1.f;
    for (int i = 0, j = n - 1; i < n; j = i, ++i) {
        Vec2 e = v[j] - v[i];
        Vec2 w = p - v[i];
        float t = clamp01(dot(w, e) / std::max(1e-8f, dot(e, e)));
        Vec2 b = w - e * t;
        d = std::min(d, dot(b, b));
        bool c0 = p.y >= v[i].y;
        bool c1 = p.y < v[j].y;
        bool c2 = e.x * w.y > e.y * w.x;
        if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) s *= -1.f;
    }
    return s * std::sqrt(d);
}

float sdSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 pa = p - a, ba = b - a;
    float h = clamp01(dot(pa, ba) / std::max(1e-8f, dot(ba, ba)));
    return length(pa - ba * h);
}

// 8-point sunburst star SDF via angular sector folding
float sdStarN(Vec2 p, float outerR, float innerR, int n, float roundR) {
    float an = kPi / static_cast<float>(n);
    float a = std::atan2(p.y, p.x) + kPi * 0.5f;
    float sector = std::fmod(a, 2.f * an);
    if (sector < 0.f) sector += 2.f * an;
    sector = std::abs(sector - an);
    float r = length(p);
    Vec2 q = {r * std::cos(sector), r * std::sin(sector)};
    Vec2 v0 = {outerR - roundR, 0.f};
    Vec2 v1 = {(innerR - roundR * 0.5f) * std::cos(an), (innerR - roundR * 0.5f) * std::sin(an)};
    Vec2 e = v1 - v0;
    Vec2 w = q - v0;
    float t = clamp01(dot(w, e) / std::max(1e-8f, dot(e, e)));
    float dist = length(w - e * t);
    float cross = e.x * w.y - e.y * w.x;
    return (cross < 0.f ? dist : -dist) - roundR;
}

// 4-point diamond star sparkle
float sdStar4(Vec2 p, float outerR, float waist) {
    p = {std::abs(p.x), std::abs(p.y)};
    Vec2 pts[4] = {{0.f, 0.f}, {outerR, 0.f}, {waist, waist}, {0.f, outerR}};
    return sdPolygon(p, pts, 4);
}

// Shield SDF: arched heraldic crest
float sdShield(Vec2 p, float scale) {
    p = p * (1.f / scale);
    static std::vector<Vec2> pts;
    if (pts.empty()) {
        // Build dense smooth contour: arched top -> right shoulder -> curved taper to tip -> left shoulder
        auto addQuad = [&](Vec2 a, Vec2 ctrl, Vec2 b, int steps) {
            for (int i = 1; i <= steps; ++i) {
                float t = static_cast<float>(i) / steps;
                float u = 1.f - t;
                pts.push_back({
                    u * u * a.x + 2 * u * t * ctrl.x + t * t * b.x,
                    u * u * a.y + 2 * u * t * ctrl.y + t * t * b.y
                });
            }
        };
        Vec2 leftTop = {-0.38f, -0.24f};
        Vec2 rightTop = {0.38f, -0.24f};
        Vec2 tip = {0.f, 0.43f};
        pts.push_back(leftTop);
        addQuad(leftTop, {0.f, -0.48f}, rightTop, 20);
        addQuad(rightTop, {0.36f, 0.17f}, tip, 22);
        addQuad(tip, {-0.36f, 0.17f}, leftTop, 22);
    }
    return sdPolygon(p, pts.data(), static_cast<int>(pts.size())) * scale;
}

// Crown body SDF (without top pearls, which are added separately for clean rim shading)
float sdCrownBody(Vec2 p) {
    static std::vector<Vec2> pts;
    if (pts.empty()) {
        // Symmetrical sculpted GD crown silhouette
        pts = {
            {-0.33f,  0.34f},
            { 0.33f,  0.34f},
            { 0.36f,  0.22f},
            { 0.42f, -0.17f},
            { 0.21f,  0.03f},
            { 0.00f, -0.33f},
            {-0.21f,  0.03f},
            {-0.42f, -0.17f},
            {-0.36f,  0.22f},
        };
    }
    return sdPolygon(p, pts.data(), static_cast<int>(pts.size())) - 0.014f;
}

// Evaluate classic GD medal material given outer SDF dOuter and recessed well SDF dWell
RGBA evalMedalPixel(
    Vec2 p,
    float dOuter,
    float dWell,
    float aa,
    float outlineW,
    float wellRimW,
    float bevelW,
    float ridgeBoost = 0.f
) {
    float alpha = 1.f - smoothstep(-aa, aa, dOuter);
    if (alpha <= 1e-4f) return {0.f, 0.f, 0.f, 0.f};

    // Base body shading (grayscale so runtime setColor() tints cleanly)
    float depth = -dOuter - outlineW;
    float bevelT = clamp01(depth / std::max(1e-4f, bevelW));

    // Top-left directional GD lighting
    float light = clamp01(0.55f - p.y * 0.55f - p.x * 0.25f);
    float bodyVal = lerp(164.f, 232.f, bevelT * 0.65f + light * 0.35f) / 255.f;

    // Outer step ring highlight
    float rimStep = smoothstep(0.18f, 0.28f, bevelT) * (1.f - smoothstep(0.32f, 0.46f, bevelT));
    bodyVal = clamp01(bodyVal + rimStep * 0.07f + ridgeBoost);

    // Classic RobTop upper curved gloss crescent
    float glossMask = (1.f - smoothstep(-0.18f, 0.05f, p.y - p.x * 0.18f - 0.05f * p.x * p.x * 4.f));
    float innerFade = smoothstep(0.08f, 0.32f, bevelT);
    bodyVal = lerp(bodyVal, 0.99f, glossMask * innerFade * 0.46f);

    // Recessed dark socket
    float wellAlpha = 1.f - smoothstep(-aa, aa, dWell);
    float wellRim = 1.f - smoothstep(0.f, aa * 1.4f, dWell - wellRimW);

    float wellVal = lerp(64.f, 92.f, clamp01(0.5f + p.y * 1.2f)) / 255.f;
    // Subtle inner top shadow inside the socket
    float innerShadow = smoothstep(-0.04f, 0.f, dWell);
    wellVal *= lerp(1.f, 0.72f, innerShadow);

    // Combine: recessed well -> inner black rim -> lit body -> outer black rim
    float col = bodyVal;
    col = lerp(col, 0.03f, wellRim);     // inner black border
    col = lerp(col, wellVal, wellAlpha); // dark socket fill

    // Outer black outline
    float bodyMask = 1.f - smoothstep(-aa, aa, dOuter + outlineW);
    col = lerp(0.02f, col, bodyMask);

    return {col, col, col, alpha};
}

// Render a 4x supersampled image and downsample to outSize x outSize
struct ImageBuf {
    int size;
    std::vector<uint8_t> rgba;
};

template <typename Fn>
ImageBuf renderSS(int outSize, int ss, Fn&& shader) {
    int hi = outSize * ss;
    std::vector<float> buf(static_cast<size_t>(hi) * hi * 4, 0.f);
    float aa = 1.35f / static_cast<float>(hi);

    for (int y = 0; y < hi; ++y) {
        float py = (static_cast<float>(y) + 0.5f) / static_cast<float>(hi) - 0.5f;
        for (int x = 0; x < hi; ++x) {
            float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(hi) - 0.5f;
            RGBA c = shader(Vec2{px, py}, aa);
            size_t idx = (static_cast<size_t>(y) * hi + x) * 4;
            buf[idx + 0] = clamp01(c.r);
            buf[idx + 1] = clamp01(c.g);
            buf[idx + 2] = clamp01(c.b);
            buf[idx + 3] = clamp01(c.a);
        }
    }

    ImageBuf out{outSize, std::vector<uint8_t>(static_cast<size_t>(outSize) * outSize * 4, 0)};
    for (int y = 0; y < outSize; ++y) {
        for (int x = 0; x < outSize; ++x) {
            float rSum = 0.f, gSum = 0.f, bSum = 0.f, aSum = 0.f;
            for (int sy = 0; sy < ss; ++sy) {
                for (int sx = 0; sx < ss; ++sx) {
                    size_t idx = (static_cast<size_t>(y * ss + sy) * hi + (x * ss + sx)) * 4;
                    float a = buf[idx + 3];
                    rSum += buf[idx + 0] * a;
                    gSum += buf[idx + 1] * a;
                    bSum += buf[idx + 2] * a;
                    aSum += a;
                }
            }
            float inv = 1.f / static_cast<float>(ss * ss);
            float aAvg = aSum * inv;
            size_t oidx = (static_cast<size_t>(y) * outSize + x) * 4;
            if (aAvg > 1e-5f) {
                float invA = 1.f / aSum;
                out.rgba[oidx + 0] = static_cast<uint8_t>(std::round(clamp01(rSum * invA) * 255.f));
                out.rgba[oidx + 1] = static_cast<uint8_t>(std::round(clamp01(gSum * invA) * 255.f));
                out.rgba[oidx + 2] = static_cast<uint8_t>(std::round(clamp01(bSum * invA) * 255.f));
                out.rgba[oidx + 3] = static_cast<uint8_t>(std::round(clamp01(aAvg) * 255.f));
            }
        }
    }
    return out;
}

void writeRaw(std::string const& path, ImageBuf const& img) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "Failed to open %s\n", path.c_str());
        return;
    }
    uint32_t dims[2] = {static_cast<uint32_t>(img.size), static_cast<uint32_t>(img.size)};
    std::fwrite(dims, sizeof(uint32_t), 2, f);
    std::fwrite(img.rgba.data(), 1, img.rgba.size(), f);
    std::fclose(f);
}

// Frame pixel helper for badge plates (transparent center cutout)
RGBA evalFramePixel(Vec2 p, float dOuter, float dHole, float aa, float outlineW, float innerRimW, float bevelW, float accentGlow = 0.f) {
    // Frame is outer minus hole
    float d = std::max(dOuter, -dHole);
    float alpha = 1.f - smoothstep(-aa, aa, d);
    if (alpha <= 1e-4f) return {0.f, 0.f, 0.f, 0.f};

    float depthOut = -dOuter - outlineW;
    float depthIn = dHole - innerRimW;
    float depth = std::min(depthOut, depthIn);
    float bevelT = clamp01(depth / std::max(1e-4f, bevelW));

    float light = clamp01(0.58f - p.y * 0.50f - p.x * 0.25f);
    float val = lerp(175.f, 242.f, bevelT * 0.6f + light * 0.4f) / 255.f;
    val = clamp01(val + accentGlow);

    // RobTop top sheen
    float sheen = (1.f - smoothstep(-0.22f, 0.04f, p.y - p.x * 0.15f)) * smoothstep(0.08f, 0.35f, bevelT);
    val = lerp(val, 0.99f, sheen * 0.42f);

    // Outer & inner black rims
    float insideBody = 1.f - smoothstep(-aa, aa, std::max(dOuter + outlineW, -dHole + innerRimW));
    val = lerp(0.02f, val, insideBody);
    return {val, val, val, alpha};
}

RGBA evalOrb(Vec2 p, Vec2 c, float r, float aa, float outlineW) {
    float d = sdCircle(p, c, r);
    float a = 1.f - smoothstep(-aa, aa, d);
    if (a <= 1e-4f) return {0.f, 0.f, 0.f, 0.f};
    Vec2 q = (p - c) * (1.f / r);
    float spec = std::exp(-dot(q - Vec2{-0.28f, -0.32f}, q - Vec2{-0.28f, -0.32f}) * 7.f);
    float val = clamp01(0.76f - q.y * 0.16f + spec * 0.35f);
    float bodyMask = 1.f - smoothstep(-aa, aa, d + outlineW);
    val = lerp(0.02f, val, bodyMask);
    return {val, val, val, a};
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <out_raw_dir>\n", argv[0]);
        return 1;
    }
    std::string dir = argv[1];
    auto pathFor = [&](char const* name) { return dir + "/" + name + ".rgba"; };

    // 1. paim_progTierRound (288x288)
    writeRaw(pathFor("paim_progTierRound"), renderSS(288, 4, [](Vec2 p, float aa) {
        float dOuter = sdCircle(p, {0.f, 0.f}, 0.455f);
        // Subtle cardinal accent notches on outer ring
        float notch = 1e9f;
        for (Vec2 n : {Vec2{0.f, -0.385f}, Vec2{0.f, 0.385f}, Vec2{-0.385f, 0.f}, Vec2{0.385f, 0.f}}) {
            notch = std::min(notch, sdCircle(p, n, 0.022f));
        }
        float dWell = sdCircle(p, {0.f, 0.f}, 0.275f);
        RGBA c = evalMedalPixel(p, dOuter, dWell, aa, 0.028f, 0.024f, 0.075f);
        // Subtle metallic stud highlight at cardinal points
        float stud = 1.f - smoothstep(-aa, aa, notch);
        if (stud > 0.f && dOuter < -0.028f && dWell > 0.024f) {
            c.r = c.g = c.b = lerp(c.r, 0.96f, stud * 0.55f);
        }
        return c;
    }));

    // 2. paim_progTierShield (288x288)
    writeRaw(pathFor("paim_progTierShield"), renderSS(288, 4, [](Vec2 p, float aa) {
        Vec2 q = {p.x, p.y + 0.02f};
        float dOuter = sdShield(q, 1.0f);
        float dWell = sdShield(Vec2{q.x, q.y - 0.01f}, 0.62f);
        float ridge = smoothstep(0.06f, 0.f, std::abs(p.x)) * (p.x < 0.f ? 0.04f : -0.02f);
        return evalMedalPixel(p, dOuter, dWell, aa, 0.028f, 0.022f, 0.070f, ridge);
    }));

    // 3. paim_progTierHex (288x288)
    writeRaw(pathFor("paim_progTierHex"), renderSS(288, 4, [](Vec2 p, float aa) {
        Vec2 q = {p.y, p.x}; // pointy-top
        float dOuter = sdHexagon(q, 0.395f, 0.030f);
        float dWell = sdHexagon(q, 0.252f, 0.020f);
        return evalMedalPixel(p, dOuter, dWell, aa, 0.028f, 0.022f, 0.072f);
    }));

    // 4. paim_progTierStar (288x288)
    writeRaw(pathFor("paim_progTierStar"), renderSS(288, 4, [](Vec2 p, float aa) {
        float dStar = sdStarN(p, 0.460f, 0.312f, 8, 0.018f);
        float dRing = sdCircle(p, {0.f, 0.f}, 0.312f);
        float dOuter = std::min(dStar, dRing);
        float dWell = sdCircle(p, {0.f, 0.f}, 0.232f);
        // Subtle radial facet shading on star rays
        float ang = std::atan2(p.y, p.x);
        float facet = std::sin(ang * 8.f) * 0.055f * smoothstep(0.24f, 0.34f, length(p));
        return evalMedalPixel(p, dOuter, dWell, aa, 0.026f, 0.022f, 0.062f, facet);
    }));

    // 5. paim_progTierCrown (288x288)
    writeRaw(pathFor("paim_progTierCrown"), renderSS(288, 4, [](Vec2 p, float aa) {
        // Crown body + recessed plaque at y=+0.20 (cocos -0.20*size)
        float dBody = sdCrownBody(p);
        float dWell = sdRoundBox(p - Vec2{0.f, 0.205f}, {0.260f, 0.108f}, 0.052f);
        RGBA base = evalMedalPixel(p, dBody, dWell, aa, 0.026f, 0.020f, 0.058f,
            smoothstep(0.06f, 0.f, std::abs(p.x)) * 0.05f);

        // 3 glossy GD pearl finials on the 3 peaks
        RGBA orbC = evalOrb(p, { 0.00f, -0.375f}, 0.058f, aa, 0.020f);
        RGBA orbL = evalOrb(p, {-0.415f, -0.205f}, 0.048f, aa, 0.018f);
        RGBA orbR = evalOrb(p, { 0.415f, -0.205f}, 0.048f, aa, 0.018f);
        RGBA res = over(base, orbC);
        res = over(res, orbL);
        res = over(res, orbR);

        // 3 subtle gems on upper brow above plaque
        for (Vec2 g : {Vec2{0.f, 0.015f}, Vec2{-0.21f, 0.042f}, Vec2{0.21f, 0.042f}}) {
            RGBA gem = evalOrb(p, g, g.x == 0.f ? 0.032f : 0.023f, aa, 0.013f);
            res = over(res, gem);
        }
        return res;
    }));

    // 6. paim_progRing (360x360) - GD tubular radial groove ring
    writeRaw(pathFor("paim_progRing"), renderSS(360, 4, [](Vec2 p, float aa) {
        float r = length(p);
        float outerR = 0.490f;
        float innerR = 0.402f;
        float dRing = std::max(r - outerR, innerR - r);
        float alpha = 1.f - smoothstep(-aa, aa, dRing);
        if (alpha <= 1e-4f) return RGBA{0.f, 0.f, 0.f, 0.f};

        float mid = (outerR + innerR) * 0.5f;
        float halfW = (outerR - innerR) * 0.5f;
        float rimW = 0.016f;
        float normT = clamp01(1.f - std::abs(r - mid) / std::max(1e-4f, halfW - rimW));
        float val = lerp(0.76f, 1.0f, std::sqrt(normT));
        float bodyMask = 1.f - smoothstep(-aa, aa, dRing + rimW);
        val = lerp(0.02f, val, bodyMask);
        return RGBA{val, val, val, alpha};
    }));

    // 7. paim_progPlateFace (288x288) - crisp halo-free recessed rounded-square icon well
    writeRaw(pathFor("paim_progPlateFace"), renderSS(288, 4, [](Vec2 p, float aa) {
        float d = sdRoundBox(p, {0.295f, 0.295f}, 0.075f);
        float alpha = 1.f - smoothstep(-aa, aa, d);
        if (alpha <= 1e-4f) return RGBA{0.f, 0.f, 0.f, 0.f};
        float vGrad = lerp(62.f, 102.f, clamp01((p.y + 0.295f) / 0.59f)) / 255.f;
        float edgeShade = smoothstep(-0.04f, 0.f, d);
        vGrad *= lerp(1.f, 0.72f, edgeShade);
        // Subtle GD top-left diagonal gloss
        float gloss = (1.f - smoothstep(-0.18f, 0.05f, p.x * 0.45f + p.y));
        vGrad = lerp(vGrad, 0.58f, gloss * 0.20f);
        return RGBA{vGrad, vGrad, vGrad, alpha};
    }));

    // 8. paim_progPlate1..6 (288x288) - shared crisp cutout (half=0.285, r=0.072)
    constexpr float kHoleHalf = 0.285f;
    constexpr float kHoleR = 0.072f;

    // Plate 1: Common - Classic GD rounded-square beveled frame
    writeRaw(pathFor("paim_progPlate1"), renderSS(288, 4, [](Vec2 p, float aa) {
        float dOut = sdRoundBox(p, {0.425f, 0.425f}, 0.105f);
        float dHole = sdRoundBox(p, {kHoleHalf, kHoleHalf}, kHoleR);
        return evalFramePixel(p, dOut, dHole, aa, 0.026f, 0.022f, 0.048f);
    }));

    // Plate 2: Uncommon - Chamfered octagonal GD frame
    writeRaw(pathFor("paim_progPlate2"), renderSS(288, 4, [](Vec2 p, float aa) {
        float dOut = sdChamferedOctagon(p, 0.435f, 0.19f, 0.025f);
        float dHole = sdRoundBox(p, {kHoleHalf, kHoleHalf}, kHoleR);
        return evalFramePixel(p, dOut, dHole, aa, 0.026f, 0.022f, 0.050f);
    }));

    // Plate 3: Rare - Armored GD frame with 4 glossy corner rivets
    writeRaw(pathFor("paim_progPlate3"), renderSS(288, 4, [](Vec2 p, float aa) {
        float dOut = sdRoundBox(p, {0.430f, 0.430f}, 0.105f);
        float dHole = sdRoundBox(p, {kHoleHalf, kHoleHalf}, kHoleR);
        RGBA base = evalFramePixel(p, dOut, dHole, aa, 0.026f, 0.022f, 0.048f);
        for (float sx : {-1.f, 1.f}) {
            for (float sy : {-1.f, 1.f}) {
                base = over(base, evalOrb(p, {sx * 0.345f, sy * 0.345f}, 0.038f, aa, 0.014f));
            }
        }
        return base;
    }));

    // Plate 4: Epic - Royal compass frame with 4 unified diamond finials
    writeRaw(pathFor("paim_progPlate4"), renderSS(288, 4, [](Vec2 p, float aa) {
        float dBox = sdRoundBox(p, {0.390f, 0.390f}, 0.095f);
        // 4 symmetrical diamond peaks at N/S/E/W
        Vec2 dp[4] = {{0.f, -0.465f}, {0.135f, -0.32f}, {0.f, -0.18f}, {-0.135f, -0.32f}};
        float dN = sdPolygon(p, dp, 4) - 0.010f;
        float dS = sdPolygon({p.x, -p.y}, dp, 4) - 0.010f;
        float dE = sdPolygon({p.y, -p.x}, dp, 4) - 0.010f;
        float dW = sdPolygon({p.y, p.x}, dp, 4) - 0.010f;
        float dSpikes = std::min({dN, dS, dE, dW});
        float dOut = std::min(dBox, dSpikes);
        float dHole = sdRoundBox(p, {kHoleHalf, kHoleHalf}, kHoleR);
        return evalFramePixel(p, dOut, dHole, aa, 0.024f, 0.022f, 0.042f);
    }));

    // Plate 5: Legendary - Trophy frame with curved side wings, crown finial & lower ribbon
    writeRaw(pathFor("paim_progPlate5"), renderSS(288, 4, [](Vec2 p, float aa) {
        Vec2 q = {p.x, p.y + 0.02f};
        float dBox = sdRoundBox(q, {0.370f, 0.370f}, 0.092f);
        // Top 3 crown peaks
        Vec2 topC[3] = {{0.f, -0.445f}, {0.095f, -0.32f}, {-0.095f, -0.32f}};
        Vec2 topR[3] = {{0.23f, -0.415f}, {0.30f, -0.32f}, {0.13f, -0.32f}};
        Vec2 topL[3] = {{-0.23f, -0.415f}, {-0.13f, -0.32f}, {-0.30f, -0.32f}};
        float dTop = std::min({sdPolygon(q, topC, 3), sdPolygon(q, topR, 3), sdPolygon(q, topL, 3)}) - 0.008f;
        // Side wing brackets
        Vec2 wingR[4] = {{0.33f, -0.16f}, {0.445f, -0.09f}, {0.425f, 0.12f}, {0.33f, 0.16f}};
        Vec2 wingL[4] = {{-0.33f, -0.16f}, {-0.33f, 0.16f}, {-0.425f, 0.12f}, {-0.445f, -0.09f}};
        float dWings = std::min(sdPolygon(q, wingR, 4), sdPolygon(q, wingL, 4)) - 0.012f;
        // Swallowtail honor banner across bottom
        Vec2 rib[6] = {
            {-0.455f, 0.255f}, {0.455f, 0.255f}, {0.395f, 0.345f},
            {0.455f, 0.435f}, {-0.455f, 0.435f}, {-0.395f, 0.345f}
        };
        float dRib = sdPolygon(q, rib, 6) - 0.008f;

        float dOut = std::min({dBox, dTop, dWings, dRib});
        float dHole = sdRoundBox(p, {kHoleHalf, kHoleHalf}, kHoleR);
        RGBA base = evalFramePixel(p, dOut, dHole, aa, 0.022f, 0.022f, 0.038f);
        // Center top orb jewel
        base = over(base, evalOrb(q, {0.f, -0.445f}, 0.032f, aa, 0.012f));
        return base;
    }));

    // Plate 6: Mythic - 8-ray sunburst halo behind double-beveled socket + 4 corner gems
    writeRaw(pathFor("paim_progPlate6"), renderSS(288, 4, [](Vec2 p, float aa) {
        float dSun = sdStarN(p, 0.485f, 0.355f, 8, 0.014f);
        float dBox = sdRoundBox(p, {0.375f, 0.375f}, 0.092f);
        float dOut = std::min(dSun, dBox);
        float dHole = sdRoundBox(p, {kHoleHalf, kHoleHalf}, kHoleR);
        RGBA base = evalFramePixel(p, dOut, dHole, aa, 0.022f, 0.022f, 0.040f);
        for (float sx : {-1.f, 1.f}) {
            for (float sy : {-1.f, 1.f}) {
                base = over(base, evalOrb(p, {sx * 0.312f, sy * 0.312f}, 0.036f, aa, 0.013f));
            }
        }
        return base;
    }));

    // 9. paim_progGlow (192x192)
    writeRaw(pathFor("paim_progGlow"), renderSS(192, 1, [](Vec2 p, float) {
        float t = clamp01(length(p) * 2.f);
        float a = std::pow(1.f - t, 2.25f);
        return RGBA{1.f, 1.f, 1.f, a};
    }));

    // 10. paim_progSpark (96x96) - crisp GD 4-point diamond star sparkle
    writeRaw(pathFor("paim_progSpark"), renderSS(96, 4, [](Vec2 p, float aa) {
        float dMain = sdStar4(p, 0.46f, 0.075f);
        float dCore = sdCircle(p, {0.f, 0.f}, 0.11f);
        float d = std::min(dMain, dCore);
        float a = 1.f - smoothstep(-aa * 1.5f, aa * 1.5f, d);
        if (a <= 1e-4f) return RGBA{0.f, 0.f, 0.f, 0.f};
        return RGBA{1.f, 1.f, 1.f, a};
    }));

    return 0;
}
