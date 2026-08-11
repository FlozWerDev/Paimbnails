#include "IconHelpPopup.hpp"

#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <string>
#include <vector>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 280.f;

struct Entry { char const* title; char const* text; cocos2d::ccColor3B accent; };

std::vector<Entry> entriesFor(IconHelpPopup::Topic topic) {
    switch (topic) {
        case IconHelpPopup::Topic::Paint:
            return {
                {"Color", "Un color plano para toda la capa. Es lo mas rapido "
                          "y lo que mejor se ve en movimiento.",
                 ui::kAccentPaint},
                {"Degradado", "Una mezcla de dos o mas colores. Lineal va de un "
                              "lado a otro; radial sale del centro hacia afuera.",
                 ui::kAccentPaint},
                {"Imagen", "Usa un PNG como pintura: solo se ve dentro de la "
                           "forma de la capa, como si la recortaras.",
                 ui::kAccentPaint},
                {"Sombreado 3D", "Deja que las sombras del dibujo original se "
                                 "noten a traves del color. Apagalo si quieres "
                                 "un color completamente plano.",
                 ui::kAccentShape},
                {"Borde", "Dibuja un contorno alrededor de la capa. Un borde "
                          "oscuro hace que el icono se lea mejor en el juego.",
                 ui::kAccentShape},
                {"Arcoiris", "La capa se pinta de blanco y el mod le va "
                             "cambiando el tono. Solo se anima en el garaje.",
                 ui::kAccentProject},
            };
        case IconHelpPopup::Topic::Export:
            return {
                {"Usar", "Compila el icono y lo activa en tu kit ahora mismo. "
                         "Es lo unico que necesitas para jugar con el.",
                 ui::kAccentProject},
                {"Colores reales", "El juego suele pintar el icono con tus "
                                   "colores de jugador. Con esto activado se "
                                   "ven los colores que tu elegiste.",
                 ui::kAccentProject},
                {"Exportar", "Genera los archivos (SD, HD y UHD). Puedes abrir "
                             "la carpeta o copiarlos a More Icons para que el "
                             "icono siga instalado aunque quites el mod.",
                 ui::kAccentProject},
                {"Compartir", "Crea un archivo .paimbicon con todo dentro para "
                              "pasarselo a alguien.",
                 ui::kAccentProject},
            };
        case IconHelpPopup::Topic::Basics:
        default:
            return {
                {"1. Las zonas", "Un icono no es una sola imagen: el juego lo "
                                 "arma con varias piezas. El Cuerpo lleva tu "
                                 "Color 1, el Detalle tu Color 2, el Brillo es "
                                 "el contorno y el Blanco son los ojos. Elige "
                                 "una zona arriba y solo editas esa.",
                 ui::kAccentZones},
                {"2. Las capas", "Dentro de una zona apilas capas, como "
                                 "pegatinas. La de mas arriba en la lista tapa "
                                 "a las de abajo. Empieza con una y agrega mas "
                                 "solo si te hace falta.",
                 ui::kAccentLayers},
                {"3. Pintar", "Cada capa se pinta con un color, un degradado o "
                              "una imagen. Es lo que mas cambia el resultado, "
                              "asi que prueba sin miedo: puedes deshacer.",
                 ui::kAccentPaint},
                {"4. Colocar", "Arrastra en la vista previa para mover la capa "
                               "seleccionada, o toca una zona del icono para "
                               "saltar a ella.",
                 ui::kAccentShape},
            };
    }
}

}  // anonymous namespace

IconHelpPopup* IconHelpPopup::create(Topic topic) {
    auto* p = new IconHelpPopup();
    if (p->init(topic)) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool IconHelpPopup::init(Topic topic) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    setTitle("Como funciona");
    setID("icon-maker-help-popup"_spr);

    auto size = m_mainLayer->getContentSize();

    auto* tabs = kit::makeTabBar(size.width - 30.f,
        {"Lo basico", "Pintura", "Usar y exportar"},
        static_cast<int>(topic),
        [this](int index) {
            Ref<IconHelpPopup> self = this;
            Loader::get()->queueInMainThread([self, index] {
                if (self && self->getParent()) {
                    self->showTopic(static_cast<Topic>(index));
                }
            });
        });
    if (tabs) {
        tabs->setPosition({15.f, size.height - 62.f});
        m_mainLayer->addChild(tabs);
    }

    m_body = CCNode::create();
    m_body->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_body);

    showTopic(topic);
    return true;
}

void IconHelpPopup::showTopic(Topic topic) {
    if (!m_body) return;
    m_body->removeAllChildren();

    auto size = m_mainLayer->getContentSize();
    float const cardW = size.width - 30.f;
    float const innerW = kit::cardInnerWidth(cardW);

    std::vector<CCNode*> cards;
    for (auto const& entry : entriesFor(topic)) {
        cards.push_back(kit::makeCard(cardW, entry.title, entry.accent,
                                      {kit::makeHint(innerW, entry.text)}));
    }

    auto* scroll = kit::makeScrollStack({cardW, size.height - 84.f}, cards, 6.f);
    if (scroll) {
        scroll->setPosition({15.f, 12.f});
        m_body->addChild(scroll);
    }
}

}  // namespace paimon::icon_maker
