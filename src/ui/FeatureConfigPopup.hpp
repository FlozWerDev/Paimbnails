#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// FeatureConfigPopup
//
// Popup generico que muestra las configuraciones agrupadas para un "feature"
// del Paimon Hub. Cada feature tiene un identificador (featureKey) que apunta
// a un builder en el registro interno; el builder usa los widgets reutilizables
// de paimon::settings_ui para renderizar toggles, sliders, dropdowns, etc.
//
// Uso tipico desde el Hub:
//
//   if (auto* popup = paimon::ui::FeatureConfigPopup::create("popup-animation"))
//       popup->show();
//
// Ademas se expone openFeatureConfigFor(englishGranularName) que enruta cada
// setting granular del buscador a:
//   1. un popup dedicado existente (Pet, Cursor, Discord, Background, etc.),
//      cuando aplica
//   2. un FeatureConfigPopup con el grupo correspondiente
//   3. el panel de settings tradicional como ultimo recurso
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::ui {

class FeatureConfigPopup : public geode::Popup {
public:
    static FeatureConfigPopup* create(std::string const& featureKey);

    // Devuelve true si existe un grupo registrado con esa clave.
    static bool hasFeatureKey(std::string const& featureKey);

protected:
    bool init(std::string const& featureKey);

    geode::ScrollLayer* m_scroll = nullptr;
};

// Enruta un setting granular (tomado del listado del buscador) a su popup
// dedicado correspondiente, cayendo al panel de settings si no hay nada.
//
// englishGranularName: el nombre en ingles tal cual aparece en getGranularSettings()
// fallbackCategoryIndex: indice de categoria del Settings Panel a abrir si no
//                       hay popup dedicado.
void openFeatureConfigFor(std::string const& englishGranularName,
                          int fallbackCategoryIndex);

} // namespace paimon::ui
