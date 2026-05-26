#include "VinylDisc.hpp"
#include "../../../../utils/PaimonDrawNode.hpp"

using namespace cocos2d;

namespace paimon::menumusic {

// ── Helpers: dibujar circulos REALES (no drawDot, que es un quad) ──
//
// CCDrawNode::drawDot() produce un quad (2 triangulos) con texCoords
// (-1,-1)..(1,1). Sin un shader especifico que los convierta en un
// circulo, visualmente es un CUADRADO. Por eso el "disco" se veia
// cuadrado. Aqui usamos poligonos con muchos segmentos para dibujar
// circulos de verdad.

static void drawFilledCircle(PaimonDrawNode* node, CCPoint center, float radius, ccColor4F color, int segments = 64) {
    if (!node || radius <= 0.f) return;
    std::vector<CCPoint> verts;
    verts.reserve(segments);
    constexpr float kTau = 6.28318530717958647692f;
    for (int i = 0; i < segments; ++i) {
        float a = kTau * static_cast<float>(i) / static_cast<float>(segments);
        verts.emplace_back(center.x + radius * cosf(a), center.y + radius * sinf(a));
    }
    node->drawPolygon(verts.data(),
        static_cast<unsigned int>(verts.size()),
        color, 0.f, ccc4f(0, 0, 0, 0));
}

static void drawRing(PaimonDrawNode* node, CCPoint center, float outer, float inner, ccColor4F color, int segments = 72) {
    if (!node || outer <= inner || inner < 0.f) return;
    std::vector<CCPoint> verts;
    verts.reserve(segments * 2 + 2);
    constexpr float kTau = 6.28318530717958647692f;
    // Generar un anillo como un strip de triangulos envuelto en un poligono convexo
    // sencillo: duplicar cada segmento exterior e interior alternados.
    // La forma mas estable es usar drawSegment como circunferencia gruesa:
    float thickness = (outer - inner);
    float radiusMid = (outer + inner) * 0.5f;
    for (int i = 0; i < segments; ++i) {
        float a0 = kTau * static_cast<float>(i) / static_cast<float>(segments);
        float a1 = kTau * static_cast<float>(i + 1) / static_cast<float>(segments);
        CCPoint p0{center.x + radiusMid * cosf(a0), center.y + radiusMid * sinf(a0)};
        CCPoint p1{center.x + radiusMid * cosf(a1), center.y + radiusMid * sinf(a1)};
        node->drawSegment(p0, p1, thickness * 0.5f, color);
    }
}

VinylDisc* VinylDisc::create(float radius) {
    auto ret = new VinylDisc();
    if (ret && ret->init(radius)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VinylDisc::init(float radius) {
    if (!CCNode::init()) return false;

    m_radius = std::max(24.f, radius);
    const float size = m_radius * 2.f;
    this->setContentSize({size, size});
    this->setAnchorPoint({0.5f, 0.5f});

    m_rotating = CCNode::create();
    m_rotating->setAnchorPoint({0.5f, 0.5f});
    m_rotating->setContentSize({size, size});
    m_rotating->setPosition({m_radius, m_radius});
    this->addChild(m_rotating, 1);

    // ── Fondo de vinilo (DISCO REAL, no un quad drawDot) ──
    //
    // Ahora si: poligono de 64 lados = circulo suave. Encima dibujamos
    // anillos concentricos (surcos) con drawSegment que es un trazo
    // curvo de verdad, no un rectangulo.
    auto draw = PaimonDrawNode::create();
    if (draw) {
        CCPoint center = {m_radius, m_radius};

        // disco base negro
        drawFilledCircle(draw, center, m_radius, {0.05f, 0.05f, 0.07f, 1.f}, 72);

        // anillo exterior (borde sutil que da volumen al vinilo)
        drawRing(draw, center, m_radius, m_radius * 0.97f, {0.18f, 0.18f, 0.22f, 1.f}, 72);

        // surcos internos
        for (int i = 0; i < 6; ++i) {
            float r = m_radius * (0.88f - i * 0.08f);
            if (r <= m_radius * 0.18f) break;
            drawRing(draw, center, r + 0.6f, r - 0.2f, {0.14f, 0.14f, 0.17f, 1.f}, 64);
        }
        m_rotating->addChild(draw, 0);
    }

    // ── Mascara circular para la portada ──
    //
    // Stencil con poligono circular de 64 lados. El radio del agujero
    // cubre ~55% del disco para que queden visibles los surcos exteriores.
    auto stencil = PaimonDrawNode::create();
    drawFilledCircle(stencil, {m_radius, m_radius}, m_radius * 0.55f, {1, 1, 1, 1}, 64);

    m_coverClip = CCClippingNode::create();
    m_coverClip->setStencil(stencil);
    m_coverClip->setAlphaThreshold(0.1f);
    m_coverClip->setContentSize({size, size});
    m_coverClip->setAnchorPoint({0.5f, 0.5f});
    m_coverClip->setPosition({m_radius, m_radius});
    m_rotating->addChild(m_coverClip, 2);

    // ── Pin central (punto blanco) ──
    //
    // Circulo pequenio en el centro del disco. Lo dibujamos directamente
    // sobre el rotating node para que gire con el resto.
    auto centerDot = PaimonDrawNode::create();
    if (centerDot) {
        drawFilledCircle(centerDot, {m_radius, m_radius}, m_radius * 0.09f, {0.92f, 0.92f, 0.96f, 1.f}, 32);
        // Pequenio huequito para simular el agujero del vinilo
        drawFilledCircle(centerDot, {m_radius, m_radius}, m_radius * 0.03f, {0.06f, 0.06f, 0.09f, 1.f}, 16);
        m_rotating->addChild(centerDot, 4);
    }
    m_centerDot = nullptr; // ya no usamos el sprite separado

    // ── Highlight estatico (no gira) para simular reflejo del vinilo ──
    //
    // Va en el nodo padre (no rotatorio) y cubre un arco superior del
    // disco. Usa drawSegment con color semitransparente.
    auto highlight = PaimonDrawNode::create();
    if (highlight) {
        constexpr float kPi = 3.14159265358979323846f;
        const int segs = 32;
        for (int i = 0; i < segs; ++i) {
            float t0 = static_cast<float>(i) / segs;
            float t1 = static_cast<float>(i + 1) / segs;
            float a0 = kPi * 0.15f + kPi * 0.35f * t0;
            float a1 = kPi * 0.15f + kPi * 0.35f * t1;
            float r = m_radius * 0.88f;
            CCPoint p0{m_radius + r * cosf(a0), m_radius + r * sinf(a0)};
            CCPoint p1{m_radius + r * cosf(a1), m_radius + r * sinf(a1)};
            highlight->drawSegment(p0, p1, 1.5f, {1.f, 1.f, 1.f, 0.18f});
        }
        this->addChild(highlight, 3);
    }

    return true;
}

void VinylDisc::setCoverFromPath(const std::string& absolutePath) {
    if (!m_coverClip) return;

    // Reemplazar cover anterior.
    if (m_coverSprite) {
        m_coverSprite->removeFromParent();
        m_coverSprite = nullptr;
    }
    if (absolutePath.empty()) return;

    auto* tex = CCTextureCache::sharedTextureCache()->addImage(absolutePath.c_str(), false);
    if (!tex) return;

    m_coverSprite = CCSprite::createWithTexture(tex);
    if (!m_coverSprite) return;

    const CCSize texSize = m_coverSprite->getContentSize();
    // Escalar para que el LADO CORTO cubra el diametro del agujero circular.
    // De este modo la imagen llena por completo el stencil (se recorta en el
    // lado largo). Usamos max para llenar el circulo (parecido a background-size: cover).
    const float holeDiameter = m_radius * 2.f * 0.55f + 2.f; // ligero padding
    const float minSide = std::min(texSize.width, texSize.height);
    if (minSide <= 0.f) return;
    const float scale = holeDiameter / minSide;
    m_coverSprite->setScale(scale);
    m_coverSprite->setAnchorPoint({0.5f, 0.5f});
    m_coverSprite->setPosition({m_radius, m_radius});
    m_coverClip->addChild(m_coverSprite, 0);
}

void VinylDisc::clearCover() {
    if (m_coverSprite) {
        m_coverSprite->removeFromParent();
        m_coverSprite = nullptr;
    }
}

void VinylDisc::startSpinning() {
    if (m_spinning) return;
    m_spinning = true;
    this->schedule(schedule_selector(VinylDisc::tick));
}

void VinylDisc::stopSpinning() {
    if (!m_spinning) return;
    m_spinning = false;
    this->unschedule(schedule_selector(VinylDisc::tick));
}

void VinylDisc::tick(float dt) {
    if (!m_rotating || !m_spinning) return;
    float r = m_rotating->getRotation() + dt * m_spinSpeed;
    if (r > 360.f) r -= 360.f;
    else if (r < -360.f) r += 360.f;
    m_rotating->setRotation(r);
}

void VinylDisc::setPausedAppearance(bool paused) {
    // Grisar la portada (canal RGB a 128) y reducir la opacidad del rotating
    // cuando esta pausado; restaurar a 255/full color cuando reanuda.
    cocos2d::ccColor3B tint = paused
        ? cocos2d::ccColor3B{130, 130, 140}
        : cocos2d::ccColor3B{255, 255, 255};

    if (m_coverSprite) m_coverSprite->setColor(tint);

    // Ademas bajamos un poco la opacidad del rotating para reforzar el
    // estado "off". CCNode no tiene setOpacity global, asi que tintamos
    // los hijos principales.
    if (m_rotating) {
        GLubyte op = paused ? 200 : 255;
        if (auto* s = m_coverSprite) s->setOpacity(op);
    }
}

} // namespace paimon::menumusic
