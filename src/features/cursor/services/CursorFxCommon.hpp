#pragma once

#include <Geode/Geode.hpp>
#include <vector>

namespace paimon::cursorfx {

constexpr float kPi = 3.14159265358979323846f;

// Particle shapes; order is the texture-cache index.
enum FxTex : int {
    TexDot = 0,
    TexGlow,
    TexSpark,
    TexRing,
    TexFlake,
    TexHeart,
    TexSquare,
    TexPuff,
    TexStar,
    TexNote,
    TexCoin,
    TexSplat,
    TexPetal,
    TexDrop,
    TexCount
};

cocos2d::CCTexture2D* fxTexture(int kind);
// Release while the GL context is alive.
void releaseFxTextures();
// Abandon during shutdown; leaking avoids touching destroyed GL state.
void abandonFxTextures();

// Private RNG; do not disturb GD gameplay randomness.
float frand();
float frand(float a, float b);
float hashNoise(unsigned int seed, int i);

cocos2d::ccColor3B hsv(float h, float s, float v);
cocos2d::ccColor3B mixColor(cocos2d::ccColor3B a, cocos2d::ccColor3B b, float t);
// Shared color-mode switch (trail + click); speedMix is cursor speed or hold progress.
enum class TrailColorMode : int;
cocos2d::ccColor3B resolveFxColor(TrailColorMode mode, cocos2d::ccColor3B c1, cocos2d::ccColor3B c2,
                                  float t, float rnd, float speedMix, float time, float hueSpeed);

// Oldest-slot pools: a dead slot wins, else the furthest along.
template <typename P, typename AgeFn>
P* acquireOldest(std::vector<P>& pool, AgeFn age) {
    P* oldest = nullptr;
    float worst = -1.f;
    for (auto& p : pool) {
        if (!p.alive) return &p;
        float t = age(p);
        if (t > worst) { worst = t; oldest = &p; }
    }
    return oldest;
}
// Premultiplied blend requires RGB multiplied by alpha.
cocos2d::ccColor4F pma(cocos2d::ccColor3B c, float a);

// Vertex-capacity batch; unbound-VBO client arrays avoid draw-hook conflicts.
class FxDrawBatch : public cocos2d::CCNode {
public:
    static FxDrawBatch* create();

    bool init() override;
    void clear() { m_verts.clear(); }
    void setAdditive(bool additive);

    void quad(cocos2d::CCPoint const& a, cocos2d::CCPoint const& b,
              cocos2d::CCPoint const& c, cocos2d::CCPoint const& d,
              cocos2d::ccColor4F const& color);
    void circle(cocos2d::CCPoint const& center, float radius,
                cocos2d::ccColor4F const& color, int segments = 20);
    void thickLine(cocos2d::CCPoint const& p1, cocos2d::CCPoint const& p2,
                   float thickness, cocos2d::ccColor4F const& color);
    void ring(cocos2d::CCPoint const& center, float radius, float thickness,
              cocos2d::ccColor4F const& color, int segments = 40);

    void draw() override;

private:
    struct FxVert {
        cocos2d::ccVertex2F pos;
        cocos2d::ccColor4B  color;
    };

    std::vector<FxVert> m_verts;
    cocos2d::ccBlendFunc m_blend{GL_ONE, GL_ONE_MINUS_SRC_ALPHA};
};

}
