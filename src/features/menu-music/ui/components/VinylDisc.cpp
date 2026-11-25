#include "VinylDisc.hpp"
#include "../../../../utils/PaimonDrawNode.hpp"
#include "../../../../utils/TextureBudget.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace cocos2d;

namespace paimon::menumusic {

namespace {

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

constexpr int kDiscTexSize    = 384;
constexpr int kSheenTexSize   = 384;
constexpr int kShadowTexSize  = 192;
constexpr int kSpindleTexSize = 128;

std::vector<VinylDisc*> s_activeDiscs;
std::unordered_map<std::uint64_t, geode::Ref<CCTexture2D>> s_discTexCache;
std::unordered_map<std::uint64_t, geode::Ref<CCTexture2D>> s_sheenTexCache;
std::unordered_map<int, geode::Ref<CCTexture2D>> s_spindleTexCache;
geode::Ref<CCTexture2D> s_shadowTex;

struct Vec3 {
    float r, g, b;
};

inline float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline Vec3 mix3(Vec3 a, Vec3 b, float t) {
    t = clamp01(t);
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}

inline Vec3 add3(Vec3 a, Vec3 b, float scale = 1.f) {
    return {a.r + b.r * scale, a.g + b.g * scale, a.b + b.b * scale};
}

inline Vec3 mul3(Vec3 a, float s) {
    return {a.r * s, a.g * s, a.b * s};
}

inline float smoothstep(float e0, float e1, float x) {
    if (e1 <= e0) return x >= e1 ? 1.f : 0.f;
    float const t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

inline float gaussBand(float x, float center, float width) {
    float const d = (x - center) / std::max(width, 0.0001f);
    return std::exp(-d * d);
}

// Smooth HSV-like spectral rainbow for RetroCD thin-film diffraction.
Vec3 spectralColor(float phase) {
    float const p = phase - std::floor(phase);
    float const r = 0.5f + 0.5f * std::cos(kTau * (p + 0.00f));
    float const g = 0.5f + 0.5f * std::cos(kTau * (p + 0.33f));
    float const b = 0.5f + 0.5f * std::cos(kTau * (p + 0.67f));
    return {r, g, b};
}

// Smooth procedural 2D hash/value noise for organic ruby marble & cosmic aurora textures.
inline float hash2(float x, float y) {
    float const h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453123f;
    return h - std::floor(h);
}

float smoothNoise(float x, float y) {
    float const ix = std::floor(x);
    float const iy = std::floor(y);
    float const fx = x - ix;
    float const fy = y - iy;
    float const ux = fx * fx * (3.f - 2.f * fx);
    float const uy = fy * fy * (3.f - 2.f * fy);
    float const a = hash2(ix,       iy);
    float const b = hash2(ix + 1.f, iy);
    float const c = hash2(ix,       iy + 1.f);
    float const d = hash2(ix + 1.f, iy + 1.f);
    return lerp(lerp(a, b, ux), lerp(c, d, ux), uy);
}

float fbmNoise(float x, float y) {
    float sum = 0.f;
    float amp = 0.5f;
    float freq = 1.f;
    for (int i = 0; i < 4; ++i) {
        sum += amp * smoothNoise(x * freq, y * freq);
        freq *= 2.05f;
        amp *= 0.5f;
    }
    return sum;
}

inline void writePixel(std::vector<std::uint8_t>& buf, int idx, Vec3 col, float alpha) {
    buf[idx + 0] = static_cast<std::uint8_t>(clamp01(col.r) * 255.f + 0.5f);
    buf[idx + 1] = static_cast<std::uint8_t>(clamp01(col.g) * 255.f + 0.5f);
    buf[idx + 2] = static_cast<std::uint8_t>(clamp01(col.b) * 255.f + 0.5f);
    buf[idx + 3] = static_cast<std::uint8_t>(clamp01(alpha) * 255.f + 0.5f);
}

CCTexture2D* uploadRGBA(std::vector<std::uint8_t> const& pixels, int w, int h) {
    auto* tex = new CCTexture2D();
    if (!tex->initWithData(
            pixels.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            w, h,
            CCSizeMake(static_cast<float>(w), static_cast<float>(h)))) {
        delete tex;
        return nullptr;
    }
    tex->setAntiAliasTexParameters();
    tex->autorelease();
    return tex;
}

void drawFilledCircle(PaimonDrawNode* node, CCPoint center, float radius, ccColor4F color, int segments = 96) {
    if (!node || radius <= 0.f) return;
    std::vector<CCPoint> verts;
    verts.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        float const a = kTau * static_cast<float>(i) / static_cast<float>(segments);
        verts.emplace_back(center.x + radius * std::cos(a), center.y + radius * std::sin(a));
    }
    node->drawPolygon(verts.data(), static_cast<unsigned int>(verts.size()), color, 0.f, ccc4f(0, 0, 0, 0));
}

// Track-gap profile: 4 authentic album tracks separated by 3 narrow glossy silent bands.
float trackGapMask(float tTrack) {
    float g = 0.f;
    g = std::max(g, gaussBand(tTrack, 0.27f, 0.014f));
    g = std::max(g, gaussBand(tTrack, 0.55f, 0.015f));
    g = std::max(g, gaussBand(tTrack, 0.80f, 0.014f));
    return clamp01(g);
}

// Per-track dynamic loudness modulation (just like real vinyl where loud tracks catch more light).
float trackLoudness(float tTrack) {
    float const wave = 0.5f + 0.5f * std::sin(tTrack * 19.f + 0.8f) * std::cos(tTrack * 7.3f);
    return 0.72f + 0.28f * wave;
}

// Shared soft circular drop shadow (192x192).
CCTexture2D* getOrCreateShadowTexture() {
    if (s_shadowTex) return s_shadowTex.data();

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kShadowTexSize) * kShadowTexSize * 4, 0);
    float const invHalf = 2.f / static_cast<float>(kShadowTexSize);

    for (int y = 0; y < kShadowTexSize; ++y) {
        float const ny = (static_cast<float>(y) + 0.5f) * invHalf - 1.f;
        for (int x = 0; x < kShadowTexSize; ++x) {
            float const nx = (static_cast<float>(x) + 0.5f) * invHalf - 1.f;
            float const r = std::sqrt(nx * nx + ny * ny);
            if (r >= 1.f) continue;

            // Core contact shadow under the vinyl + soft penumbra fading cleanly to 0 at r = 1.
            float const core = 1.f - smoothstep(0.76f, 0.96f, r);
            float const soft = std::pow(clamp01(1.f - smoothstep(0.58f, 1.f, r)), 1.8f);
            float const a = clamp01(core * 0.42f + soft * 0.28f);
            int const idx = (y * kShadowTexSize + x) * 4;
            writePixel(pixels, idx, {0.01f, 0.01f, 0.03f}, a);
        }
    }

    if (auto* tex = uploadRGBA(pixels, kShadowTexSize, kShadowTexSize)) {
        s_shadowTex = tex;
        return tex;
    }
    return nullptr;
}

// High-definition rotating vinyl disc body (384x384 RGBA8888).
CCTexture2D* getOrCreateDiscTexture(VinylStyle style, bool detailedGrooves, float labelRatio) {
    int const labelKey = static_cast<int>(std::round(labelRatio * 100.f));
    std::uint64_t const key =
        (static_cast<std::uint64_t>(static_cast<int>(style)) << 16) |
        (static_cast<std::uint64_t>(detailedGrooves ? 1 : 0) << 12) |
        static_cast<std::uint64_t>(labelKey & 0xFFF);

    if (auto it = s_discTexCache.find(key); it != s_discTexCache.end() && it->second) {
        return it->second.data();
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kDiscTexSize) * kDiscTexSize * 4, 0);
    float const invHalf = 2.f / static_cast<float>(kDiscTexSize);
    float const aa = 2.4f * invHalf;

    float const labelR       = std::clamp(labelRatio, 0.25f, 0.65f);
    float const labelTrimOut = labelR + 0.014f;
    float const deadWaxOut   = labelR + 0.088f;
    float const grooveOuter  = 0.942f;
    float const rimInner     = 0.968f;

    for (int y = 0; y < kDiscTexSize; ++y) {
        float const ny = (static_cast<float>(y) + 0.5f) * invHalf - 1.f;
        for (int x = 0; x < kDiscTexSize; ++x) {
            float const nx = (static_cast<float>(x) + 0.5f) * invHalf - 1.f;
            float const r = std::sqrt(nx * nx + ny * ny);
            if (r > 1.f + aa) continue;

            float const outerAlpha = 1.f - smoothstep(1.f - aa, 1.f, r);
            if (outerAlpha <= 0.001f) continue;

            float const angle = std::atan2(ny, nx);
            float const normAngle = (angle + kPi) / kTau; // 0..1

            // Micro-groove multi-harmonic signal in [-1..1].
            float grooveWave = 0.f;
            float trackMask = 0.f;
            float gap = 0.f;
            float loud = 1.f;
            if (r >= deadWaxOut && r <= grooveOuter) {
                float const tTrack = clamp01((r - deadWaxOut) / std::max(0.01f, grooveOuter - deadWaxOut));
                gap = trackGapMask(tTrack);
                loud = trackLoudness(tTrack);
                float const fadeEnds = smoothstep(0.f, 0.025f, tTrack) * (1.f - smoothstep(0.975f, 1.f, tTrack));
                trackMask = fadeEnds * (1.f - gap * 0.88f);
                if (detailedGrooves) {
                    float const g1 = std::sin(r * 415.f);
                    float const g2 = std::sin(r * 228.f + 1.15f);
                    float const g3 = std::sin(r * 740.f + 0.42f);
                    float const microGrain = 0.14f * std::sin(angle * 5.f + r * 32.f)
                                           + 0.10f * std::cos(angle * 11.f - r * 19.f);
                    grooveWave = (0.50f * g1 + 0.30f * g2 + 0.20f * g3 + microGrain) * trackMask * loud;
                } else {
                    grooveWave = 0.16f * std::sin(r * 120.f) * trackMask;
                }
            }

            // Dead-wax spiral run-out groove.
            float const runOutR = labelTrimOut + 0.036f + 0.014f * normAngle;
            float const runOutGroove = (r > labelTrimOut && r < deadWaxOut)
                ? gaussBand(r, runOutR, 0.0045f)
                : 0.f;

            // Lead-in guard lip + rounded outer bevel.
            float const outerBevel = smoothstep(rimInner, 1.f, r);
            float const guardLip = gaussBand(r, 0.975f, 0.011f);
            float const leadInEntry = gaussBand(r, 0.954f, 0.0045f);
            float const labelTrim = gaussBand(r, labelR + 0.006f, 0.0085f);
            float const labelRecess = gaussBand(r, labelTrimOut + 0.004f, 0.012f);

            // Base material evaluation per VinylStyle.
            Vec3 col{0.05f, 0.05f, 0.07f};
            float matAlpha = 1.f;

            switch (style) {
                case VinylStyle::GoldRecord: {
                    float const radialTone = 0.5f + 0.5f * std::sin(r * 16.f + 0.5f);
                    Vec3 const goldDeep{0.58f, 0.41f, 0.10f};
                    Vec3 const goldMid {0.86f, 0.68f, 0.24f};
                    Vec3 const goldHi  {1.00f, 0.90f, 0.48f};
                    col = mix3(goldDeep, goldMid, 0.45f + 0.55f * radialTone);
                    col = mix3(col, grooveWave > 0.f ? goldHi : goldDeep, std::fabs(grooveWave) * 0.58f);
                    if (gap > 0.02f) col = mix3(col, goldHi, gap * 0.28f);
                    if (r < deadWaxOut && r > labelR) col = mix3(col, goldMid, 0.35f);
                    col = add3(col, goldHi, guardLip * 0.30f + labelTrim * 0.35f - runOutGroove * 0.18f);
                    col = mul3(col, 1.f - outerBevel * 0.26f - labelRecess * 0.16f);
                    break;
                }

                case VinylStyle::PlatinumChrome: {
                    float const brushed = 0.5f + 0.5f * std::sin(r * 22.f);
                    Vec3 const platDark{0.52f, 0.57f, 0.65f};
                    Vec3 const platMid {0.78f, 0.83f, 0.90f};
                    Vec3 const platHi  {0.97f, 0.99f, 1.00f};
                    col = mix3(platDark, platMid, 0.5f + 0.5f * brushed);
                    col = mix3(col, grooveWave > 0.f ? platHi : platDark, std::fabs(grooveWave) * 0.55f);
                    if (gap > 0.02f) col = mix3(col, platHi, gap * 0.30f);
                    col = add3(col, platHi, guardLip * 0.28f + labelTrim * 0.30f - runOutGroove * 0.16f);
                    col = mul3(col, 1.f - outerBevel * 0.24f - labelRecess * 0.15f);
                    break;
                }

                case VinylStyle::RetroCD: {
                    // Authentic 90s polycarbonate CD: reflective aluminum data zone + clear clamping ring.
                    Vec3 const cdBase{0.79f, 0.83f, 0.88f};
                    Vec3 const cdHi  {0.96f, 0.98f, 1.00f};
                    Vec3 const cdDark{0.62f, 0.67f, 0.74f};
                    float const rainbowPhase = normAngle * 2.0f + r * 1.45f;
                    Vec3 const irid = spectralColor(rainbowPhase);
                    col = mix3(cdBase, irid, 0.20f * smoothstep(deadWaxOut, deadWaxOut + 0.08f, r));
                    col = mix3(col, grooveWave > 0.f ? cdHi : cdDark, std::fabs(grooveWave) * 0.22f);
                    // Clear polycarbonate inner clamping zone around center.
                    if (r < deadWaxOut && r >= labelR) {
                        float const clampRing = gaussBand(r, labelR + 0.048f, 0.012f);
                        col = mix3({0.58f, 0.63f, 0.70f}, cdHi, clampRing * 0.55f);
                        matAlpha = 0.62f + 0.30f * clampRing;
                    }
                    col = add3(col, cdHi, guardLip * 0.32f + labelTrim * 0.28f);
                    col = mul3(col, 1.f - outerBevel * 0.20f);
                    break;
                }

                case VinylStyle::CyberpunkNeon: {
                    Vec3 const cyberBase{0.05f, 0.02f, 0.11f};
                    Vec3 const neonCyan {0.00f, 0.95f, 1.00f};
                    Vec3 const neonPink {1.00f, 0.14f, 0.72f};
                    float const dualPhase = 0.5f + 0.5f * std::sin(r * 42.f + angle * 2.f);
                    Vec3 const grooveNeon = mix3(neonCyan, neonPink, dualPhase);
                    col = cyberBase;
                    float const grooveLit = std::max(0.f, grooveWave) * 0.62f;
                    col = add3(col, grooveNeon, grooveLit);
                    // Subtle synthwave radial ring accents in track gaps.
                    col = add3(col, neonPink, gap * 0.26f);
                    col = add3(col, neonCyan, guardLip * 0.85f + leadInEntry * 0.35f + runOutGroove * 0.45f);
                    col = add3(col, neonPink, labelTrim * 0.85f);
                    col = mul3(col, 1.f - outerBevel * 0.25f);
                    break;
                }

                case VinylStyle::GlassClear: {
                    Vec3 const frostBase{0.82f, 0.90f, 0.98f};
                    Vec3 const frostHi  {1.00f, 1.00f, 1.00f};
                    float const fresnel = std::pow(clamp01(r), 2.4f);
                    col = mix3(frostBase, frostHi, 0.35f + 0.45f * fresnel + std::max(0.f, grooveWave) * 0.35f);
                    matAlpha = 0.28f + 0.22f * fresnel + std::fabs(grooveWave) * 0.16f
                             + guardLip * 0.42f + labelTrim * 0.45f + runOutGroove * 0.12f;
                    if (r < deadWaxOut && r >= labelR) matAlpha += 0.08f;
                    break;
                }

                case VinylStyle::RubyMarbled: {
                    // Multi-octave organic ruby & burgundy marbling rotating with the vinyl!
                    float const warp = fbmNoise(nx * 3.1f + 2.3f, ny * 3.1f - 1.7f);
                    float const swirl = fbmNoise(nx * 4.5f + warp * 2.2f, ny * 4.5f - warp * 2.2f);
                    Vec3 const wineDeep {0.22f, 0.015f, 0.055f};
                    Vec3 const rubyMid  {0.58f, 0.05f,  0.14f};
                    Vec3 const rubyGlow {0.90f, 0.17f,  0.29f};
                    Vec3 const brass    {0.92f, 0.76f,  0.34f};
                    col = mix3(wineDeep, rubyMid, smoothstep(0.22f, 0.72f, swirl));
                    col = mix3(col, rubyGlow, smoothstep(0.58f, 0.92f, swirl) * 0.65f);
                    col = mix3(col, grooveWave > 0.f ? rubyGlow : wineDeep, std::fabs(grooveWave) * 0.36f);
                    if (gap > 0.02f) col = mix3(col, wineDeep, gap * 0.30f);
                    col = add3(col, rubyGlow, guardLip * 0.32f + leadInEntry * 0.14f);
                    col = mix3(col, brass, labelTrim * 0.90f);
                    col = mul3(col, 1.f - outerBevel * 0.28f - labelRecess * 0.18f);
                    matAlpha = 0.94f;
                    break;
                }

                case VinylStyle::AuroraCosmic: {
                    float const n1 = fbmNoise(nx * 2.8f + 4.1f, ny * 2.8f + 1.2f);
                    float const n2 = fbmNoise(nx * 4.2f - n1 * 1.8f, ny * 4.2f + n1 * 1.8f);
                    float const ribbon = smoothstep(0.36f, 0.76f, n2);
                    Vec3 const spaceBg  {0.03f, 0.04f, 0.11f};
                    Vec3 const emerald  {0.10f, 0.88f, 0.56f};
                    Vec3 const cyanGlow {0.10f, 0.78f, 1.00f};
                    Vec3 const violet   {0.48f, 0.18f, 0.88f};
                    Vec3 const auroraCol = mix3(mix3(violet, cyanGlow, n1), emerald, smoothstep(0.45f, 0.85f, n2));
                    col = mix3(spaceBg, auroraCol, ribbon * 0.52f);
                    col = add3(col, cyanGlow, std::max(0.f, grooveWave) * 0.26f);
                    // Tiny subtle star flecks embedded in the cosmic vinyl resin.
                    float const starCell = hash2(std::floor(nx * 55.f), std::floor(ny * 55.f));
                    if (starCell > 0.992f && r > labelTrimOut && r < rimInner) {
                        col = add3(col, {0.85f, 0.98f, 1.0f}, 0.55f);
                    }
                    col = add3(col, emerald, guardLip * 0.45f + runOutGroove * 0.25f);
                    col = add3(col, cyanGlow, labelTrim * 0.75f);
                    col = mul3(col, 1.f - outerBevel * 0.24f);
                    break;
                }

                case VinylStyle::ClassicBlack:
                default: {
                    Vec3 const vinylDeep{0.035f, 0.036f, 0.044f};
                    Vec3 const vinylMid {0.072f, 0.075f, 0.090f};
                    Vec3 const vinylHi  {0.175f, 0.182f, 0.212f};
                    col = mix3(vinylDeep, vinylMid, 0.45f + 0.25f * std::sin(r * 14.f));
                    col = mix3(col, grooveWave > 0.f ? vinylHi : vinylDeep, std::fabs(grooveWave) * 0.62f);
                    // Glossy darker track gaps & satin dead-wax contrast.
                    if (gap > 0.02f) col = mix3(col, {0.028f, 0.030f, 0.038f}, gap * 0.65f);
                    if (r < deadWaxOut && r > labelTrimOut) {
                        col = mix3(col, {0.055f, 0.058f, 0.070f}, 0.55f);
                    }
                    col = add3(col, {0.24f, 0.25f, 0.30f}, guardLip * 0.55f + leadInEntry * 0.20f + runOutGroove * 0.22f);
                    col = add3(col, {0.62f, 0.65f, 0.72f}, labelTrim * 0.65f);
                    col = mul3(col, 1.f - outerBevel * 0.32f - labelRecess * 0.22f);
                    break;
                }
            }

            // Classic vintage pressed record label fallback inside r < labelR.
            if (r < labelR) {
                float const lr = r / std::max(0.01f, labelR); // 0..1 inside label
                Vec3 lblInner, lblOuter, lblAccent;
                switch (style) {
                    case VinylStyle::GoldRecord:
                        lblInner = {0.22f, 0.15f, 0.05f}; lblOuter = {0.13f, 0.08f, 0.02f}; lblAccent = {0.98f, 0.84f, 0.38f}; break;
                    case VinylStyle::PlatinumChrome:
                        lblInner = {0.16f, 0.20f, 0.28f}; lblOuter = {0.09f, 0.11f, 0.16f}; lblAccent = {0.88f, 0.94f, 1.00f}; break;
                    case VinylStyle::RetroCD:
                        lblInner = {0.18f, 0.24f, 0.34f}; lblOuter = {0.10f, 0.13f, 0.20f}; lblAccent = {0.62f, 0.92f, 1.00f}; break;
                    case VinylStyle::CyberpunkNeon:
                        lblInner = {0.14f, 0.04f, 0.24f}; lblOuter = {0.06f, 0.02f, 0.12f}; lblAccent = {0.00f, 0.95f, 1.00f}; break;
                    case VinylStyle::GlassClear:
                        lblInner = {0.20f, 0.26f, 0.34f}; lblOuter = {0.12f, 0.16f, 0.22f}; lblAccent = {0.90f, 0.96f, 1.00f}; break;
                    case VinylStyle::RubyMarbled:
                        lblInner = {0.32f, 0.05f, 0.10f}; lblOuter = {0.18f, 0.02f, 0.05f}; lblAccent = {0.94f, 0.78f, 0.36f}; break;
                    case VinylStyle::AuroraCosmic:
                        lblInner = {0.08f, 0.18f, 0.28f}; lblOuter = {0.04f, 0.08f, 0.16f}; lblAccent = {0.22f, 0.95f, 0.82f}; break;
                    case VinylStyle::ClassicBlack:
                    default:
                        lblInner = {0.58f, 0.13f, 0.16f}; lblOuter = {0.36f, 0.06f, 0.08f}; lblAccent = {0.94f, 0.82f, 0.48f}; break;
                }
                Vec3 lbl = mix3(lblInner, lblOuter, smoothstep(0.15f, 0.98f, lr));
                // Subtle embossed pressing step ring + classic outer & inner decorative rings.
                float const pressRing = gaussBand(lr, 0.68f, 0.022f);
                float const outerDeco = gaussBand(lr, 0.88f, 0.018f);
                float const innerDeco = gaussBand(lr, 0.36f, 0.016f);
                lbl = mix3(lbl, lblAccent, (outerDeco * 0.65f + innerDeco * 0.45f));
                lbl = add3(lbl, {0.12f, 0.12f, 0.12f}, pressRing * 0.35f);

                float const edgeBlend = smoothstep(labelR - aa * 1.4f, labelR, r);
                col = mix3(lbl, col, edgeBlend);
                matAlpha = lerp(1.f, matAlpha, edgeBlend);
            }

            int const idx = (y * kDiscTexSize + x) * 4;
            writePixel(pixels, idx, col, outerAlpha * matAlpha);
        }
    }

    if (auto* tex = uploadRGBA(pixels, kDiscTexSize, kDiscTexSize)) {
        s_discTexCache[key] = tex;
        return tex;
    }
    return nullptr;
}

// Stationary optical lighting & anisotropic bowtie specular sheen (384x384 RGBA8888).
CCTexture2D* getOrCreateSheenTexture(VinylStyle style, float labelRatio, bool anisotropicGleam) {
    int const labelKey = static_cast<int>(std::round(labelRatio * 100.f));
    std::uint64_t const key =
        (static_cast<std::uint64_t>(static_cast<int>(style)) << 16) |
        (static_cast<std::uint64_t>(anisotropicGleam ? 1 : 0) << 12) |
        static_cast<std::uint64_t>(labelKey & 0xFFF);

    if (auto it = s_sheenTexCache.find(key); it != s_sheenTexCache.end() && it->second) {
        return it->second.data();
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSheenTexSize) * kSheenTexSize * 4, 0);
    float const invHalf = 2.f / static_cast<float>(kSheenTexSize);
    float const aa = 2.4f * invHalf;

    float const labelR     = std::clamp(labelRatio, 0.25f, 0.65f);
    float const deadWaxOut = labelR + 0.088f;

    // Two opposing studio light cones along 40 deg and 220 deg.
    constexpr float kLightAngle = 0.70f;

    Vec3 flareA{1.f, 1.f, 1.f};
    Vec3 flareB{0.92f, 0.96f, 1.f};
    Vec3 rimCol{1.f, 1.f, 1.f};
    float flareGain = 0.28f;
    float rimGain   = 0.34f;

    switch (style) {
        case VinylStyle::GoldRecord:
            flareA = {1.00f, 0.96f, 0.74f}; flareB = {1.00f, 0.88f, 0.50f};
            rimCol = {1.00f, 0.97f, 0.78f}; flareGain = 0.42f; rimGain = 0.48f;
            break;
        case VinylStyle::PlatinumChrome:
            flareA = {0.96f, 0.99f, 1.00f}; flareB = {0.82f, 0.92f, 1.00f};
            rimCol = {1.00f, 1.00f, 1.00f}; flareGain = 0.44f; rimGain = 0.52f;
            break;
        case VinylStyle::RetroCD:
            flareA = {0.25f, 0.92f, 1.00f}; flareB = {1.00f, 0.22f, 0.78f};
            rimCol = {0.96f, 0.99f, 1.00f}; flareGain = 0.40f; rimGain = 0.44f;
            break;
        case VinylStyle::CyberpunkNeon:
            flareA = {0.00f, 0.96f, 1.00f}; flareB = {1.00f, 0.16f, 0.76f};
            rimCol = {0.10f, 1.00f, 0.98f}; flareGain = 0.38f; rimGain = 0.50f;
            break;
        case VinylStyle::GlassClear:
            flareA = {1.00f, 1.00f, 1.00f}; flareB = {0.84f, 0.95f, 1.00f};
            rimCol = {1.00f, 1.00f, 1.00f}; flareGain = 0.34f; rimGain = 0.46f;
            break;
        case VinylStyle::RubyMarbled:
            flareA = {1.00f, 0.84f, 0.88f}; flareB = {1.00f, 0.56f, 0.68f};
            rimCol = {1.00f, 0.88f, 0.90f}; flareGain = 0.34f; rimGain = 0.42f;
            break;
        case VinylStyle::AuroraCosmic:
            flareA = {0.18f, 0.98f, 0.82f}; flareB = {0.22f, 0.82f, 1.00f};
            rimCol = {0.42f, 1.00f, 0.92f}; flareGain = 0.36f; rimGain = 0.44f;
            break;
        case VinylStyle::ClassicBlack:
        default:
            flareA = {0.96f, 0.98f, 1.00f}; flareB = {0.88f, 0.92f, 1.00f};
            rimCol = {0.95f, 0.97f, 1.00f}; flareGain = 0.28f; rimGain = 0.34f;
            break;
    }

    for (int y = 0; y < kSheenTexSize; ++y) {
        float const ny = (static_cast<float>(y) + 0.5f) * invHalf - 1.f;
        for (int x = 0; x < kSheenTexSize; ++x) {
            float const nx = (static_cast<float>(x) + 0.5f) * invHalf - 1.f;
            float const r = std::sqrt(nx * nx + ny * ny);
            if (r > 1.f + aa) continue;

            float const mask = 1.f - smoothstep(1.f - aa, 1.f, r);
            if (mask <= 0.001f) continue;

            float const angle = std::atan2(ny, nx);
            float const cLight = std::cos(angle - kLightAngle);
            float const absC = std::fabs(cLight);

            Vec3 col{0.f, 0.f, 0.f};
            float alpha = 0.f;

            // 1. Anisotropic brushed-groove hourglass reflection cones (only over vinyl zone).
            if (anisotropicGleam && r > labelR) {
                float const zoneMask = smoothstep(labelR, labelR + 0.03f, r) * (1.f - smoothstep(0.955f, 0.992f, r));
                float const broadLobe = std::pow(absC, 5.f);
                float const sharpStreak = std::pow(absC, 26.f);
                float const radialProfile = 0.72f + 0.28f * std::sin(clamp01((r - labelR) / (1.f - labelR)) * kPi);
                // Slightly dimmer across smooth dead-wax so groove reflections pop realistically.
                float const grooveBoost = lerp(0.55f, 1.f, smoothstep(deadWaxOut - 0.02f, deadWaxOut + 0.02f, r));

                Vec3 coneCol = (cLight >= 0.f) ? flareA : flareB;
                if (style == VinylStyle::RetroCD) {
                    // Vivid thin-film spectral dispersion inside the CD reflection cones!
                    float const specPhase = (cLight >= 0.f ? 0.10f : 0.55f) + (angle - kLightAngle) * 0.45f + r * 0.35f;
                    coneCol = mix3(coneCol, spectralColor(specPhase), 0.72f);
                } else if (style == VinylStyle::ClassicBlack) {
                    // Subtle micro-iridescent wing tint on glossy lacquer vinyl.
                    float const wing = broadLobe * (1.f - sharpStreak);
                    coneCol = mix3(coneCol, {0.82f, 0.90f, 1.0f}, wing * 0.25f);
                }

                float const coneIntensity = (broadLobe * 0.46f + sharpStreak * 0.85f)
                                          * radialProfile * grooveBoost * zoneMask * flareGain;
                col = add3(col, coneCol, coneIntensity);
                alpha += coneIntensity;
            }

            // 2. Crisp upper-left studio bevel rim catchlight.
            float const rimArc = std::pow(std::max(0.f, std::cos(angle - 2.30f)), 5.f);
            float const rimBand = gaussBand(r, 0.976f, 0.015f);
            float const rimInt = rimArc * rimBand * rimGain;
            col = add3(col, rimCol, rimInt);
            alpha += rimInt;

            // 3. Soft diagonal studio specular glaze across upper-left of the center label.
            if (r < labelR) {
                float const lblLight = std::max(0.f, (-nx * 0.65f + ny * 0.76f) / std::max(0.01f, labelR));
                float const glaze = std::pow(clamp01(lblLight), 2.2f) * 0.11f;
                col = add3(col, {1.f, 1.f, 1.f}, glaze);
                alpha += glaze;
            }

            // Normalize premultiplied color back for standard non-premultiplied RGBA texture.
            if (alpha > 0.002f) {
                float const outA = clamp01(alpha) * mask;
                Vec3 const normCol = mul3(col, 1.f / std::max(alpha, 0.001f));
                int const idx = (y * kSheenTexSize + x) * 4;
                writePixel(pixels, idx, normCol, outA);
            }
        }
    }

    if (auto* tex = uploadRGBA(pixels, kSheenTexSize, kSheenTexSize)) {
        s_sheenTexCache[key] = tex;
        return tex;
    }
    return nullptr;
}

// High-detail 3D machined metallic spindle hub (128x128 RGBA8888).
CCTexture2D* getOrCreateSpindleTexture(VinylStyle style) {
    int const key = static_cast<int>(style);
    if (auto it = s_spindleTexCache.find(key); it != s_spindleTexCache.end() && it->second) {
        return it->second.data();
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSpindleTexSize) * kSpindleTexSize * 4, 0);
    float const invHalf = 2.f / static_cast<float>(kSpindleTexSize);
    float const aa = 2.5f * invHalf;

    Vec3 metalLight{0.96f, 0.97f, 1.00f};
    Vec3 metalDark {0.42f, 0.45f, 0.52f};
    Vec3 boreCol   {0.04f, 0.04f, 0.06f};
    Vec3 rimGlint  {1.00f, 1.00f, 1.00f};

    if (style == VinylStyle::GoldRecord || style == VinylStyle::RubyMarbled) {
        metalLight = {1.00f, 0.92f, 0.56f};
        metalDark  = {0.56f, 0.39f, 0.10f};
        boreCol    = {0.10f, 0.06f, 0.02f};
        rimGlint   = {1.00f, 0.98f, 0.82f};
    } else if (style == VinylStyle::CyberpunkNeon) {
        metalLight = {0.26f, 0.22f, 0.38f};
        metalDark  = {0.08f, 0.06f, 0.14f};
        boreCol    = {0.00f, 0.92f, 1.00f};
        rimGlint   = {1.00f, 0.24f, 0.78f};
    } else if (style == VinylStyle::RetroCD) {
        metalLight = {0.82f, 0.88f, 0.95f};
        metalDark  = {0.46f, 0.52f, 0.60f};
        boreCol    = {0.03f, 0.04f, 0.06f};
    }

    for (int y = 0; y < kSpindleTexSize; ++y) {
        float const ny = (static_cast<float>(y) + 0.5f) * invHalf - 1.f;
        for (int x = 0; x < kSpindleTexSize; ++x) {
            float const nx = (static_cast<float>(x) + 0.5f) * invHalf - 1.f;
            float const r = std::sqrt(nx * nx + ny * ny);
            if (r >= 1.f) continue;

            float const lightDot = clamp01(0.5f + 0.5f * (-nx * 0.70f + ny * 0.71f) / std::max(r, 0.15f));
            int const idx = (y * kSpindleTexSize + x) * 4;

            if (r > 0.70f) {
                // Soft contact shadow cast by the raised metallic spindle washer onto the album label.
                float const sa = (1.f - smoothstep(0.70f, 0.98f, r)) * 0.42f;
                writePixel(pixels, idx, {0.02f, 0.02f, 0.04f}, sa);
            } else if (r > 0.28f) {
                // Brushed machined collar with concentric lathe micro-ring & beveled edge.
                float const outerEdge = 1.f - smoothstep(0.66f, 0.70f, r);
                float const bevel = gaussBand(r, 0.64f, 0.055f);
                float const lathe = 0.5f + 0.5f * std::sin(r * 75.f);
                Vec3 col = mix3(metalDark, metalLight, 0.22f + 0.62f * lightDot + 0.16f * lathe);
                col = add3(col, rimGlint, bevel * lightDot * 0.45f);
                if (style == VinylStyle::CyberpunkNeon) {
                    col = add3(col, {0.0f, 0.95f, 1.0f}, gaussBand(r, 0.65f, 0.04f) * 0.85f);
                }
                writePixel(pixels, idx, col, clamp01(outerEdge + (1.f - outerEdge) * 0.42f));
            } else {
                // Center spindle bore with subtle inner rim catchlight & specular pinpoint.
                float const innerRim = smoothstep(0.28f - aa * 2.f, 0.28f, r);
                Vec3 col = mix3(boreCol, metalLight, innerRim * (1.f - lightDot) * 0.55f);
                // Crisp specular glint near upper-left of the spindle pin.
                float const gx = nx + 0.08f;
                float const gy = ny - 0.08f;
                float const glint = std::exp(-(gx * gx + gy * gy) / 0.0045f);
                col = add3(col, rimGlint, glint * 0.85f);
                writePixel(pixels, idx, col, 1.f);
            }
        }
    }

    if (auto* tex = uploadRGBA(pixels, kSpindleTexSize, kSpindleTexSize)) {
        s_spindleTexCache[key] = tex;
        return tex;
    }
    return nullptr;
}

} // namespace


VinylDisc* VinylDisc::create(float radius, std::optional<VinylStyle> style) {
    auto ret = new VinylDisc();
    if (ret && ret->init(radius, style)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

VinylDisc::~VinylDisc() {
    CC_SAFE_RELEASE_NULL(m_currentCoverTexture);
    auto it = std::find(s_activeDiscs.begin(), s_activeDiscs.end(), this);
    if (it != s_activeDiscs.end()) {
        s_activeDiscs.erase(it);
    }
}

void VinylDisc::clearTextureCache() {
    s_discTexCache.clear();
    s_sheenTexCache.clear();
    s_spindleTexCache.clear();
    s_shadowTex = nullptr;
}

bool VinylDisc::init(float radius, std::optional<VinylStyle> style) {
    if (!CCNode::init()) return false;

    m_radius = std::max(20.f, radius);
    m_style = style.has_value() ? *style : getSavedStyle();
    m_anisotropicGleam = geode::Mod::get()->getSavedValue<bool>("menuMusicVinylGleam", true);
    m_detailedGrooves = geode::Mod::get()->getSavedValue<bool>("menuMusicVinylGrooves", true);
    int const labelIdx = geode::Mod::get()->getSavedValue<int>("menuMusicVinylLabelSize", 1);
    if (labelIdx == 0) m_labelRatio = 0.38f;
    else if (labelIdx == 2) m_labelRatio = 0.54f;
    else m_labelRatio = 0.46f;

    m_spinSpeed = geode::Mod::get()->getSavedValue<float>("menuMusicVinylSpeed", 40.f);
    m_currentSpinSpeed = 0.f;

    float const size = m_radius * 2.f;
    this->setContentSize({size, size});
    this->setAnchorPoint({0.5f, 0.5f});

    // Soft floating drop shadow underneath the disc (does not rotate).
    if (auto* shadowTex = getOrCreateShadowTexture()) {
        if (auto* shadowSpr = CCSprite::createWithTexture(shadowTex)) {
            float const targetDia = size * 1.12f;
            float const sprW = std::max(1.f, shadowSpr->getContentSize().width);
            shadowSpr->setScale(targetDia / sprW);
            shadowSpr->setPosition({m_radius + m_radius * 0.04f, m_radius - m_radius * 0.06f});
            this->addChild(shadowSpr, 0);
            m_shadowNode = shadowSpr;
        }
    }

    m_rotating = CCNode::create();
    m_rotating->setAnchorPoint({0.5f, 0.5f});
    m_rotating->setContentSize({size, size});
    m_rotating->setPosition({m_radius, m_radius});
    this->addChild(m_rotating, 1);

    buildGeometry();
    buildAnisotropicSheen();

    s_activeDiscs.push_back(this);

    return true;
}

void VinylDisc::buildGeometry() {
    if (!m_rotating) return;

    if (m_discBody) {
        m_discBody->removeFromParent();
        m_discBody = nullptr;
    }
    if (m_coverClip) {
        m_coverClip->removeFromParent();
        m_coverClip = nullptr;
        m_coverSprite = nullptr;
    }
    if (m_spindleNode) {
        m_spindleNode->removeFromParent();
        m_spindleNode = nullptr;
    }

    float const size = m_radius * 2.f;
    CCPoint const center = {m_radius, m_radius};

    // 1. High-definition procedural vinyl body sprite (384x384 anti-aliased RGBA).
    // Scale against sprite point size (already divided by CC_CONTENT_SCALE_FACTOR), not raw pixels.
    m_discBody = CCNode::create();
    m_discBody->setContentSize({size, size});
    m_rotating->addChild(m_discBody, 0);

    if (auto* discTex = getOrCreateDiscTexture(m_style, m_detailedGrooves, m_labelRatio)) {
        if (auto* bodySpr = CCSprite::createWithTexture(discTex)) {
            float const sprW = std::max(1.f, bodySpr->getContentSize().width);
            bodySpr->setPosition(center);
            bodySpr->setScale(size / sprW);
            m_discBody->addChild(bodySpr, 0);
        }
    }

    // 2. Smooth 96-segment circular clipping node for the center album cover artwork.
    auto* stencil = PaimonDrawNode::create();
    drawFilledCircle(stencil, center, m_radius * m_labelRatio, {1, 1, 1, 1}, 96);

    m_coverClip = CCClippingNode::create();
    m_coverClip->setStencil(stencil);
    m_coverClip->setAlphaThreshold(0.05f);
    m_coverClip->setContentSize({size, size});
    m_coverClip->setAnchorPoint({0.5f, 0.5f});
    m_coverClip->setPosition(center);
    m_rotating->addChild(m_coverClip, 2);

    // 3. 3D machined metallic spindle hub (128x128 crisp texture).
    // Mounted stationary on the disc root above sheen so directional specular highlights stay steady.
    m_spindleNode = CCNode::create();
    m_spindleNode->setContentSize({size, size});
    m_spindleNode->setPosition({0.f, 0.f});
    this->addChild(m_spindleNode, 7);

    if (auto* spindleTex = getOrCreateSpindleTexture(m_style)) {
        if (auto* spindleSpr = CCSprite::createWithTexture(spindleTex)) {
            float const spindleDia = m_radius * 0.25f;
            float const sprW = std::max(1.f, spindleSpr->getContentSize().width);
            spindleSpr->setPosition(center);
            spindleSpr->setScale(spindleDia / sprW);
            m_spindleNode->addChild(spindleSpr, 0);
        }
    }
}

void VinylDisc::buildAnisotropicSheen() {
    if (m_sheenNode) {
        m_sheenNode->removeFromParent();
        m_sheenNode = nullptr;
    }

    float const size = m_radius * 2.f;
    CCPoint const center = {m_radius, m_radius};

    m_sheenNode = CCNode::create();
    m_sheenNode->setContentSize({size, size});
    m_sheenNode->setPosition({0.f, 0.f});
    this->addChild(m_sheenNode, 6);

    if (auto* sheenTex = getOrCreateSheenTexture(m_style, m_labelRatio, m_anisotropicGleam)) {
        if (auto* sheenSpr = CCSprite::createWithTexture(sheenTex)) {
            float const sprW = std::max(1.f, sheenSpr->getContentSize().width);
            sheenSpr->setPosition(center);
            sheenSpr->setScale(size / sprW);
            m_sheenNode->addChild(sheenSpr, 0);
        }
    }
}

void VinylDisc::setCoverFromPath(const std::string& absolutePath) {
    if (absolutePath == m_coverPath && (m_coverSprite || m_coverLoading)) return;
    m_coverPath = absolutePath;
    auto const generation = ++m_coverGeneration;
    m_coverLoading = false;
    if (absolutePath.empty()) {
        if (m_coverSprite) { m_coverSprite->removeFromParent(); m_coverSprite = nullptr; }
        CC_SAFE_RELEASE_NULL(m_currentCoverTexture);
        return;
    }
    m_coverLoading = true;
    geode::WeakRef<VinylDisc> weak = this;
    paimon::image::loadBudgetedAsync(absolutePath, [weak, generation](CCTexture2D* texture) {
        auto self = weak.lock();
        if (!self || generation != self->m_coverGeneration) return;
        self->m_coverLoading = false;
        if (texture) {
            self->applyCoverTexture(texture);
        } else if (self->m_coverSprite) {
            self->m_coverSprite->removeFromParent();
            self->m_coverSprite = nullptr;
            CC_SAFE_RELEASE_NULL(self->m_currentCoverTexture);
        }
    });
}

void VinylDisc::applyCoverTexture(CCTexture2D* texture) {
    if (m_currentCoverTexture != texture) {
        CC_SAFE_RELEASE(m_currentCoverTexture);
        m_currentCoverTexture = texture;
        CC_SAFE_RETAIN(m_currentCoverTexture);
    }
    if (m_coverSprite) { m_coverSprite->removeFromParent(); m_coverSprite = nullptr; }
    if (!texture || !m_coverClip) return;

    m_coverSprite = CCSprite::createWithTexture(texture);
    if (!m_coverSprite) return;

    const CCSize texSize = m_coverSprite->getContentSize();
    const float holeDiameter = m_radius * 2.f * m_labelRatio + 1.f;
    const float minSide = std::min(texSize.width, texSize.height);
    if (minSide <= 0.f) return;
    const float scale = holeDiameter / minSide;
    m_coverSprite->setScale(scale);
    m_coverSprite->setAnchorPoint({0.5f, 0.5f});
    m_coverSprite->setPosition({m_radius, m_radius});
    m_coverClip->addChild(m_coverSprite, 0);
    setPausedAppearance(m_paused);
}

void VinylDisc::clearCover() {
    setCoverFromPath("");
}

void VinylDisc::onExit() {
    ++m_coverGeneration;
    m_coverLoading = false;
    this->unschedule(schedule_selector(VinylDisc::tick));
    CCNode::onExit();
}

void VinylDisc::startSpinning() {
    if (m_spinning) return;
    m_spinning = true;
    this->schedule(schedule_selector(VinylDisc::tick));
}

void VinylDisc::stopSpinning() {
    if (!m_spinning) return;
    m_spinning = false;
    // Let tick coast gently to rest via turntable inertia before unscheduling.
    if (std::fabs(m_currentSpinSpeed) < 0.5f) {
        m_currentSpinSpeed = 0.f;
        this->unschedule(schedule_selector(VinylDisc::tick));
    }
}

void VinylDisc::tick(float dt) {
    if (!m_rotating) return;
    float const safeDt = std::clamp(dt, 0.f, 0.05f);
    bool const playOnly = geode::Mod::get()->getSavedValue<bool>("menuMusicVinylSpinOnPlayOnly", true);
    bool const shouldSpin = m_spinning && !(playOnly && m_paused);
    float const targetSpeed = shouldSpin ? m_spinSpeed : 0.f;

    // Smooth turntable platter inertia (spins up and coasts down naturally).
    float const inertiaRate = 1.f - std::exp((shouldSpin ? -5.0f : -6.5f) * safeDt);
    m_currentSpinSpeed = lerp(m_currentSpinSpeed, targetSpeed, inertiaRate);

    if (!shouldSpin && std::fabs(m_currentSpinSpeed) < 0.35f) {
        m_currentSpinSpeed = 0.f;
        if (!m_spinning) {
            this->unschedule(schedule_selector(VinylDisc::tick));
        }
        return;
    }

    float r = m_rotating->getRotation() + safeDt * m_currentSpinSpeed;
    if (r > 360.f) r -= 360.f;
    else if (r < -360.f) r += 360.f;
    m_rotating->setRotation(r);
}

void VinylDisc::setPausedAppearance(bool paused) {
    m_paused = paused;
    cocos2d::ccColor3B tint = paused
        ? cocos2d::ccColor3B{130, 130, 140}
        : cocos2d::ccColor3B{255, 255, 255};

    if (m_coverSprite) m_coverSprite->setColor(tint);

    if (m_rotating) {
        GLubyte op = paused ? 200 : 255;
        if (auto* s = m_coverSprite) s->setOpacity(op);
    }
}

void VinylDisc::setStyle(VinylStyle style) {
    if (m_style == style) return;
    m_style = style;
    setSavedStyle(style);
    reloadAppearance();
}

void VinylDisc::setAnisotropicGleam(bool enabled) {
    m_anisotropicGleam = enabled;
    buildAnisotropicSheen();
}

void VinylDisc::setDetailedGrooves(bool enabled) {
    m_detailedGrooves = enabled;
    buildGeometry();
    if (m_currentCoverTexture) {
        applyCoverTexture(m_currentCoverTexture);
    }
}

void VinylDisc::setLabelSizeRatio(float ratio) {
    m_labelRatio = std::clamp(ratio, 0.25f, 0.65f);
    buildGeometry();
    buildAnisotropicSheen();
    if (m_currentCoverTexture) {
        applyCoverTexture(m_currentCoverTexture);
    }
}

void VinylDisc::reloadAppearance() {
    m_style = getSavedStyle();
    m_anisotropicGleam = geode::Mod::get()->getSavedValue<bool>("menuMusicVinylGleam", true);
    m_detailedGrooves = geode::Mod::get()->getSavedValue<bool>("menuMusicVinylGrooves", true);
    int const labelIdx = geode::Mod::get()->getSavedValue<int>("menuMusicVinylLabelSize", 1);
    if (labelIdx == 0) m_labelRatio = 0.38f;
    else if (labelIdx == 2) m_labelRatio = 0.54f;
    else m_labelRatio = 0.46f;

    m_spinSpeed = geode::Mod::get()->getSavedValue<float>("menuMusicVinylSpeed", 40.f);

    buildGeometry();
    buildAnisotropicSheen();

    if (m_currentCoverTexture) {
        applyCoverTexture(m_currentCoverTexture);
    } else if (!m_coverPath.empty()) {
        auto path = m_coverPath;
        m_coverPath.clear();
        setCoverFromPath(path);
    }
}

VinylStyle VinylDisc::getSavedStyle() {
    int val = geode::Mod::get()->getSavedValue<int>("menuMusicVinylStyle", 0);
    val = std::clamp(val, 0, static_cast<int>(VinylStyle::Count) - 1);
    return static_cast<VinylStyle>(val);
}

void VinylDisc::setSavedStyle(VinylStyle style) {
    geode::Mod::get()->setSavedValue("menuMusicVinylStyle", static_cast<int>(style));
}

std::vector<std::string> const& VinylDisc::getStyleNames() {
    static std::vector<std::string> const names = {
        "Vinilo Clasico",
        "Disco de Oro",
        "Disco de Platino",
        "Compact Disc (CD 90s)",
        "Cyberpunk Neon",
        "Acrilico Transparente",
        "Vinilo Rubi",
        "Aurora Boreal"
    };
    return names;
}

char const* VinylDisc::getStyleDescription(VinylStyle style) {
    switch (style) {
        case VinylStyle::ClassicBlack:
            return "Vinilo negro obsidiana HD con micro-surcos de alta densidad, pistas separadas, dead-wax y buje mecanizado.";
        case VinylStyle::GoldRecord:
            return "Disco de oro VIP de 24k con acabado metalico cepillado, reflejos anisotropicos calidos y buje dorado.";
        case VinylStyle::PlatinumChrome:
            return "Disco de platino espejo con surcos de titanio, destellos azul hielo y pulido cromado.";
        case VinylStyle::RetroCD:
            return "Autentico CD-ROM de los 90s con anillo central de policarbonato y difraccion arcoiris holografica.";
        case VinylStyle::CyberpunkNeon:
            return "Estetica synthwave con vinilo violeta oscuro, micro-surcos laser cian/magenta y halo neon.";
        case VinylStyle::GlassClear:
            return "Vinilo acrilico transparente con borde Fresnel refractivo y micro-surcos esmerilados.";
        case VinylStyle::RubyMarbled:
            return "Edicion coleccionista rojo rubi con vetas marmoladas organicas, surcos granate y buje de laton.";
        case VinylStyle::AuroraCosmic:
            return "Resina cosmica indigo con cortinas boreales esmeralda/cian, destellos estelares y brillo polar.";
        default:
            return "";
    }
}

void VinylDisc::reloadAllInstances() {
    auto copy = s_activeDiscs;
    for (auto* disc : copy) {
        if (disc) disc->reloadAppearance();
    }
}

} // namespace paimon::menumusic
