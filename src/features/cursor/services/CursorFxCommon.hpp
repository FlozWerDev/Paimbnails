#pragma once

// Shared cursor-effect textures, geometry batch, color, and random helpers.

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
// Seeded noise is deterministic.
float hashNoise(unsigned int seed, int i);

cocos2d::ccColor3B hsv(float h, float s, float v);
cocos2d::ccColor3B mixColor(cocos2d::ccColor3B a, cocos2d::ccColor3B b, float t);
// Premultiplied blend requires RGB multiplied by alpha.
cocos2d::ccColor4F pma(cocos2d::ccColor3B c, float a);

// Untextured batch reuses vertex capacity; client arrays with an unbound VBO
// avoid conflicts with other draw hooks.
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
    // Hollow ring centered at radius.
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
