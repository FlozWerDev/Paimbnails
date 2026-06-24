#include "CoverHero.hpp"
#include "VinylDisc.hpp"

#include "../../../../utils/PaimonDrawNode.hpp"
#include "../../../../utils/SpriteHelper.hpp"

#include "../../services/MenuMusicCoverLog.hpp"
#include <filesystem>

#include <cmath>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::menumusic {

CoverHero* CoverHero::create(const CCSize& size, float skew) {
    auto ret = new CoverHero();
    if (ret && ret->init(size, skew)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CoverHero::init(const CCSize& size, float skew) {
    if (!CCNode::init()) return false;

    m_size = size;
    m_skew = std::max(0.f, skew);

    // Usamos el tamano completo como contentSize; el stencil hace la diagonal.
    this->setContentSize(m_size);
    this->setAnchorPoint({0.f, 0.5f});

    // Stencil diagonal (parallelogramo tipo LevelCell)
    //
    // Vertices: (0,0) → (W,0) → (W-skew,H) → (0,H). El corte diagonal
    // va solo en el borde SUPERIOR-DERECHO. Las esquinas izquierdas
    // se dejan rectas aqui porque el contenedor padre (content clipper
    // del popup) ya redondea las esquinas externas; redondear aqui
    // encima produciria un doble recorte visible.
    auto stencil = PaimonDrawNode::create();
    if (stencil) {
        CCPoint poly[4] = {
            ccp(0.f,           0.f),
            ccp(m_size.width,  0.f),
            ccp(m_size.width - m_skew, m_size.height),
            ccp(0.f,           m_size.height)
        };
        ccColor4F white = {1.f, 1.f, 1.f, 1.f};
        stencil->drawPolygon(poly, 4, white, 0.f, white);
    }

    m_clip = CCClippingNode::create();
    if (!m_clip) return false;
    m_clip->setStencil(stencil);
    m_clip->setAlphaThreshold(0.05f);
    m_clip->setContentSize(m_size);
    m_clip->setAnchorPoint({0.f, 0.f});
    m_clip->setPosition({0.f, 0.f});
    this->addChild(m_clip, 0);

    // Fallback (cuando no hay cover)
    //
    // Dibujamos un degradado radial oscuro + un icono de nota musical para
    // que el hero nunca quede en blanco.
    m_fallback = CCNode::create();
    if (m_fallback) {
        m_fallback->setContentSize(m_size);
        m_fallback->setAnchorPoint({0.f, 0.f});

        auto bg = PaimonDrawNode::create();
        if (bg) {
            // Fondo con dos paradas de color (simulamos un degradado con dos
            // poligonos superpuestos con alphas distintos).
            CCPoint full[4] = {
                ccp(0.f, 0.f), ccp(m_size.width, 0.f),
                ccp(m_size.width, m_size.height), ccp(0.f, m_size.height)
            };
            bg->drawPolygon(full, 4, ccc4f(0.08f, 0.08f, 0.14f, 1.f),
                0.f, ccc4f(0, 0, 0, 0));
            m_fallback->addChild(bg, 0);
        }

        // Nota musical central.
        auto note = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
        if (note) {
            note->setScale(1.4f);
            note->setColor({180, 180, 210});
            note->setOpacity(170);
            note->setAnchorPoint({0.5f, 0.5f});
            note->setPosition({m_size.width * 0.45f, m_size.height * 0.5f});
            m_fallback->addChild(note, 1);
        }

        m_clip->addChild(m_fallback, 0);
    }

    // Overlay oscuro de legibilidad
    //
    // Un degradado izq-a-derecha que oscurece mas el lado derecho para que
    // el track-label (que va a la derecha del hero) destaque contra la
    // parte oscurecida.
    m_gradient = PaimonDrawNode::create();
    if (m_gradient) {
        auto* d = static_cast<PaimonDrawNode*>(m_gradient);
        // Dos poligonos solapados con alphas distintos. Como PaimonDrawNode
        // no acepta colores por vertice para un solo poligono de manera
        // directa, usamos 3 bandas apiladas.
        const int bands = 8;
        for (int i = 0; i < bands; ++i) {
            float t0 = static_cast<float>(i) / bands;
            float t1 = static_cast<float>(i + 1) / bands;
            float x0 = m_size.width * t0;
            float x1 = m_size.width * t1;
            // skew varia de 0 a m_skew segun la altura — para un stencil
            // vertical aprox. basta con no superar el ancho util.
            float alpha = 0.05f + t0 * 0.55f; // 5% a la izquierda, 60% a la derecha
            CCPoint q[4] = {
                ccp(x0, 0.f),
                ccp(x1, 0.f),
                ccp(x1, m_size.height),
                ccp(x0, m_size.height)
            };
            d->drawPolygon(q, 4, ccc4f(0.f, 0.f, 0.05f, alpha), 0.f, ccc4f(0, 0, 0, 0));
        }
        // Franja extra superior para que el titulo del popup (MENU MUSIC)
        // quede legible sobre la portada.
        const int topBands = 5;
        const float topBandH = 36.f;
        for (int i = 0; i < topBands; ++i) {
            float t0 = static_cast<float>(i) / topBands;
            float t1 = static_cast<float>(i + 1) / topBands;
            float y0 = m_size.height - topBandH + topBandH * t0;
            float y1 = m_size.height - topBandH + topBandH * t1;
            // mas opaco cerca del top, se desvanece hacia abajo.
            float alpha = 0.55f * (t1);
            CCPoint q[4] = {
                ccp(0.f,          y0),
                ccp(m_size.width, y0),
                ccp(m_size.width, y1),
                ccp(0.f,          y1)
            };
            d->drawPolygon(q, 4, ccc4f(0.f, 0.f, 0.02f, alpha), 0.f, ccc4f(0, 0, 0, 0));
        }
        m_gradient->setContentSize(m_size);
        m_clip->addChild(m_gradient, 2);
    }

    // Borde diagonal: solo sombra sutil interior, sin highlight blanco
    //
    // El highlight blanco quedaba muy marcado y rompia el look limpio. Se
    // deja solo una linea oscura finisima para dar sensacion de borde sin
    // destacar como una division visible.
    m_borderGloss = PaimonDrawNode::create();
    if (m_borderGloss) {
        CCPoint s0 = ccp(m_size.width - 1.f,          0.f);
        CCPoint s1 = ccp(m_size.width - m_skew - 1.f, m_size.height);
        m_borderGloss->drawSegment(s0, s1, 0.7f, ccc4f(0.f, 0.f, 0.f, 0.3f));
        this->addChild(m_borderGloss, 3);
    }

    // Disco pequeno decorativo (vinilo)
    //
    // Posicionado en la esquina inferior del hero, sobresaliendo un poco del
    // corte diagonal. Gira cuando el player esta reproduciendo.
    // Disco pequeno decorativo (vinilo) — tambien es el boton play/pause
    //
    // Lo situamos dentro del area del hero, un poco hacia la derecha, a
    // media altura. No va dentro del clip (para que el disco no se recorte
    // con la diagonal). El tamano se elige relativo al hero para que
    // responda a redimensionamientos.
    const float discRadius = std::min(m_size.width * 0.28f, m_size.height * 0.30f);
    m_smallDisc = VinylDisc::create(discRadius);
    if (m_smallDisc) {
        m_smallDisc->setAnchorPoint({0.5f, 0.5f});
        // Centrado horizontal dentro del hero (ligeramente a la izquierda
        // del borde diagonal para que el disco no sobresalga demasiado).
        m_smallDisc->setPosition({
            m_size.width * 0.44f,
            m_size.height * 0.50f
        });
        this->addChild(m_smallDisc, 4);
    }

    return true;
}

void CoverHero::setCoversFromPaths(std::vector<std::string> const& absolutePaths) {
    this->unschedule(schedule_selector(CoverHero::tickCarousel));
    m_coverPaths.clear();
    m_carouselIndex = 0;
    m_carouselElapsed = 0.f;

    for (auto const& path : absolutePaths) {
        if (!path.empty()) m_coverPaths.push_back(path);
    }

    if (m_coverPaths.empty()) {
        coverlog::info("[MenuMusicCover] CoverHero: no cover paths, showing fallback");
        clearCover();
        return;
    }

    coverlog::info("[MenuMusicCover] CoverHero: loading {} cover(s), first='{}'",
        m_coverPaths.size(), m_coverPaths.front());

    if (m_coverPaths.size() == 1) {
        setCoverFromPath(m_coverPaths.front());
        return;
    }

    showCoverAtIndex(0);
    this->schedule(schedule_selector(CoverHero::tickCarousel), 1.f);
}

void CoverHero::showCoverAtIndex(std::size_t index) {
    if (m_coverPaths.empty()) return;
    m_carouselIndex = index % m_coverPaths.size();
    setCoverFromPath(m_coverPaths[m_carouselIndex]);
}

void CoverHero::tickCarousel(float dt) {
    if (m_coverPaths.size() <= 1) return;
    m_carouselElapsed += dt;
    if (m_carouselElapsed < kCarouselInterval) return;
    m_carouselElapsed = 0.f;
    showCoverAtIndex(m_carouselIndex + 1);
}

std::string CoverHero::getCurrentCoverPath() const {
    if (m_coverPaths.empty()) return {};
    return m_coverPaths[m_carouselIndex % m_coverPaths.size()];
}

void CoverHero::setCoverFromPath(const std::string& absolutePath) {
    // Limpiar cover anterior.
    if (m_coverSprite) {
        m_coverSprite->removeFromParent();
        m_coverSprite = nullptr;
    }

    if (absolutePath.empty()) {
        if (m_fallback) m_fallback->setVisible(true);
        if (m_smallDisc) m_smallDisc->setCoverFromPath("");
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(absolutePath, ec) || ec) {
        coverlog::warn("[MenuMusicCover] CoverHero: file not found '{}'", absolutePath);
        if (m_fallback) m_fallback->setVisible(true);
        return;
    }

    auto* tex = CCTextureCache::sharedTextureCache()->addImage(absolutePath.c_str(), false);
    if (!tex) {
        coverlog::warn("[MenuMusicCover] CoverHero: CCTextureCache failed '{}'", absolutePath);
        if (m_fallback) m_fallback->setVisible(true);
        return;
    }

    m_coverSprite = CCSprite::createWithTexture(tex);
    if (!m_coverSprite) {
        coverlog::warn("[MenuMusicCover] CoverHero: sprite create failed '{}'", absolutePath);
        return;
    }

    // Modo cover: el lado mas corto llena el contenedor, el mas largo se
    // recorta por el stencil.
    const CCSize ts = m_coverSprite->getContentSize();
    const float sx = m_size.width / ts.width;
    const float sy = m_size.height / ts.height;
    const float scale = std::max(sx, sy);
    m_coverSprite->setScale(scale);
    m_coverSprite->setAnchorPoint({0.5f, 0.5f});
    m_coverSprite->setPosition({m_size.width / 2.f, m_size.height / 2.f});

    if (m_clip) m_clip->addChild(m_coverSprite, 1);
    // Ocultar fallback cuando hay portada.
    if (m_fallback) m_fallback->setVisible(false);

    // Sincronizar la portada con el disco decorativo.
    if (m_smallDisc) m_smallDisc->setCoverFromPath(absolutePath);
}

void CoverHero::clearCover() {
    this->unschedule(schedule_selector(CoverHero::tickCarousel));
    m_coverPaths.clear();
    m_carouselIndex = 0;
    m_carouselElapsed = 0.f;
    setCoverFromPath("");
}

void CoverHero::startSpinning() {
    if (m_smallDisc) m_smallDisc->startSpinning();
}

void CoverHero::stopSpinning() {
    if (m_smallDisc) m_smallDisc->stopSpinning();
}

void CoverHero::setPausedAppearance(bool paused) {
    // Grisar el cover sprite + cover del disco pequeno. Usamos setColor que
    // tintea el sprite (valores < 255 mezclan con gris). Tambien apagamos
    // el giro del disco.
    const ccColor3B tint = paused
        ? ccColor3B{130, 130, 140}
        : ccColor3B{255, 255, 255};

    if (m_coverSprite) {
        m_coverSprite->setColor(tint);
        m_coverSprite->setOpacity(paused ? 200 : 255);
    }
    if (m_smallDisc) {
        m_smallDisc->setPausedAppearance(paused);
        if (paused) m_smallDisc->stopSpinning();
        else m_smallDisc->startSpinning();
    }
}

} // namespace paimon::menumusic
