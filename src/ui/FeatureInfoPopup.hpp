#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <string>
#include <vector>

// ────────────────────────────────────────────────────────────────────────────
// FeatureInfoPopup — Popup scrolleable que explica un feature del mod.
//
// Cada sección tiene un título coloreado y un cuerpo descriptivo.
// Se usa desde el Paimon Hub para que el usuario entienda qué hace cada
// configuración y dónde se aplica.
// ────────────────────────────────────────────────────────────────────────────

namespace paimon::ui {

struct InfoSection {
    std::string title;       // Título de la sección (ej: "Thumbnail Size")
    std::string body;        // Descripción de qué hace y dónde se aplica
    cocos2d::ccColor3B color = {100, 220, 255}; // Color del título
};

class FeatureInfoPopup : public geode::Popup {
public:
    static FeatureInfoPopup* create(
        std::string const& mainTitle,
        std::vector<InfoSection> const& sections
    );

protected:
    bool init(
        std::string const& mainTitle,
        std::vector<InfoSection> const& sections
    );

    void buildContent(
        std::string const& mainTitle,
        std::vector<InfoSection> const& sections
    );

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::ui
