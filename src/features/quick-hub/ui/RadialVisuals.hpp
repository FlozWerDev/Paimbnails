#pragma once

// Piezas de dibujo compartidas entre la rueda, su vista previa y las listas de
// configuracion, para que un boton se vea igual en los tres sitios.

#include <Geode/Geode.hpp>
#include "../data/QuickHubCategories.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::quickhub {

// Paleta de la rueda.
constexpr cocos2d::ccColor4F kRadialCardFill  = {0.05f, 0.06f, 0.10f, 0.94f};
constexpr cocos2d::ccColor4F kRadialHubFill   = {0.04f, 0.05f, 0.09f, 0.55f};
constexpr cocos2d::ccColor3B kRadialHintColor = {150, 160, 185};

inline cocos2d::ccColor4F accentColor(cocos2d::ccColor3B c, float alpha) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha};
}

// Los sprites de GD miden entre 20 y 120 px. Normalizarlos a una caja fija evita
// que un boton grande se salga de su tarjeta y que un icono pequeno se pierda.
inline cocos2d::CCSprite* makeFittedIcon(std::string const& frame, float box) {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame.c_str());
    if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
    if (!icon) return nullptr;

    auto size = icon->getContentSize();
    float longest = std::max(size.width, size.height);
    icon->setScale(longest > 0.f ? box / longest : 1.f);
    icon->setAnchorPoint({0.5f, 0.5f});
    return icon;
}

// Circulo relleno, centrado en el (0,0) del padre.
//
// Se arma con createRoundedRect (un poligono convexo recorrido por el borde) y
// no con un abanico desde el centro: CCDrawNode extruye cada vertice segun las
// normales de sus dos aristas, y en el vertice central de un abanico cerrado
// esas aristas son opuestas, asi que el calculo se degenera y la figura sale
// sin relleno, solo con su contorno.
inline cocos2d::CCDrawNode* makeCircle(
    float radius,
    cocos2d::ccColor4F fill,
    cocos2d::ccColor4F border = {0.f, 0.f, 0.f, 0.f},
    float borderWidth = 0.f
) {
    auto* node = paimon::SpriteHelper::createRoundedRect(
        radius * 2.f, radius * 2.f, radius, fill, border, borderWidth);
    if (node) node->setPosition({-radius, -radius});
    return node;
}

struct RadialBadge {
    cocos2d::CCNode* root = nullptr;
    cocos2d::CCNode* ring = nullptr; // aro de acento: solo al apuntar
};

// Disco liso con el icono dentro, centrado en el (0,0) del nodo devuelto.
// contentSize y anchorPoint se dejan a cero: con un anchorPoint centrado, cocos
// desplaza el origen local media insignia y las piezas caen fuera de sitio.
inline RadialBadge makeRadialBadge(
    RadialOptionDef const& def,
    RadialButtonShape shape,
    float size,
    bool dimmed = false
) {
    using namespace cocos2d;

    RadialBadge badge;
    badge.root = CCNode::create();

    float radius = shape == RadialButtonShape::Square ? size * 0.24f : size * 0.5f;

    if (shape != RadialButtonShape::Icon) {
        auto fill = kRadialCardFill;
        if (dimmed) fill.a = 0.45f;

        if (auto* card = paimon::SpriteHelper::createRoundedRect(size, size, radius, fill)) {
            card->setPosition({-size * 0.5f, -size * 0.5f});
            badge.root->addChild(card, 0);
        }

        float ringSize = size + 7.f;
        float ringRadius = shape == RadialButtonShape::Square ? radius + 3.5f : ringSize * 0.5f;
        if (auto* ring = paimon::SpriteHelper::createRoundedRectOutline(
                ringSize, ringSize, ringRadius, accentColor(def.color, 0.95f), 1.6f)) {
            ring->setPosition({-ringSize * 0.5f, -ringSize * 0.5f});
            ring->setVisible(false);
            badge.root->addChild(ring, 1);
            badge.ring = ring;
        }
    }

    if (auto* icon = makeFittedIcon(def.icon, size * 0.58f)) {
        icon->setPosition({0.f, 0.f});
        icon->setOpacity(dimmed ? 130 : 235);
        badge.root->addChild(icon, 2);
    }

    return badge;
}

struct RadialGeometry {
    float radius = 90.f;
    float badgeSize = 48.f;
};

// Reparte `count` insignias sin que se toquen ni se salgan de la pantalla:
// primero el radio mas grande que cabe, y luego el tamano que permite el arco
// disponible para cada una.
inline RadialGeometry radialGeometryFor(int count, cocos2d::CCSize winSize) {
    constexpr float kMaxBadge = 48.f;
    constexpr float kMinRadius = 80.f;
    constexpr float kTwoPi = 2.f * static_cast<float>(M_PI);

    RadialGeometry geometry;
    float maxRadius = std::max(kMinRadius,
        std::min(winSize.width, winSize.height) * 0.5f - kMaxBadge * 0.75f - 10.f);

    float needed = (kMaxBadge + 14.f) * static_cast<float>(std::max(count, 1)) / kTwoPi;
    geometry.radius = std::min(std::max(needed, kMinRadius), maxRadius);

    // El anillo de acento sobresale 6px de la tarjeta, asi que el hueco por
    // insignia tiene que descontarlo ademas del aire entre vecinas.
    float arc = count > 1 ? kTwoPi * geometry.radius / static_cast<float>(count) : kMaxBadge * 4.f;
    geometry.badgeSize = std::clamp(arc - 14.f, 26.f, kMaxBadge);
    return geometry;
}

// Item 1 arriba, avanzando en sentido horario: se lee como una lista.
inline float radialAngleFor(int index, int count) {
    if (count <= 0) return 90.f;
    return 90.f - (360.f / static_cast<float>(count)) * static_cast<float>(index);
}

} // namespace paimon::quickhub
