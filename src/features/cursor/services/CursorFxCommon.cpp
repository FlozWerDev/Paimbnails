#include "CursorFxCommon.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/shaders/CCShaderCache.h>
#include <Geode/cocos/shaders/CCGLProgram.h>
#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::cursorfx {

namespace {

std::mt19937& rng() {
    static std::mt19937 s_rng{std::random_device{}()};
    return s_rng;
}

ccColor4B toByte(ccColor4F const& c) {
    auto to255 = [](float v) {
        return static_cast<GLubyte>(std::clamp(v, 0.f, 1.f) * 255.f);
    };
    return {to255(c.r), to255(c.g), to255(c.b), to255(c.a)};
}

// Alfa de la textura en coordenadas normalizadas x,y en [-1, 1].
float texAlpha(int kind, float x, float y) {
    float d = std::sqrt(x * x + y * y);
    switch (kind) {
        case TexDot: {
            // Punto solido con el borde apenas difuminado.
            return std::clamp((1.f - d) * 3.2f, 0.f, 1.f);
        }
        case TexGlow: {
            // Halo ancho y suave, sin borde: fuego y cabezas brillantes.
            float a = std::max(0.f, 1.f - d);
            return std::min(1.f, std::pow(a, 2.0f) * 0.85f + std::pow(a, 6.f) * 0.5f);
        }
        case TexSpark: {
            float ax = std::fabs(x), ay = std::fabs(y);
            float core = std::pow(std::max(0.f, 1.f - d), 7.f);
            float thin = std::max(0.f, 1.f - std::min(ax, ay) * 9.f);
            float arms = std::pow(std::max(0.f, 1.f - d), 1.3f) * thin;
            return std::min(1.f, core + arms * 0.85f);
        }
        case TexRing: {
            float ring = std::max(0.f, 1.f - std::fabs(d - 0.80f) / 0.17f);
            ring = ring * ring * 0.85f;
            float hx = x + 0.36f, hy = y - 0.36f;
            float hl = std::max(0.f, 1.f - std::sqrt(hx * hx + hy * hy) / 0.26f);
            float fill = std::max(0.f, 1.f - d) * 0.10f;
            return std::min(1.f, ring + hl * hl * 0.9f + fill);
        }
        case TexFlake: {
            if (d > 1.f) return 0.f;
            float ang = std::atan2(y, x);
            // 6 brazos + ramitas laterales a dos alturas + nucleo.
            float arm = std::pow(std::max(0.f, std::cos(6.f * ang)), 5.f);
            float arms = arm * std::clamp((1.f - d) * 2.6f, 0.f, 1.f);
            float ticks = 0.f;
            for (float rr : {0.36f, 0.62f}) {
                float band = std::max(0.f, 1.f - std::fabs(d - rr) / 0.13f);
                ticks = std::max(ticks, band * std::pow(std::max(0.f, std::cos(6.f * ang)), 0.5f));
            }
            float core = std::clamp((0.20f - d) * 9.f, 0.f, 1.f);
            return std::clamp(arms + ticks * 0.5f + core, 0.f, 1.f);
        }
        case TexHeart: {
            // (x^2 + y^2 - 1)^3 - x^2 * y^3 <= 0, con y invertida (punta abajo)
            float hx = x * 1.28f, hy = -y * 1.28f;
            float k = hx * hx + hy * hy - 1.f;
            float f = k * k * k - hx * hx * hy * hy * hy;
            return f <= 0.f ? 1.f : 0.f;
        }
        case TexSquare: {
            float m = std::min(1.f - std::fabs(x), 1.f - std::fabs(y));
            return std::clamp(m * 5.f, 0.f, 1.f);
        }
        case TexPuff: {
            // Bola difusa con el contorno apenas ondulado: nube de humo.
            float ang = std::atan2(y, x);
            float lobes = 0.90f + 0.10f * std::sin(ang * 3.f + 1.1f) + 0.05f * std::sin(ang * 5.f - 0.4f);
            float a = std::max(0.f, 1.f - d / lobes);
            return std::pow(a, 1.6f) * 0.8f;
        }
        case TexStar: {
            // Estrella de 5 puntas: el borde es la recta entre la punta
            // (r = 1) y el valle (r = 0.45) resuelta en polares.
            if (d > 1.f) return 0.f;
            float ang = std::atan2(y, x) - kPi * 0.5f;
            float k = 2.f * kPi / 5.f;
            float a = ang - k * std::floor(ang / k) - k * 0.5f;
            float th = std::fabs(a);
            float half = k * 0.5f;
            float denom = std::sin(th) + 0.45f * std::sin(half - th);
            float edge = denom > 0.0001f ? (0.45f * std::sin(half)) / denom : 1.f;
            return std::clamp((edge - d) * 11.f, 0.f, 1.f);
        }
        case TexNote: {
            // Corchea: cabeza ovalada inclinada + palito + banderin curvo.
            float hx = x + 0.34f, hy = y + 0.46f;
            constexpr float c = 0.906f, s = -0.423f;
            float rx = hx * c - hy * s, ry = hx * s + hy * c;
            float head = (rx * rx) / (0.40f * 0.40f) + (ry * ry) / (0.29f * 0.29f) <= 1.f ? 1.f : 0.f;
            float stem = (std::fabs(x - 0.50f) < 0.095f && y > -0.50f && y < 0.88f) ? 1.f : 0.f;
            float flag = 0.f;
            if (y > 0.12f && y < 0.88f) {
                float t = (0.88f - y) / 0.76f;
                float fx = 0.50f + 0.40f * t * t;
                if (x > fx - 0.02f && x < fx + 0.20f) flag = 1.f;
            }
            return std::max(head, std::max(stem, flag));
        }
        case TexCoin: {
            // Disco con el canto marcado (menos alfa) y un brillo arriba.
            float disc = std::clamp((0.95f - d) * 8.f, 0.f, 1.f);
            float groove = 1.f - 0.55f * std::max(0.f, 1.f - std::fabs(d - 0.66f) / 0.10f);
            float hx = x + 0.28f, hy = y - 0.30f;
            float hl = std::max(0.f, 1.f - std::sqrt(hx * hx + hy * hy) / 0.24f);
            return std::min(1.f, disc * groove + hl * hl * 0.55f * disc);
        }
        case TexSplat: {
            // Mancha con lobulos irregulares y tres gotitas sueltas.
            float ang = std::atan2(y, x);
            float lobes = 0.62f + 0.16f * std::sin(ang * 3.f + 0.7f)
                        + 0.10f * std::sin(ang * 5.f - 1.3f)
                        + 0.06f * std::sin(ang * 8.f + 2.1f);
            float a = std::clamp((lobes - d) * 9.f, 0.f, 1.f);
            constexpr float sa[3] = {0.9f, 2.6f, 4.7f};
            constexpr float sr[3] = {0.80f, 0.86f, 0.74f};
            constexpr float ss[3] = {0.13f, 0.10f, 0.15f};
            for (int i = 0; i < 3; ++i) {
                float dx = x - std::cos(sa[i]) * sr[i];
                float dy = y - std::sin(sa[i]) * sr[i];
                a = std::max(a, std::clamp((ss[i] - std::sqrt(dx * dx + dy * dy)) * 14.f, 0.f, 1.f));
            }
            return a;
        }
        case TexPetal: {
            // Hoja: ancho maximo al medio, puntas arriba y abajo.
            float yy = (y + 1.f) * 0.5f;
            if (yy <= 0.f || yy >= 1.f) return 0.f;
            float w = 0.62f * std::sin(std::pow(yy, 0.75f) * kPi);
            return std::clamp((w - std::fabs(x)) * 9.f, 0.f, 1.f);
        }
        case TexDrop: {
            // Gota: semicirculo abajo que se afina hasta la punta de arriba.
            float w = y < 0.f
                ? std::sqrt(std::max(0.f, 0.62f * 0.62f - y * y))
                : 0.62f * std::pow(std::max(0.f, 1.f - y / 0.95f), 0.85f);
            return std::clamp((w - std::fabs(x)) * 9.f, 0.f, 1.f);
        }
        default: return 0.f;
    }
}

std::array<geode::Ref<CCTexture2D>, TexCount>& texCache() {
    static std::array<geode::Ref<CCTexture2D>, TexCount> s_cache{};
    return s_cache;
}

} // namespace

CCTexture2D* fxTexture(int kind) {
    if (kind < 0 || kind >= TexCount) return nullptr;
    auto& slot = texCache()[static_cast<size_t>(kind)];
    if (slot) return slot.data();

    constexpr int kSize = 64;
    constexpr int kSuper = 3;   // 3x3 supersampling: bordes sin escalones
    std::vector<uint8_t> pixels(kSize * kSize * 4, 255);
    for (int py = 0; py < kSize; ++py) {
        for (int px = 0; px < kSize; ++px) {
            float acc = 0.f;
            for (int sy = 0; sy < kSuper; ++sy) {
                for (int sx = 0; sx < kSuper; ++sx) {
                    float fx = (px + (sx + 0.5f) / kSuper) / kSize * 2.f - 1.f;
                    float fy = (py + (sy + 0.5f) / kSuper) / kSize * 2.f - 1.f;
                    acc += std::clamp(texAlpha(kind, fx, fy), 0.f, 1.f);
                }
            }
            acc /= static_cast<float>(kSuper * kSuper);
            pixels[static_cast<size_t>((py * kSize + px) * 4 + 3)] =
                static_cast<uint8_t>(acc * 255.f);
        }
    }

    auto* tex = new CCTexture2D();
    if (tex->initWithData(pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
                          kSize, kSize, CCSizeMake(kSize, kSize))) {
        // adopt: initWithData deja refcount=1, sin adopt Ref retendria de nuevo.
        slot = geode::Ref<CCTexture2D>::adopt(tex);
        return slot.data();
    }
    tex->release();
    return nullptr;
}

void releaseFxTextures() {
    for (auto& slot : texCache()) slot = nullptr;
}

void abandonFxTextures() {
    for (auto& slot : texCache()) (void)slot.take();
}

float frand() {
    return static_cast<float>(rng()() & 0xFFFFFFu) / 16777215.f;
}

float frand(float a, float b) { return a + (b - a) * frand(); }

float hashNoise(unsigned int seed, int i) {
    unsigned int h = seed * 374761393u + static_cast<unsigned int>(i) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFu) / 32767.5f - 1.f;
}

ccColor3B hsv(float h, float s, float v) {
    h = h - std::floor(h);
    float r = 0.f, g = 0.f, b = 0.f;
    float i = std::floor(h * 6.f);
    float f = h * 6.f - i;
    float p = v * (1.f - s);
    float q = v * (1.f - f * s);
    float t = v * (1.f - (1.f - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return ccc3(static_cast<GLubyte>(r * 255.f),
                static_cast<GLubyte>(g * 255.f),
                static_cast<GLubyte>(b * 255.f));
}

ccColor3B mixColor(ccColor3B a, ccColor3B b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return ccc3(static_cast<GLubyte>(a.r + (b.r - a.r) * t),
                static_cast<GLubyte>(a.g + (b.g - a.g) * t),
                static_cast<GLubyte>(a.b + (b.b - a.b) * t));
}

ccColor4F pma(ccColor3B c, float a) {
    a = std::clamp(a, 0.f, 1.f);
    return ccc4f(c.r / 255.f * a, c.g / 255.f * a, c.b / 255.f * a, a);
}


FxDrawBatch* FxDrawBatch::create() {
    auto* node = new FxDrawBatch();
    if (node->init()) {
        node->autorelease();
        return node;
    }
    delete node;
    return nullptr;
}

bool FxDrawBatch::init() {
    if (!CCNode::init()) return false;
    // Sin textura: posicion + color por vertice y listo.
    auto* cache = CCShaderCache::sharedShaderCache();
    if (!cache) return false;
    auto* program = cache->programForKey(kCCShader_PositionColor);
    if (!program) return false;
    setShaderProgram(program);
    m_verts.reserve(2048);
    return true;
}

void FxDrawBatch::setAdditive(bool additive) {
    // Los colores llegan premultiplicados por su alfa.
    m_blend = additive ? ccBlendFunc{GL_ONE, GL_ONE}
                       : ccBlendFunc{GL_ONE, GL_ONE_MINUS_SRC_ALPHA};
}

void FxDrawBatch::quad(CCPoint const& a, CCPoint const& b, CCPoint const& c,
                       CCPoint const& d, ccColor4F const& color) {
    ccColor4B col = toByte(color);
    m_verts.push_back({{a.x, a.y}, col});
    m_verts.push_back({{b.x, b.y}, col});
    m_verts.push_back({{c.x, c.y}, col});
    m_verts.push_back({{a.x, a.y}, col});
    m_verts.push_back({{c.x, c.y}, col});
    m_verts.push_back({{d.x, d.y}, col});
}

void FxDrawBatch::circle(CCPoint const& center, float radius, ccColor4F const& color,
                         int segments) {
    if (radius <= 0.05f || segments < 3) return;
    ccColor4B col = toByte(color);
    float step = 2.f * kPi / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        m_verts.push_back({{center.x, center.y}, col});
        m_verts.push_back({{center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius}, col});
        m_verts.push_back({{center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius}, col});
    }
}

void FxDrawBatch::thickLine(CCPoint const& p1, CCPoint const& p2, float thickness,
                            ccColor4F const& color) {
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float h = std::max(0.25f, thickness * 0.5f);
    float nx = -dy / len * h, ny = dx / len * h;
    quad(ccp(p1.x + nx, p1.y + ny), ccp(p2.x + nx, p2.y + ny),
         ccp(p2.x - nx, p2.y - ny), ccp(p1.x - nx, p1.y - ny), color);
}

void FxDrawBatch::ring(CCPoint const& center, float radius, float thickness,
                       ccColor4F const& color, int segments) {
    if (radius <= 0.05f || segments < 3) return;
    float inner = std::max(0.f, radius - thickness * 0.5f);
    float outer = radius + thickness * 0.5f;
    float step = 2.f * kPi / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        quad(ccp(center.x + c0 * inner, center.y + s0 * inner),
             ccp(center.x + c1 * inner, center.y + s1 * inner),
             ccp(center.x + c1 * outer, center.y + s1 * outer),
             ccp(center.x + c0 * outer, center.y + s0 * outer), color);
    }
}

void FxDrawBatch::draw() {
    if (m_verts.empty()) return;

    CC_NODE_DRAW_SETUP();
    ccGLBlendFunc(m_blend.src, m_blend.dst);
    // Arrays del cliente: sin VBO atado, nada externo puede pisarlos.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_Color);
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE,
                          sizeof(FxVert), &m_verts[0].pos);
    glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(FxVert), &m_verts[0].color);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts.size()));
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
    CC_INCREMENT_GL_DRAWS(1);
#endif
}

} // namespace paimon::cursorfx
